/*
 * Metal.framework - the minimum that lets a Metal-probing app fall back.
 *
 * OpenOSX has no Metal driver, and building one is a multi-year job. But an
 * app must not fail to LOAD merely because it links Metal: SDL2 (and so The
 * Powder Toy) references Metal, calls MTLCreateSystemDefaultDevice at runtime,
 * and - getting nil - falls back to OpenGL. So the whole compatibility fix is:
 *
 *   - MTLCreateSystemDefaultDevice / MTLCopyAllDevices return nil, which is
 *     exactly the "no Metal here" signal that triggers the GL path.
 *   - the five MTL*Descriptor classes SDL2 references exist as empty NSObject
 *     subclasses, so the __objc_classrefs resolve and the binary loads.
 *
 * Nothing here draws anything. It exists to make absence graceful.
 */
#import <Foundation/Foundation.h>

id MTLCreateSystemDefaultDevice(void) { return nil; }

NSArray *MTLCopyAllDevices(void) { return nil; }

/* Descriptor objects an app may alloc/init before discovering there is no
 * device to use them with. Empty is correct: without a device they are never
 * consumed. */
@interface MTLRenderPassDescriptor : NSObject @end
@implementation MTLRenderPassDescriptor @end

@interface MTLRenderPipelineDescriptor : NSObject @end
@implementation MTLRenderPipelineDescriptor @end

@interface MTLSamplerDescriptor : NSObject @end
@implementation MTLSamplerDescriptor @end

@interface MTLTextureDescriptor : NSObject @end
@implementation MTLTextureDescriptor @end

@interface MTLVertexDescriptor : NSObject @end
@implementation MTLVertexDescriptor @end
