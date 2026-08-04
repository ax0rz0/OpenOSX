/*
 * Wire protocol between libPDSurface and whichever graphics driver is backing
 * it. Both the library and every implementing kext include this header.
 *
 * A driver opts in by accepting kPDSurfaceConnectType on IOServiceOpen and
 * implementing the selectors below; nothing else about the driver is assumed.
 * IOVirtIOGPU backs surfaces with virtio-gpu resources, a native driver would
 * back them with its own allocator, and callers cannot tell the difference.
 */
#ifndef _PD_SURFACE_PROTOCOL_H
#define _PD_SURFACE_PROTOCOL_H

#include <stdint.h>

/* IOServiceOpen type. Deliberately distinct from any driver's own user-client
 * types so a driver can serve surfaces alongside whatever else it does. */
enum { kPDSurfaceConnectType = 0x50445346 /* 'PDSF' */ };

enum {
    kPDSurfaceProtocolVersion = 1,
};

enum {
    kPDSurface_GetVersion = 0,  /* scalarOut[0] = protocol version            */
    kPDSurface_Create,          /* structIn: PDSurfaceCreateRequest
                                 *   scalarOut[0] = surface id
                                 *   scalarOut[1] = stride
                                 *   scalarOut[2] = mappable byte length      */
    kPDSurface_Destroy,         /* scalarIn[0] = surface id                   */
    kPDSurface_Lookup,          /* scalarIn[0] = surface id;
                                 *   structOut: PDSurfaceInfo                 */
    kPDSurface_SetScanout,      /* scalarIn[0] = surface id, 0 restores the
                                 *   driver's own framebuffer                 */
    kPDSurface_Flush,           /* structIn: PDSurfaceFlushRequest            */
    kPDSurface_MethodCount
};

/* Formats carry DRM fourcc values. They are a Linux-ism, but every consumer
 * on both sides of this protocol already speaks them, and inventing a parallel
 * set would only add a translation table at each boundary. */
enum {
    kPDSurfaceFormatARGB8888 = 0x34325241, /* 'AR24' */
    kPDSurfaceFormatXRGB8888 = 0x34325258, /* 'XR24' */
};

enum {
    kPDSurfaceUsageScanout  = 1u << 0, /* may be handed to kPDSurface_SetScanout */
    kPDSurfaceUsageCursor   = 1u << 1,
    kPDSurfaceUsageLinear   = 1u << 2, /* no tiling; required for CPU rendering  */
    kPDSurfaceUsageRender   = 1u << 3, /* GPU renders into it                    */
};

struct PDSurfaceCreateRequest {
    uint32_t width;
    uint32_t height;
    uint32_t format;
    uint32_t usage;
};

/* Publish CPU writes to a surface. Guest-backed storage is not visible to the
 * display until it is pushed, so anything drawn through the mapped base address
 * needs this before it appears. A zero-sized rectangle means the whole
 * surface. */
struct PDSurfaceFlushRequest {
    uint64_t surface_id;
    uint32_t x, y;
    uint32_t width, height;
};

struct PDSurfaceInfo {
    uint64_t surface_id;
    uint32_t width;
    uint32_t height;
    uint32_t format;
    uint32_t usage;
    uint32_t stride;
    uint64_t length;
};

#endif /* _PD_SURFACE_PROTOCOL_H */
