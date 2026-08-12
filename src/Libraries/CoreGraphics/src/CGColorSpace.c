/*
 * CGColorSpace - an OpenOSX CoreGraphics opaque type, real CFType.
 *
 * Registered against the shipped CoreFoundation exactly as
 * DiskArbitration/DADisk.c does (CFRuntimeBase first member,
 * _CFRuntimeRegisterClass, _CFRuntimeCreateInstance), so CFGetTypeID,
 * CFRetain/CFRelease and CFEqual work on a color space the same as on any
 * Apple CF object. That in-tree precedent is why this needs no new machinery.
 *
 * This is the bookkeeping layer only: a color space here knows its model and
 * component count. Colour management (ICC transforms) is a separate, later
 * job; device and generic spaces are all The Powder Toy and the first cut of
 * AppKit need.
 */
#include <CoreGraphics/CoreGraphics.h>
#include <CoreFoundation/CoreFoundation.h>
#include <CoreFoundation/CFRuntime.h>
#include <pthread.h>
#include <string.h>

struct openosx_colorspace {
    CFRuntimeBase base;
    CGColorSpaceModel model;
    size_t components;      /* excluding alpha */
    const char *name;
};

static CFTypeID g_typeID = _kCFRuntimeNotATypeID;

static void cs_dealloc(CFTypeRef o) { (void)o; }   /* no owned resources */

static CFStringRef cs_desc(CFTypeRef o)
{
    struct openosx_colorspace *cs = (struct openosx_colorspace *)o;
    return CFStringCreateWithFormat(CFGetAllocator(o), NULL,
                                    CFSTR("<CGColorSpace %p>{model=%d}"), o, (int)cs->model);
}

static const CFRuntimeClass cs_class = {
    0, "CGColorSpace", NULL, NULL, cs_dealloc, NULL, NULL, NULL, cs_desc
};

static void cs_register(void) { g_typeID = _CFRuntimeRegisterClass(&cs_class); }

CFTypeID CGColorSpaceGetTypeID(void)
{
    static pthread_once_t once = PTHREAD_ONCE_INIT;
    pthread_once(&once, cs_register);
    return g_typeID;
}

static CGColorSpaceRef cs_make(CGColorSpaceModel model, size_t comps, const char *name)
{
    CGColorSpaceGetTypeID();
    struct openosx_colorspace *cs = (struct openosx_colorspace *)
        _CFRuntimeCreateInstance(kCFAllocatorDefault, g_typeID,
                                 sizeof(struct openosx_colorspace) - sizeof(CFRuntimeBase),
                                 NULL);
    if (!cs) return NULL;
    cs->model = model;
    cs->components = comps;
    cs->name = name;
    return (CGColorSpaceRef)cs;
}

CGColorSpaceRef CGColorSpaceCreateDeviceRGB(void)
{ return cs_make(kCGColorSpaceModelRGB, 3, "DeviceRGB"); }

CGColorSpaceRef CGColorSpaceCreateDeviceGray(void)
{ return cs_make(kCGColorSpaceModelMonochrome, 1, "DeviceGray"); }

CGColorSpaceRef CGColorSpaceCreateDeviceCMYK(void)
{ return cs_make(kCGColorSpaceModelCMYK, 4, "DeviceCMYK"); }

CGColorSpaceRef CGColorSpaceCreateWithName(CFStringRef name)
{
    /* Every generic/sRGB name maps to a 3-component RGB space for now; the
     * distinctions only matter once real colour management exists. Monochrome
     * and CMYK names are honoured for component count. */
    if (name && CFStringFind(name, CFSTR("Gray"), 0).location != kCFNotFound)
        return CGColorSpaceCreateDeviceGray();
    if (name && CFStringFind(name, CFSTR("CMYK"), 0).location != kCFNotFound)
        return CGColorSpaceCreateDeviceCMYK();
    return CGColorSpaceCreateDeviceRGB();
}

CGColorSpaceModel CGColorSpaceGetModel(CGColorSpaceRef cs)
{
    return cs ? ((struct openosx_colorspace *)cs)->model : kCGColorSpaceModelUnknown;
}

size_t CGColorSpaceGetNumberOfComponents(CGColorSpaceRef cs)
{
    return cs ? ((struct openosx_colorspace *)cs)->components : 0;
}

CGColorSpaceRef CGColorSpaceRetain(CGColorSpaceRef cs)
{ if (cs) CFRetain(cs); return cs; }

void CGColorSpaceRelease(CGColorSpaceRef cs)
{ if (cs) CFRelease(cs); }

/* Internal: component count for CGColor, without re-querying via the public
 * accessor from another TU. */
size_t _openosx_colorspace_components(CGColorSpaceRef cs)
{ return CGColorSpaceGetNumberOfComponents(cs); }
