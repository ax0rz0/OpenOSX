/*
 * Shareable graphics buffers, independent of which driver provides them.
 */
#ifndef _PD_SURFACE_H
#define _PD_SURFACE_H

#include <mach/kern_return.h>
#include <stdbool.h>
#include <stdint.h>

#include <PDSurfaceProtocol.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct PDSurfaceDevice *PDSurfaceDeviceRef;
typedef struct PDSurface *PDSurfaceRef;

typedef struct {
    uint32_t width;
    uint32_t height;
    uint32_t format;  /* kPDSurfaceFormat*  */
    uint32_t usage;   /* kPDSurfaceUsage* bits */
} PDSurfaceDescriptor;

/* Opens the first driver that answers the surface protocol. */
kern_return_t PDSurfaceDeviceOpen(PDSurfaceDeviceRef *outDevice);
void          PDSurfaceDeviceClose(PDSurfaceDeviceRef device);
const char   *PDSurfaceDeviceGetName(PDSurfaceDeviceRef device);

kern_return_t PDSurfaceCreate(PDSurfaceDeviceRef device,
                              const PDSurfaceDescriptor *descriptor,
                              PDSurfaceRef *outSurface);
void          PDSurfaceRelease(PDSurfaceRef surface);

uint32_t PDSurfaceGetWidth(PDSurfaceRef surface);
uint32_t PDSurfaceGetHeight(PDSurfaceRef surface);
uint32_t PDSurfaceGetFormat(PDSurfaceRef surface);
uint32_t PDSurfaceGetStride(PDSurfaceRef surface);

/* Mapped guest memory, for surfaces created with kPDSurfaceUsageLinear.
 * NULL when the driver keeps the surface where the CPU cannot reach it. */
void *PDSurfaceGetBaseAddress(PDSurfaceRef surface);

/* Identifier another process can hand to PDSurfaceLookup. This is what stands
 * in for a dmabuf file descriptor: Darwin has no such thing, and an opaque id
 * over the driver's own registry costs less than pretending otherwise. */
uint64_t      PDSurfaceGetID(PDSurfaceRef surface);
kern_return_t PDSurfaceLookup(PDSurfaceDeviceRef device, uint64_t surfaceID,
                              PDSurfaceRef *outSurface);

/* Publish CPU writes made through the base address. Passing a zero-sized
 * rectangle flushes the whole surface. */
kern_return_t PDSurfaceFlush(PDSurfaceRef surface, uint32_t x, uint32_t y,
                             uint32_t width, uint32_t height);

/* Display the surface directly. Passing NULL restores the driver's own
 * framebuffer. Only valid for surfaces created with kPDSurfaceUsageScanout. */
kern_return_t PDSurfaceSetScanout(PDSurfaceDeviceRef device,
                                  PDSurfaceRef surface);

#ifdef __cplusplus
}
#endif

#endif /* _PD_SURFACE_H */
