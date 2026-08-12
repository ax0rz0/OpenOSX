/*
 * CGColor - an OpenOSX CoreGraphics opaque type, real CFType.
 *
 * Same CFRuntime registration as CGColorSpace / DADisk. A color is a retained
 * color space plus a malloc'd component array (colour channels followed by
 * alpha), which is all the public accessors expose.
 *
 * The constant colors (kCGColorBlack/White/Clear) and CGColorGetConstantColor
 * are the two CoreGraphics symbols The Powder Toy imports beyond the display
 * layer, so this file is what takes its CoreGraphics gap to zero.
 */
#include <CoreGraphics/CoreGraphics.h>
#include <CoreFoundation/CoreFoundation.h>
#include <CoreFoundation/CFRuntime.h>
#include <pthread.h>
#include <stdlib.h>
#include <string.h>

size_t _openosx_colorspace_components(CGColorSpaceRef cs);

struct openosx_color {
    CFRuntimeBase base;
    CGColorSpaceRef space;
    size_t count;           /* components including alpha */
    CGFloat *components;
};

static CFTypeID g_typeID = _kCFRuntimeNotATypeID;

static void color_dealloc(CFTypeRef o)
{
    struct openosx_color *c = (struct openosx_color *)o;
    if (c->space) CFRelease(c->space);
    free(c->components);
}

static Boolean color_equal(CFTypeRef a, CFTypeRef b)
{
    struct openosx_color *x = (struct openosx_color *)a, *y = (struct openosx_color *)b;
    if (x->count != y->count) return false;
    for (size_t i = 0; i < x->count; i++)
        if (x->components[i] != y->components[i]) return false;
    return true;
}

static CFStringRef color_desc(CFTypeRef o)
{
    return CFStringCreateWithFormat(CFGetAllocator(o), NULL, CFSTR("<CGColor %p>"), o);
}

static const CFRuntimeClass color_class = {
    0, "CGColor", NULL, NULL, color_dealloc, color_equal, NULL, NULL, color_desc
};

static void color_register(void) { g_typeID = _CFRuntimeRegisterClass(&color_class); }

CFTypeID CGColorGetTypeID(void)
{
    static pthread_once_t once = PTHREAD_ONCE_INIT;
    pthread_once(&once, color_register);
    return g_typeID;
}

static CGColorRef color_make(CGColorSpaceRef space, const CGFloat *comps, size_t count)
{
    CGColorGetTypeID();
    struct openosx_color *c = (struct openosx_color *)
        _CFRuntimeCreateInstance(kCFAllocatorDefault, g_typeID,
                                 sizeof(struct openosx_color) - sizeof(CFRuntimeBase),
                                 NULL);
    if (!c) return NULL;
    c->space = space ? (CGColorSpaceRef)CFRetain(space) : NULL;
    c->count = count;
    c->components = malloc(count * sizeof(CGFloat));
    if (!c->components) { if (c->space) CFRelease(c->space); return NULL; }
    if (comps) memcpy(c->components, comps, count * sizeof(CGFloat));
    else memset(c->components, 0, count * sizeof(CGFloat));
    return (CGColorRef)c;
}

CGColorRef CGColorCreate(CGColorSpaceRef space, const CGFloat *components)
{
    size_t n = _openosx_colorspace_components(space) + 1;  /* + alpha */
    return color_make(space, components, n);
}

CGColorRef CGColorCreateGenericRGB(CGFloat r, CGFloat g, CGFloat b, CGFloat a)
{
    CGColorSpaceRef s = CGColorSpaceCreateDeviceRGB();
    CGFloat comps[4] = { r, g, b, a };
    CGColorRef c = color_make(s, comps, 4);
    CFRelease(s);
    return c;
}

CGColorRef CGColorCreateGenericGray(CGFloat gray, CGFloat a)
{
    CGColorSpaceRef s = CGColorSpaceCreateDeviceGray();
    CGFloat comps[2] = { gray, a };
    CGColorRef c = color_make(s, comps, 2);
    CFRelease(s);
    return c;
}

const CGFloat *CGColorGetComponents(CGColorRef c)
{ return c ? ((struct openosx_color *)c)->components : NULL; }

size_t CGColorGetNumberOfComponents(CGColorRef c)
{ return c ? ((struct openosx_color *)c)->count : 0; }

CGColorSpaceRef CGColorGetColorSpace(CGColorRef c)
{ return c ? ((struct openosx_color *)c)->space : NULL; }

CGFloat CGColorGetAlpha(CGColorRef c)
{
    if (!c) return 0;
    struct openosx_color *cc = (struct openosx_color *)c;
    return cc->count ? cc->components[cc->count - 1] : 1;
}

bool CGColorEqualToColor(CGColorRef a, CGColorRef b)
{
    if (a == b) return true;
    if (!a || !b) return false;
    return color_equal(a, b);
}

CGColorRef CGColorRetain(CGColorRef c) { if (c) CFRetain(c); return c; }
void CGColorRelease(CGColorRef c) { if (c) CFRelease(c); }

/* ---- constant colors ---- */

const CFStringRef kCGColorBlack = CFSTR("kCGColorBlack");
const CFStringRef kCGColorWhite = CFSTR("kCGColorWhite");
const CFStringRef kCGColorClear = CFSTR("kCGColorClear");

/* Process-lifetime singletons, like Apple's constant colors. */
static CGColorRef g_black, g_white, g_clear;

static void build_constant_colors(void)
{
    g_black = CGColorCreateGenericGray(0.0, 1.0);
    g_white = CGColorCreateGenericGray(1.0, 1.0);
    g_clear = CGColorCreateGenericGray(0.0, 0.0);
}

CGColorRef CGColorGetConstantColor(CFStringRef name)
{
    static pthread_once_t once = PTHREAD_ONCE_INIT;
    if (name == NULL) return NULL;
    pthread_once(&once, build_constant_colors);
    if (CFEqual(name, kCGColorWhite)) return g_white;
    if (CFEqual(name, kCGColorClear)) return g_clear;
    return g_black;   /* kCGColorBlack and any unknown name */
}
