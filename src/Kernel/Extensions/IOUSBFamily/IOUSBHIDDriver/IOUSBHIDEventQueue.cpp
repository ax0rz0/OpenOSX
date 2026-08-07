#include "IOUSBHIDEventQueue.h"

#include <IOKit/IOLib.h>
#include <IOKit/IOService.h>
#include <kern/thread_call.h>
#include <miscfs/devfs/devfs.h>
#include <sys/conf.h>
#include <sys/errno.h>
#include <sys/uio.h>

extern "C" int devfs_is_ready(void);

static void *gKbdNode;
static int gKbdMajor = -1;
static thread_call_t gKbdRetryCall;
static IONotifier *gKbdBSDNotifier;
static IOLock *gKbdLock;
static UInt32 gKbdSequence;
static UInt32 gKbdReadIndex;
static UInt32 gKbdWriteIndex;
static USBHIDKbdEvent gKbdEvents[64];

static void *gMouseNode;
static int gMouseMajor = -1;
static thread_call_t gMouseRetryCall;
static IONotifier *gMouseBSDNotifier;
static IOLock *gMouseLock;
static UInt32 gMouseSequence;
static UInt32 gMouseReadIndex;
static UInt32 gMouseWriteIndex;
static USBHIDMouseEvent gMouseEvents[64];

static void usb_hid_kbd_publish_retry(thread_call_param_t, thread_call_param_t);
static void usb_hid_mouse_publish_retry(thread_call_param_t, thread_call_param_t);
static bool usb_hid_kbd_iobsd_published(void *, void *, IOService *, IONotifier *);
static bool usb_hid_mouse_iobsd_published(void *, void *, IOService *, IONotifier *);

static int usb_hid_open(dev_t, int, int, struct proc *) { return 0; }
static int usb_hid_close(dev_t, int, int, struct proc *) { return 0; }

/* spec_open() calls d_open on every open but spec_close() only reaches d_close
 * once vcount() drops to zero, so this tracks "somebody has it open" rather
 * than a balanced count. */
static volatile bool gKbdGrabbed;

static int
usb_hid_kbd_open(dev_t, int, int, struct proc *)
{
    gKbdGrabbed = true;
    return 0;
}

static int
usb_hid_kbd_close(dev_t, int, int, struct proc *)
{
    gKbdGrabbed = false;
    return 0;
}

bool
USBHIDKeyboardIsGrabbed(void)
{
    return gKbdGrabbed;
}

static int
usb_hid_kbd_read(dev_t, struct uio *uio, int)
{
    USBHIDKbdEvent event;
    int error = 0;

    if (!gKbdLock) return ENXIO;
    if (uio_resid(uio) < (user_ssize_t)sizeof(event)) return EINVAL;

    IOLockLock(gKbdLock);
    if (gKbdReadIndex == gKbdWriteIndex) {
        error = EAGAIN;
    } else {
        event = gKbdEvents[gKbdReadIndex % (UInt32)(sizeof(gKbdEvents) / sizeof(gKbdEvents[0]))];
        gKbdReadIndex++;
    }
    IOLockUnlock(gKbdLock);

    if (error) return error;
    return uiomove((const char *)&event, (int)sizeof(event), uio);
}

static int
usb_hid_mouse_read(dev_t, struct uio *uio, int)
{
    USBHIDMouseEvent event;
    int error = 0;

    if (!gMouseLock) return ENXIO;
    if (uio_resid(uio) < (user_ssize_t)sizeof(event)) return EINVAL;

    IOLockLock(gMouseLock);
    if (gMouseReadIndex == gMouseWriteIndex) {
        error = EAGAIN;
    } else {
        event = gMouseEvents[gMouseReadIndex % (UInt32)(sizeof(gMouseEvents) / sizeof(gMouseEvents[0]))];
        gMouseReadIndex++;
    }
    IOLockUnlock(gMouseLock);

    if (error) return error;
    return uiomove((const char *)&event, (int)sizeof(event), uio);
}

static struct cdevsw usb_hid_kbd_cdevsw = {
    usb_hid_kbd_open, usb_hid_kbd_close, usb_hid_kbd_read, eno_rdwrt,
    eno_ioctl, eno_stop, eno_reset, 0, eno_select, eno_mmap,
    eno_strat, eno_getc, eno_putc, 0
};

static struct cdevsw usb_hid_mouse_cdevsw = {
    usb_hid_open, usb_hid_close, usb_hid_mouse_read, eno_rdwrt,
    eno_ioctl, eno_stop, eno_reset, 0, eno_select, eno_mmap,
    eno_strat, eno_getc, eno_putc, 0
};

static void
schedule_retry(thread_call_t *call, thread_call_func_t func)
{
    AbsoluteTime deadline;
    if (!*call) {
        *call = thread_call_allocate(func, NULL);
        if (!*call) return;
    }
    clock_interval_to_deadline(1, kSecondScale, &deadline);
    thread_call_enter_delayed(*call, deadline);
}

