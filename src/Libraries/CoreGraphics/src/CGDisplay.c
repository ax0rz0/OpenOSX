/*
 * CGDirectDisplay - display enumeration, modes, gamma and cursor.
 *
 * OpenOSX implementation (not Apple source). This is the bulk of what SDL2
 * apps like The Powder Toy import from CoreGraphics: not the 2D drawing API
 * (they render through OpenGL/Metal) but the display-info and cursor calls a
 * windowing layer makes to find the screen and go fullscreen.
 *
 * OpenOSX presents a single display whose geometry is read live from the GOP
 * framebuffer via PDGOP - so CGDisplayBounds and the display mode report the
 * real resolution, not a guess. Fullscreen capture, gamma fade and gamma
 * tables are accepted and succeed as no-ops: on a single software framebuffer
 * there is nothing to capture from other displays and no hardware gamma ramp,
 * and an app that "captures" the one display then draws to it works exactly as
 * if the capture had taken effect.
 */
#include <CoreGraphics/CoreGraphics.h>
#include <CoreFoundation/CoreFoundation.h>
#include <PDGOP.h>
#include <stdlib.h>
#include <string.h>

#define OPENOSX_MAIN_DISPLAY ((CGDirectDisplayID)1)

/* Option key an app may pass to CGDisplayCopyAllDisplayModes to also list
 * duplicate low-resolution (HiDPI-scaled) modes. We report one mode and ignore
 * the option, but the symbol must exist for the binary to link. */
const CFStringRef kCGDisplayShowDuplicateLowResolutionModes =
    CFSTR("kCGDisplayShowDuplicateLowResolutionModes");

/* Read the framebuffer geometry once and cache it. PDGOPOpen touches IOKit, so
 * it is not something to repeat on every CGDisplayBounds call. */
static PDGOPFramebuffer g_fb;
static int g_fb_valid = -1;

static const PDGOPFramebuffer *fb(void)
{
    if (g_fb_valid < 0) {
        g_fb_valid = (PDGOPOpen(&g_fb) == 0) ? 1 : 0;
        if (!g_fb_valid) {          /* IOKit unavailable: a sane default */
            g_fb.width = 1920; g_fb.height = 1080; g_fb.bpp = 32;
        }
    }
    return &g_fb;
}

/* ---- display enumeration ---- */

CGDirectDisplayID CGMainDisplayID(void) { return OPENOSX_MAIN_DISPLAY; }

CGError CGGetOnlineDisplayList(uint32_t maxDisplays,
                               CGDirectDisplayID *online, uint32_t *count)
{
    if (count) *count = 1;
    if (online && maxDisplays >= 1) online[0] = OPENOSX_MAIN_DISPLAY;
    return kCGErrorSuccess;
}

CGError CGGetActiveDisplayList(uint32_t maxDisplays,
                               CGDirectDisplayID *active, uint32_t *count)
{ return CGGetOnlineDisplayList(maxDisplays, active, count); }

boolean_t CGDisplayIsMain(CGDirectDisplayID d) { return d == OPENOSX_MAIN_DISPLAY; }

CGDirectDisplayID CGDisplayMirrorsDisplay(CGDirectDisplayID d)
{ (void)d; return kCGNullDirectDisplay; }

CGRect CGDisplayBounds(CGDirectDisplayID d)
{
    (void)d;
    const PDGOPFramebuffer *f = fb();
    return (CGRect){ { 0, 0 }, { (CGFloat)f->width, (CGFloat)f->height } };
}

size_t CGDisplayPixelsWide(CGDirectDisplayID d) { (void)d; return fb()->width; }
size_t CGDisplayPixelsHigh(CGDirectDisplayID d) { (void)d; return fb()->height; }

/* Physical size in millimetres. Unknown on a VM framebuffer; report a plausible
 * ~96dpi so anything dividing pixels by size gets a sane number instead of a
 * divide-by-zero. */
CGSize CGDisplayScreenSize(CGDirectDisplayID d)
{
    (void)d;
    const PDGOPFramebuffer *f = fb();
    return (CGSize){ f->width * 25.4 / 96.0, f->height * 25.4 / 96.0 };
}

/* ---- display modes ---- */

struct openosx_display_mode {
    size_t width, height, pixelWidth, pixelHeight;
    double refresh;
    uint32_t ioflags;
    int32_t modeID;
};

/* CGDisplayModeRef is opaque; back it with the struct above. Kept out of the CF
 * runtime deliberately - apps only pass it by pointer to the getters and then
 * release it, and CFArray below uses no-op callbacks so it need not be a real
 * CFType. */
static CGDisplayModeRef make_mode(const PDGOPFramebuffer *f)
{
    struct openosx_display_mode *m = calloc(1, sizeof(*m));
    if (!m) return NULL;
    m->width = m->pixelWidth = f->width;
    m->height = m->pixelHeight = f->height;
    m->refresh = 60.0;
    m->ioflags = 0;
    m->modeID = 0;
    return (CGDisplayModeRef)m;
}

CGDisplayModeRef CGDisplayCopyDisplayMode(CGDirectDisplayID d)
{ (void)d; return make_mode(fb()); }

static const void *mode_retain(CFAllocatorRef a, const void *v) { (void)a; return v; }
static void mode_noop_release(CFAllocatorRef a, const void *v) { (void)a; (void)v; }

