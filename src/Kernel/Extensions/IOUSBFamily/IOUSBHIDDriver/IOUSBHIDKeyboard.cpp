#include "IOUSBHIDKeyboard.h"
#include "IOUSBHIDEventQueue.h"

#include <IOKit/IOLib.h>
#include <IOKit/hidsystem/IOHIDParameter.h>
#include <IOKit/hidsystem/IOHIDShared.h>
#include <kern/thread.h>
#include "../../ApplePS2Controller/ApplePS2KeyboardMap.h"

#define super IOHIKeyboard
OSDefineMetaClassAndStructors(IOUSBHIDKeyboard, IOHIKeyboard);

#define DEADKEY 0xFF

enum {
    kUSBHIDReqTypeSet = 0x21,
    kUSBHIDReqSetIdle = 0x0A,
    kUSBHIDReqSetProtocol = 0x0B,
    kUSBHIDProtocolBoot = 0
};

static const UInt8 gUSBToADB[256] = {
    DEADKEY, DEADKEY, DEADKEY, DEADKEY,
    0x00, 0x0b, 0x08, 0x02, 0x0e, 0x03, 0x05, 0x04, 0x22, 0x26, 0x28, 0x25,
    0x2e, 0x2d, 0x1f, 0x23, 0x0c, 0x0f, 0x01, 0x11, 0x20, 0x09, 0x0d, 0x07,
    0x10, 0x06, 0x12, 0x13, 0x14, 0x15, 0x17, 0x16, 0x1a, 0x1c, 0x19, 0x1d,
    0x24, 0x35, 0x33, 0x30, 0x31, 0x1b, 0x18, 0x21, 0x1e, 0x2a, 0x2a, 0x29,
    0x27, 0x32, 0x2b, 0x2f, 0x2c, 0x39, 0x7a, 0x78, 0x63, 0x76, 0x60, 0x61,
    0x62, 0x64, 0x65, 0x6d, 0x67, 0x6f, 0x69, 0x6b, 0x71, 0x72, 0x73, 0x74,
    0x75, 0x77, 0x79, 0x7c, 0x7b, 0x7d, 0x7e, 0x47, 0x4b, 0x43, 0x4e, 0x45,
    0x4c, 0x53, 0x54, 0x55, 0x56, 0x57, 0x58, 0x59, 0x5b, 0x5c, 0x52, 0x41,
    0x0a, 0x6e, 0x7f, 0x51, 0x69, 0x6b, 0x71, 0x6a, 0x40, 0x4f, 0x50, 0x5a,
    DEADKEY, DEADKEY, DEADKEY, DEADKEY, DEADKEY, 0x72,
};

static const UInt8 gUSBModToADB[8] = {
    0x3b, 0x38, 0x3a, 0x37, 0x3e, 0x3c, 0x3d, 0x36
};

bool IOUSBHIDKeyboard::init(OSDictionary *dict)
{
    if (!super::init(dict)) return false;
    fInterface = NULL;
    fInterruptPipe = NULL;
    fReportMem = NULL;
    fRunning = false;
    fConsoleGrabbed = false;
    bzero(fLastReport, sizeof(fLastReport));
    setName("IOUSBHIDKeyboard");
    setProperty("HIDKeyboardKeysDefined", kOSBooleanTrue);
    setProperty(kIOHIDVirtualHIDevice, kOSBooleanFalse);
    setProperty(kIOHIDKindKey, kHIKeyboardDevice, 32);
    setProperty(kIOHIDInterfaceIDKey, NX_EVS_DEVICE_INTERFACE_ADB, 32);
    setProperty(kIOHIDSubinterfaceIDKey, NX_EVS_DEVICE_TYPE_KEYBOARD, 32);
    setProperty("Transport", "USB");
    setProperty("USB Product Name", "USB HID Keyboard");
    return true;
}

bool IOUSBHIDKeyboard::start(IOService *provider)
{
    IOUSBFindEndpointRequest request;

    fInterface = OSDynamicCast(IOUSBInterface, provider);
    if (!fInterface || !super::start(provider)) return false;

    if (!fInterface->open(this)) {
        IOLog("IOUSBHIDKeyboard: failed to open interface\n");
        return false;
    }

    bzero(&request, sizeof(request));
    request.type = kUSBInterrupt;
    request.direction = kUSBIn;
    fInterruptPipe = fInterface->FindNextPipe(NULL, &request, true);
    if (!fInterruptPipe) {
        IOLog("IOUSBHIDKeyboard: no interrupt-IN pipe\n");
        fInterface->close(this);
        return false;
    }

    fReportMem = IOBufferMemoryDescriptor::inTaskWithOptions(kernel_task, kIODirectionInOut, 8);
    if (!fReportMem) {
        fInterruptPipe->release();
        fInterruptPipe = NULL;
        fInterface->close(this);
        return false;
    }

    IOLog("IOUSBHIDKeyboard: using existing HID protocol\n");
    USBHIDPublishKeyboardDevice();

    fRunning = true;
    thread_t thread = THREAD_NULL;
    if (kernel_thread_start((thread_continue_t)&IOUSBHIDKeyboard::pollThread, this, &thread) != KERN_SUCCESS) {
        fRunning = false;
        IOLog("IOUSBHIDKeyboard: failed to start poll thread\n");
        return false;
    }
    thread_deallocate(thread);
    IOLog("IOUSBHIDKeyboard: started\n");
    return true;
}

void IOUSBHIDKeyboard::stop(IOService *provider)
{
    fRunning = false;
    if (fInterface) fInterface->close(this);
    super::stop(provider);
}

