/* SPDX-License-Identifier: MIT */
/*
 * Buffers that are already scanout memory.
 *
 * The shm allocator hands back ordinary anonymous memory, which then has to be
 * copied into the display's own storage every frame. Allocating from PDSurface
 * instead means the renderer draws straight into something the driver can put
 * on screen, so presenting is a flush and a flip rather than a full-screen
 * memcpy.
 */
#include <assert.h>
#include <stdlib.h>

#include <wlr/interfaces/wlr_buffer.h>
#include <wlr/render/allocator.h>
#include <wlr/render/drm_format_set.h>
#include <wlr/util/log.h>

#include <PDSurface.h>

#include "backend.h"

struct wlr_puredarwin_allocator {
	struct wlr_allocator base;
	PDSurfaceDeviceRef device;
};

struct wlr_puredarwin_buffer {
	struct wlr_buffer base;
	PDSurfaceDeviceRef device;
	PDSurfaceRef surface;
	uint32_t format;
};

static const struct wlr_buffer_impl buffer_impl;

static struct wlr_puredarwin_buffer *buffer_from_buffer(
		struct wlr_buffer *wlr_buffer) {
	assert(wlr_buffer->impl == &buffer_impl);
	struct wlr_puredarwin_buffer *buffer =
		wl_container_of(wlr_buffer, buffer, base);
	return buffer;
}

PDSurfaceRef puredarwin_buffer_get_surface(struct wlr_buffer *wlr_buffer) {
	if (wlr_buffer == NULL || wlr_buffer->impl != &buffer_impl) {
		return NULL;
	}
	return buffer_from_buffer(wlr_buffer)->surface;
}

/* Surfaces live in the registry of the connection that created them, so
 * scanning one out has to go through that same connection rather than any
 * other handle on the driver. */
PDSurfaceDeviceRef puredarwin_buffer_get_device(struct wlr_buffer *wlr_buffer) {
	if (wlr_buffer == NULL || wlr_buffer->impl != &buffer_impl) {
		return NULL;
	}
	return buffer_from_buffer(wlr_buffer)->device;
}

static void buffer_destroy(struct wlr_buffer *wlr_buffer) {
	struct wlr_puredarwin_buffer *buffer = buffer_from_buffer(wlr_buffer);
	wlr_buffer_finish(wlr_buffer);
	PDSurfaceRelease(buffer->surface);
	free(buffer);
}

static bool buffer_begin_data_ptr_access(struct wlr_buffer *wlr_buffer,
		uint32_t flags, void **data, uint32_t *format, size_t *stride) {
	struct wlr_puredarwin_buffer *buffer = buffer_from_buffer(wlr_buffer);
	void *base = PDSurfaceGetBaseAddress(buffer->surface);
	if (base == NULL) {
		return false;
	}
	*data = base;
	*format = buffer->format;
	*stride = PDSurfaceGetStride(buffer->surface);
	return true;
}

static void buffer_end_data_ptr_access(struct wlr_buffer *wlr_buffer) {
	/* Writes are published by PDSurfaceFlush() at commit, not here: this is
	 * called once per render pass and flushing each time would push the same
	 * frame to the host repeatedly. */
}

static const struct wlr_buffer_impl buffer_impl = {
	.destroy = buffer_destroy,
	.begin_data_ptr_access = buffer_begin_data_ptr_access,
	.end_data_ptr_access = buffer_end_data_ptr_access,
};

static struct wlr_buffer *allocator_create_buffer(
		struct wlr_allocator *wlr_allocator, int width, int height,
		const struct wlr_drm_format *format) {
	struct wlr_puredarwin_allocator *allocator =
		wl_container_of(wlr_allocator, allocator, base);

	/* PDSurface's format constants are DRM fourccs, so no translation. */
	if (format->format != kPDSurfaceFormatARGB8888 &&
			format->format != kPDSurfaceFormatXRGB8888) {
		wlr_log(WLR_DEBUG, "OpenOSX allocator rejected format 0x%" PRIX32,
			format->format);
		return NULL;
	}

	struct wlr_puredarwin_buffer *buffer = calloc(1, sizeof(*buffer));
	if (buffer == NULL) {
		return NULL;
	}

	PDSurfaceDescriptor descriptor = {
		.width = (uint32_t)width,
		.height = (uint32_t)height,
		.format = format->format,
		.usage = kPDSurfaceUsageScanout | kPDSurfaceUsageLinear,
	};
	if (PDSurfaceCreate(allocator->device, &descriptor,
			&buffer->surface) != KERN_SUCCESS) {
		free(buffer);
		return NULL;
	}
	if (PDSurfaceGetBaseAddress(buffer->surface) == NULL) {
		PDSurfaceRelease(buffer->surface);
		free(buffer);
		return NULL;
	}

	buffer->device = allocator->device;
	buffer->format = format->format;
	wlr_buffer_init(&buffer->base, &buffer_impl, width, height);
	return &buffer->base;
}

static void allocator_destroy(struct wlr_allocator *wlr_allocator) {
	struct wlr_puredarwin_allocator *allocator =
		wl_container_of(wlr_allocator, allocator, base);
	PDSurfaceDeviceClose(allocator->device);
	free(allocator);
}

static const struct wlr_allocator_interface allocator_impl = {
	.destroy = allocator_destroy,
	.create_buffer = allocator_create_buffer,
};

struct wlr_allocator *wlr_puredarwin_allocator_create(void) {
	/* Declining here falls the compositor back to shm buffers and the copying
	 * present path, which is the reference for telling a scanout-flip problem
	 * apart from a compositing one. */
	if (getenv("WLR_PUREDARWIN_NO_SURFACE_ALLOC") != NULL) {
		wlr_log(WLR_INFO, "OpenOSX allocator disabled by environment");
		return NULL;
	}

	struct wlr_puredarwin_allocator *allocator = calloc(1, sizeof(*allocator));
	if (allocator == NULL) {
		return NULL;
	}
	if (PDSurfaceDeviceOpen(&allocator->device) != KERN_SUCCESS) {
		wlr_log(WLR_DEBUG, "No PDSurface provider; falling back");
		free(allocator);
		return NULL;
	}

	wlr_allocator_init(&allocator->base, &allocator_impl,
		WLR_BUFFER_CAP_DATA_PTR);
	wlr_log(WLR_INFO, "Created OpenOSX allocator on %s",
		PDSurfaceDeviceGetName(allocator->device));
	return &allocator->base;
}