void
USBHIDPublishKeyboardDevice(void)
{
    if (gKbdNode) return;
    if (!gKbdLock) {
        gKbdLock = IOLockAlloc();
        if (!gKbdLock) return;
    }
    if (gKbdMajor < 0) {
        gKbdMajor = cdevsw_add(-1, &usb_hid_kbd_cdevsw);
        if (gKbdMajor < 0) return;
    }
    if (!devfs_is_ready()) {
        if (!gKbdBSDNotifier) {
            OSDictionary *matching = IOService::resourceMatching("IOBSD");
            if (matching) {
                gKbdBSDNotifier = IOService::addMatchingNotification(gIOPublishNotification,
                                                                     matching,
                                                                     usb_hid_kbd_iobsd_published,
                                                                     NULL, NULL);
                matching->release();
            }
        }
        schedule_retry(&gKbdRetryCall, usb_hid_kbd_publish_retry);
        return;
    }
    gKbdNode = devfs_make_node(makedev(gKbdMajor, 0), DEVFS_CHAR, 0, 0, 0666, "usb_hid_kbd");
    if (!gKbdNode)
        schedule_retry(&gKbdRetryCall, usb_hid_kbd_publish_retry);
    else
        IOLog("IOUSBHIDDriver: published /dev/usb_hid_kbd\n");
}

void
USBHIDPublishMouseDevice(void)
{
    if (gMouseNode) return;
    if (!gMouseLock) {
        gMouseLock = IOLockAlloc();
        if (!gMouseLock) return;
    }
    if (gMouseMajor < 0) {
        gMouseMajor = cdevsw_add(-1, &usb_hid_mouse_cdevsw);
        if (gMouseMajor < 0) return;
    }
    if (!devfs_is_ready()) {
        if (!gMouseBSDNotifier) {
            OSDictionary *matching = IOService::resourceMatching("IOBSD");
            if (matching) {
                gMouseBSDNotifier = IOService::addMatchingNotification(gIOPublishNotification,
                                                                       matching,
                                                                       usb_hid_mouse_iobsd_published,
                                                                       NULL, NULL);
                matching->release();
            }
        }
        schedule_retry(&gMouseRetryCall, usb_hid_mouse_publish_retry);
        return;
    }
    gMouseNode = devfs_make_node(makedev(gMouseMajor, 0), DEVFS_CHAR, 0, 0, 0666, "usb_hid_mouse");
    if (!gMouseNode)
        schedule_retry(&gMouseRetryCall, usb_hid_mouse_publish_retry);
    else
        IOLog("IOUSBHIDDriver: published /dev/usb_hid_mouse\n");
}

static void usb_hid_kbd_publish_retry(thread_call_param_t, thread_call_param_t)
{
    USBHIDPublishKeyboardDevice();
}

static void usb_hid_mouse_publish_retry(thread_call_param_t, thread_call_param_t)
{
    USBHIDPublishMouseDevice();
}

static bool usb_hid_kbd_iobsd_published(void *, void *, IOService *, IONotifier *notifier)
{
    USBHIDPublishKeyboardDevice();
    if (gKbdNode && notifier) {
        notifier->remove();
        if (gKbdBSDNotifier == notifier) gKbdBSDNotifier = NULL;
    }
    return true;
}

static bool usb_hid_mouse_iobsd_published(void *, void *, IOService *, IONotifier *notifier)
{
    USBHIDPublishMouseDevice();
    if (gMouseNode && notifier) {
        notifier->remove();
        if (gMouseBSDNotifier == notifier) gMouseBSDNotifier = NULL;
    }
    return true;
}

void
USBHIDPushKeyboardEvent(UInt8 usage, bool down)
{
    USBHIDKbdEvent event;
    UInt32 slot;

    if (!gKbdLock) {
        USBHIDPublishKeyboardDevice();
        if (!gKbdLock) return;
    }

    event.sequence = ++gKbdSequence;
    event.usage = usage;
    event.down = down ? 1 : 0;
    event.reserved[0] = event.reserved[1] = 0;

    IOLockLock(gKbdLock);
    slot = gKbdWriteIndex % (UInt32)(sizeof(gKbdEvents) / sizeof(gKbdEvents[0]));
    gKbdEvents[slot] = event;
    gKbdWriteIndex++;
    if (gKbdWriteIndex - gKbdReadIndex > (UInt32)(sizeof(gKbdEvents) / sizeof(gKbdEvents[0])))
        gKbdReadIndex = gKbdWriteIndex - (UInt32)(sizeof(gKbdEvents) / sizeof(gKbdEvents[0]));
    IOLockUnlock(gKbdLock);
}

void
USBHIDPushMouseEvent(UInt8 mouseIndex, UInt8 buttons, SInt8 dx, SInt8 dy, SInt8 wheel)
{
    USBHIDMouseEvent event;
    UInt32 slot;

    if (!gMouseLock) {
        USBHIDPublishMouseDevice();
        if (!gMouseLock) return;
    }

    event.sequence = ++gMouseSequence;
    event.mouseIndex = mouseIndex;
    event.buttons = buttons;
    event.dx = dx;
    event.dy = dy;
    event.wheel = wheel;
    bzero(event.reserved, sizeof(event.reserved));

    IOLockLock(gMouseLock);
    slot = gMouseWriteIndex % (UInt32)(sizeof(gMouseEvents) / sizeof(gMouseEvents[0]));
    gMouseEvents[slot] = event;
    gMouseWriteIndex++;
    if (gMouseWriteIndex - gMouseReadIndex > (UInt32)(sizeof(gMouseEvents) / sizeof(gMouseEvents[0])))
        gMouseReadIndex = gMouseWriteIndex - (UInt32)(sizeof(gMouseEvents) / sizeof(gMouseEvents[0]));
    IOLockUnlock(gMouseLock);
}