void IOUSBHIDKeyboard::free()
{
    fRunning = false;
    if (fReportMem) { fReportMem->release(); fReportMem = NULL; }
    if (fInterruptPipe) { fInterruptPipe->release(); fInterruptPipe = NULL; }
    super::free();
}

bool IOUSBHIDKeyboard::setBootProtocol()
{
    IOUSBDevRequest req;
    bzero(&req, sizeof(req));
    req.bmRequestType = kUSBHIDReqTypeSet;
    req.bRequest = kUSBHIDReqSetProtocol;
    req.wValue = kUSBHIDProtocolBoot;
    req.wIndex = fInterface->GetInterfaceNumber();
    req.wLength = 0;
    fInterface->DeviceRequest(&req);

    bzero(&req, sizeof(req));
    req.bmRequestType = kUSBHIDReqTypeSet;
    req.bRequest = kUSBHIDReqSetIdle;
    req.wValue = 0;
    req.wIndex = fInterface->GetInterfaceNumber();
    req.wLength = 0;
    fInterface->DeviceRequest(&req);
    return true;
}

void IOUSBHIDKeyboard::pollThread(void *arg, wait_result_t)
{
    ((IOUSBHIDKeyboard *)arg)->pollLoop();
    thread_terminate(current_thread());
}

void IOUSBHIDKeyboard::pollLoop()
{
    while (fRunning) {
        IOByteCount bytesRead = 8;
        bzero(fReportMem->getBytesNoCopy(), 8);
        IOReturn ret = fInterruptPipe->Read(fReportMem, 0, 0, 8,
                                            (IOUSBCompletion *)NULL, &bytesRead);
        if (ret == kIOReturnSuccess && bytesRead > 0) {
            UInt8 report[8];
            bzero(report, sizeof(report));
            bcopy(fReportMem->getBytesNoCopy(), report, bytesRead > 8 ? 8 : bytesRead);
            handleReport(report);
        } else {
            IOSleep(10);
        }
    }
}

static bool reportHasUsage(const UInt8 report[8], UInt8 usage)
{
    for (int i = 2; i < 8; i++)
        if (report[i] == usage) return true;
    return false;
}

void IOUSBHIDKeyboard::handleReport(const UInt8 report[8])
{
    bool grabbed = USBHIDKeyboardIsGrabbed();
    if (grabbed && !fConsoleGrabbed) {
        /* Anything held down when the grab started has already been reported
         * to the console; without a matching release it stays latched there
         * for the lifetime of the compositor. */
        AbsoluteTime now;
        clock_get_uptime((uint64_t *)&now);
        for (UInt8 bit = 0; bit < 8; bit++)
            if (fLastReport[0] & (1U << bit))
                dispatchKeyboardEvent(gUSBModToADB[bit], false, now);
        for (int i = 2; i < 8; i++) {
            UInt8 usage = fLastReport[i];
            if (usage >= 4 && usage <= 0x75 && gUSBToADB[usage] != DEADKEY)
                dispatchKeyboardEvent(gUSBToADB[usage], false, now);
        }
    }
    fConsoleGrabbed = grabbed;

    if (report[2] == 1) {
        bcopy(report, fLastReport, sizeof(fLastReport));
        return;
    }

    UInt8 changedMods = report[0] ^ fLastReport[0];
    for (UInt8 bit = 0; bit < 8; bit++)
        if (changedMods & (1U << bit))
            dispatchModifier(bit, !!(report[0] & (1U << bit)));

    for (int i = 2; i < 8; i++) {
        UInt8 usage = fLastReport[i];
        if (usage >= 4 && !reportHasUsage(report, usage))
            dispatchUSBUsage(usage, false);
    }
    for (int i = 2; i < 8; i++) {
        UInt8 usage = report[i];
        if (usage >= 4 && !reportHasUsage(fLastReport, usage))
            dispatchUSBUsage(usage, true);
    }
    bcopy(report, fLastReport, sizeof(fLastReport));
}

void IOUSBHIDKeyboard::dispatchUSBUsage(UInt8 usage, bool down)
{
    if (usage > 0x75) return;
    USBHIDPushKeyboardEvent(usage, down);

    if (fConsoleGrabbed) return;

    UInt8 adb = gUSBToADB[usage];
    if (adb == DEADKEY) return;

    AbsoluteTime now;
    clock_get_uptime((uint64_t *)&now);
    dispatchKeyboardEvent(adb, down, now);
}

void IOUSBHIDKeyboard::dispatchModifier(UInt8 bit, bool down)
{
    if (bit >= 8) return;
    USBHIDPushKeyboardEvent((UInt8)(0xE0 + bit), down);

    if (fConsoleGrabbed) return;

    AbsoluteTime now;
    clock_get_uptime((uint64_t *)&now);
    dispatchKeyboardEvent(gUSBModToADB[bit], down, now);
}

void IOUSBHIDKeyboard::setAlphaLockFeedback(bool)
{
}

const unsigned char *IOUSBHIDKeyboard::defaultKeymapOfLength(UInt32 *length)
{
    *length = sizeof(applePS2USAKeyMap);
    return applePS2USAKeyMap;
}

UInt32 IOUSBHIDKeyboard::maxKeyCodes()
{
    return NX_NUMKEYCODES;
}

UInt32 IOUSBHIDKeyboard::deviceType()
{
    return NX_EVS_DEVICE_TYPE_KEYBOARD;
}

UInt32 IOUSBHIDKeyboard::interfaceID()
{
    return NX_EVS_DEVICE_INTERFACE_ADB;
}
