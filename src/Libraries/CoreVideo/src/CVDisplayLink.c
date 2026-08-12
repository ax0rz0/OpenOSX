/*
 * CVDisplayLink - a timer that calls back once per display refresh.
 *
 * OpenOSX implementation (not Apple source). On real hardware a display link
 * is driven by the GPU's vblank interrupt; OpenOSX's framebuffer exposes no
 * vblank, so this is a pthread that fires the stored callback every ~16.67 ms
 * (60 Hz) using mach_absolute_time for the CVTimeStamp. That is a faithful
 * stand-in: an app that uses a display link to pace rendering gets an even
 * 60 Hz tick, which is exactly what it asked for. SDL2 (and so The Powder Toy)
 * uses one purely for timing, never for a real vblank guarantee.
 *
 * The object is a CFType (registered like CGColor / DADisk) so CVDisplayLink-
 * Retain/Release, which alias CFRetain/CFRelease, work on it.
 */
#include <CoreVideo/CVReturn.h>
#include <CoreVideo/CVBase.h>
#include <CoreVideo/CVDisplayLink.h>
#include <CoreFoundation/CoreFoundation.h>
#include <CoreFoundation/CFRuntime.h>
#include <mach/mach_time.h>
#include <pthread.h>
#include <stdlib.h>
#include <string.h>

#define OPENOSX_REFRESH_HZ   60
#define OPENOSX_PERIOD_NS    (1000000000LL / OPENOSX_REFRESH_HZ)

struct openosx_display_link {
    CFRuntimeBase base;
    CVDisplayLinkOutputCallback callback;
    void *userInfo;
    pthread_t thread;
    volatile int running;
    CGDirectDisplayID display;
    double timebase_ns_per_tick;
};

static CFTypeID g_typeID = _kCFRuntimeNotATypeID;

static void dl_dealloc(CFTypeRef o)
{
    struct openosx_display_link *dl = (struct openosx_display_link *)o;
    if (dl->running) {
        dl->running = 0;
        pthread_join(dl->thread, NULL);
    }
}

static const CFRuntimeClass dl_class = {
    0, "CVDisplayLink", NULL, NULL, dl_dealloc, NULL, NULL, NULL, NULL
};

static void dl_register(void) { g_typeID = _CFRuntimeRegisterClass(&dl_class); }

CFTypeID CVDisplayLinkGetTypeID(void)
{
    static pthread_once_t once = PTHREAD_ONCE_INIT;
    pthread_once(&once, dl_register);
    return g_typeID;
}

static uint64_t now_ns(struct openosx_display_link *dl)
{
    return (uint64_t)(mach_absolute_time() * dl->timebase_ns_per_tick);
}

static void fill_timestamp(struct openosx_display_link *dl, uint64_t ns, CVTimeStamp *ts)
{
    memset(ts, 0, sizeof(*ts));
    ts->version = 0;
    ts->flags = kCVTimeStampVideoTimeValid | kCVTimeStampHostTimeValid |
                kCVTimeStampVideoRefreshPeriodValid;
    ts->hostTime = mach_absolute_time();
    ts->videoTime = (int64_t)ns;
    ts->videoTimeScale = 1000000000;                 /* ns */
    ts->videoRefreshPeriod = OPENOSX_PERIOD_NS;
}

static void *dl_loop(void *arg)
{
    struct openosx_display_link *dl = arg;
    uint64_t next = now_ns(dl);
    while (dl->running) {
        uint64_t t = now_ns(dl);
        if (t >= next) {
            CVTimeStamp inNow, inOutputTime;
            fill_timestamp(dl, t, &inNow);
            fill_timestamp(dl, next + OPENOSX_PERIOD_NS, &inOutputTime);
            if (dl->callback)
                dl->callback((CVDisplayLinkRef)dl, &inNow, &inOutputTime,
                             0, NULL, dl->userInfo);
            next += OPENOSX_PERIOD_NS;
        } else {
            struct timespec sleep = { 0, (long)(next - t) };
            if (sleep.tv_nsec > 2000000) sleep.tv_nsec = 2000000; /* cap 2ms slices */
            nanosleep(&sleep, NULL);
        }
    }
    return NULL;
}