CFArrayRef CGDisplayCopyAllDisplayModes(CGDirectDisplayID d, CFDictionaryRef opts)
{
    (void)d; (void)opts;
    CGDisplayModeRef m = make_mode(fb());
    const void *vals[1] = { m };
    CFArrayCallBacks cb;
    memset(&cb, 0, sizeof(cb));
    cb.retain = mode_retain;
    cb.release = mode_noop_release;      /* array does not own the mode */
    return CFArrayCreate(kCFAllocatorDefault, vals, 1, &cb);
}

size_t CGDisplayModeGetWidth(CGDisplayModeRef m)
{ return m ? ((struct openosx_display_mode *)m)->width : 0; }
size_t CGDisplayModeGetHeight(CGDisplayModeRef m)
{ return m ? ((struct openosx_display_mode *)m)->height : 0; }
size_t CGDisplayModeGetPixelWidth(CGDisplayModeRef m)
{ return m ? ((struct openosx_display_mode *)m)->pixelWidth : 0; }
size_t CGDisplayModeGetPixelHeight(CGDisplayModeRef m)
{ return m ? ((struct openosx_display_mode *)m)->pixelHeight : 0; }
double CGDisplayModeGetRefreshRate(CGDisplayModeRef m)
{ return m ? ((struct openosx_display_mode *)m)->refresh : 0.0; }
uint32_t CGDisplayModeGetIOFlags(CGDisplayModeRef m)
{ return m ? ((struct openosx_display_mode *)m)->ioflags : 0; }
int32_t CGDisplayModeGetIODisplayModeID(CGDisplayModeRef m)
{ return m ? ((struct openosx_display_mode *)m)->modeID : 0; }

bool CGDisplayModeIsUsableForDesktopGUI(CGDisplayModeRef m) { (void)m; return true; }

CFStringRef CGDisplayModeCopyPixelEncoding(CGDisplayModeRef m)
{
    (void)m;                              /* always 32-bit on our framebuffer */
    return CFStringCreateWithCString(kCFAllocatorDefault,
                                     IO32BitDirectPixels, kCFStringEncodingASCII);
}

CGDisplayModeRef CGDisplayModeRetain(CGDisplayModeRef m) { return m; }
void CGDisplayModeRelease(CGDisplayModeRef m) { free((void *)m); }

CGError CGDisplaySetDisplayMode(CGDirectDisplayID d, CGDisplayModeRef m,
                                CFDictionaryRef opts)
{ (void)d; (void)m; (void)opts; return kCGErrorSuccess; }

/* ---- capture / release (fullscreen) ---- */

CGError CGDisplayCapture(CGDirectDisplayID d) { (void)d; return kCGErrorSuccess; }
CGError CGDisplayRelease(CGDirectDisplayID d) { (void)d; return kCGErrorSuccess; }
CGError CGCaptureAllDisplays(void) { return kCGErrorSuccess; }
CGError CGReleaseAllDisplays(void) { return kCGErrorSuccess; }
int32_t CGShieldingWindowLevel(void) { return 2147483630; /* CGShieldingWindowLevel() ~ INT_MAX */ }

/* ---- gamma fade: accept and succeed, doing nothing visible ---- */

CGError CGAcquireDisplayFadeReservation(CGDisplayReservationInterval sec,
                                        CGDisplayFadeReservationToken *token)
{ (void)sec; if (token) *token = 1; return kCGErrorSuccess; }

CGError CGReleaseDisplayFadeReservation(CGDisplayFadeReservationToken token)
{ (void)token; return kCGErrorSuccess; }

CGError CGDisplayFade(CGDisplayFadeReservationToken token, CGDisplayFadeInterval t,
                      CGDisplayBlendFraction a, CGDisplayBlendFraction b,
                      float r, float g, float bl, boolean_t wait)
{ (void)token;(void)t;(void)a;(void)b;(void)r;(void)g;(void)bl;(void)wait; return kCGErrorSuccess; }

/* ---- gamma tables: report identity, accept any set ---- */

CGError CGGetDisplayTransferByTable(CGDirectDisplayID d, uint32_t cap,
                                    CGGammaValue *red, CGGammaValue *green,
                                    CGGammaValue *blue, uint32_t *count)
{
    (void)d;
    for (uint32_t i = 0; i < cap; i++) {
        CGGammaValue v = (cap > 1) ? (CGGammaValue)i / (cap - 1) : 0;
        if (red) red[i] = v;
        if (green) green[i] = v;
        if (blue) blue[i] = v;
    }
    if (count) *count = cap;
    return kCGErrorSuccess;
}

CGError CGSetDisplayTransferByTable(CGDirectDisplayID d, uint32_t count,
                                    const CGGammaValue *red,
                                    const CGGammaValue *green,
                                    const CGGammaValue *blue)
{ (void)d;(void)count;(void)red;(void)green;(void)blue; return kCGErrorSuccess; }

/* ---- cursor ---- */

CGError CGWarpMouseCursorPosition(CGPoint p) { (void)p; return kCGErrorSuccess; }
CGError CGDisplayMoveCursorToPoint(CGDirectDisplayID d, CGPoint p)
{ (void)d; (void)p; return kCGErrorSuccess; }
CGError CGAssociateMouseAndMouseCursorPosition(boolean_t connected)
{ (void)connected; return kCGErrorSuccess; }

/* ---- legacy IOKit / OpenGL bridges ---- */

/* CGDisplayIOServicePort is long-deprecated; 0 (MACH_PORT_NULL) is the
 * documented "no port" answer and what callers fall back from. */
uint32_t CGDisplayIOServicePort(CGDirectDisplayID d) { (void)d; return 0; }

CGOpenGLDisplayMask CGDisplayIDToOpenGLDisplayMask(CGDirectDisplayID d)
{ (void)d; return 1; }