static CVDisplayLinkRef dl_create(CGDirectDisplayID display)
{
    CVDisplayLinkGetTypeID();
    struct openosx_display_link *dl = (struct openosx_display_link *)
        _CFRuntimeCreateInstance(kCFAllocatorDefault, g_typeID,
                                 sizeof(struct openosx_display_link) - sizeof(CFRuntimeBase),
                                 NULL);
    if (!dl) return NULL;
    dl->callback = NULL;
    dl->userInfo = NULL;
    dl->running = 0;
    dl->display = display;
    mach_timebase_info_data_t tb;
    mach_timebase_info(&tb);
    dl->timebase_ns_per_tick = (double)tb.numer / (double)tb.denom;
    return (CVDisplayLinkRef)dl;
}

CVReturn CVDisplayLinkCreateWithActiveCGDisplays(CVDisplayLinkRef *out)
{
    if (!out) return kCVReturnError;
    *out = dl_create(1 /* main display */);
    return *out ? kCVReturnSuccess : kCVReturnAllocationFailed;
}

CVReturn CVDisplayLinkCreateWithCGDisplay(CGDirectDisplayID display, CVDisplayLinkRef *out)
{
    if (!out) return kCVReturnError;
    *out = dl_create(display);
    return *out ? kCVReturnSuccess : kCVReturnAllocationFailed;
}

CVReturn CVDisplayLinkSetOutputCallback(CVDisplayLinkRef ref,
                                        CVDisplayLinkOutputCallback cb, void *userInfo)
{
    struct openosx_display_link *dl = (struct openosx_display_link *)ref;
    if (!dl) return kCVReturnError;
    dl->callback = cb;
    dl->userInfo = userInfo;
    return kCVReturnSuccess;
}

CVReturn CVDisplayLinkStart(CVDisplayLinkRef ref)
{
    struct openosx_display_link *dl = (struct openosx_display_link *)ref;
    if (!dl) return kCVReturnError;
    if (dl->running) return kCVReturnSuccess;
    dl->running = 1;
    if (pthread_create(&dl->thread, NULL, dl_loop, dl) != 0) {
        dl->running = 0;
        return kCVReturnError;
    }
    return kCVReturnSuccess;
}

CVReturn CVDisplayLinkStop(CVDisplayLinkRef ref)
{
    struct openosx_display_link *dl = (struct openosx_display_link *)ref;
    if (!dl) return kCVReturnError;
    if (dl->running) {
        dl->running = 0;
        pthread_join(dl->thread, NULL);
    }
    return kCVReturnSuccess;
}

CVTime CVDisplayLinkGetNominalOutputVideoRefreshPeriod(CVDisplayLinkRef ref)
{
    (void)ref;
    /* value/timeScale = 1/60 s, as Apple reports it. */
    CVTime t;
    t.timeValue = 1;
    t.timeScale = OPENOSX_REFRESH_HZ;
    t.flags = 0;                             /* a definite value, not indefinite */
    return t;
}

CVReturn CVDisplayLinkSetCurrentCGDisplay(CVDisplayLinkRef ref, CGDirectDisplayID display)
{
    struct openosx_display_link *dl = (struct openosx_display_link *)ref;
    if (!dl) return kCVReturnError;
    dl->display = display;
    return kCVReturnSuccess;
}

/* SDL2 calls this to bind the link to whatever display a GL context is on. We
 * have one display, so it is a successful no-op beyond recording intent. */
CVReturn CVDisplayLinkSetCurrentCGDisplayFromOpenGLContext(CVDisplayLinkRef ref,
                                                           void *glContext, void *pixelFormat)
{
    (void)glContext; (void)pixelFormat;
    return ref ? kCVReturnSuccess : kCVReturnError;
}

CVReturn CVDisplayLinkSetCurrentCGDisplayFromOpenGLContextInternal(CVDisplayLinkRef ref,
                                                                   void *a, void *b)
{ return CVDisplayLinkSetCurrentCGDisplayFromOpenGLContext(ref, a, b); }

Boolean CVDisplayLinkIsRunning(CVDisplayLinkRef ref)
{
    struct openosx_display_link *dl = (struct openosx_display_link *)ref;
    return dl && dl->running;
}

CVDisplayLinkRef CVDisplayLinkRetain(CVDisplayLinkRef ref)
{ if (ref) CFRetain(ref); return ref; }

void CVDisplayLinkRelease(CVDisplayLinkRef ref)
{ if (ref) CFRelease(ref); }
