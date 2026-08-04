/* SPDX-License-Identifier: MIT */
#include <assert.h>
#include <stdlib.h>

#include <wlr/interfaces/wlr_output.h>
#include <wlr/backend/interface.h>
#include <wlr/util/log.h>

#include "backend.h"

struct wlr_puredarwin_backend *puredarwin_backend_from_backend(
		struct wlr_backend *wlr_backend) {
	assert(wlr_backend_is_puredarwin(wlr_backend));
	return wl_container_of(wlr_backend, (struct wlr_puredarwin_backend *)0, backend);
}

static bool backend_start(struct wlr_backend *wlr_backend) {
	struct wlr_puredarwin_backend *backend =
		puredarwin_backend_from_backend(wlr_backend);
	if (backend->output == NULL) {
		wlr_log(WLR_ERROR, "PureDarwin framebuffer output is unavailable");
		return false;
	}
	if (!puredarwin_input_init(backend)) {
		return false;
	}

	backend->started = true;
	wl_signal_emit_mutable(&backend->backend.events.new_output,
		&backend->output->wlr_output);
	return true;
}

static void backend_destroy(struct wlr_backend *wlr_backend) {
	struct wlr_puredarwin_backend *backend =
		puredarwin_backend_from_backend(wlr_backend);
	if (backend->event_loop_destroy.link.next != NULL) {
		wl_list_remove(&backend->event_loop_destroy.link);
	}
	if (backend->surface_device != NULL) {
		PDSurfaceSetScanout(backend->surface_device, NULL);
		PDSurfaceDeviceClose(backend->surface_device);
	}
	if (backend->gpu != NULL) {
		pd_virgl_close(backend->gpu);
	}
	puredarwin_input_finish(backend);
	wlr_backend_finish(wlr_backend);
	if (backend->output != NULL) {
		wlr_output_destroy(&backend->output->wlr_output);
	}
	free(backend);
}

static const struct wlr_backend_impl backend_impl = {
	.start = backend_start,
	.destroy = backend_destroy,
};

static void handle_event_loop_destroy(struct wl_listener *listener, void *data) {
	struct wlr_puredarwin_backend *backend =
		wl_container_of(listener, backend, event_loop_destroy);
	backend->event_loop_destroy.link.next = NULL;
	backend_destroy(&backend->backend);
}

struct wlr_backend *wlr_puredarwin_backend_create(struct wl_event_loop *loop) {
	struct wlr_puredarwin_backend *backend = calloc(1, sizeof(*backend));
	if (backend == NULL) {
		return NULL;
	}

	wlr_backend_init(&backend->backend, &backend_impl);
	backend->backend.buffer_caps = WLR_BUFFER_CAP_DATA_PTR | WLR_BUFFER_CAP_SHM;
	backend->event_loop = loop;
	backend->event_loop_destroy.notify = handle_event_loop_destroy;
	wl_event_loop_add_destroy_listener(loop, &backend->event_loop_destroy);
	/* Separate from the allocator's own connection: the backend needs one to
	 * drive the scanout even when buffers came from somewhere else. */
	if (PDSurfaceDeviceOpen(&backend->surface_device) != KERN_SUCCESS) {
		backend->surface_device = NULL;
	}
	backend->gpu = pd_virgl_open();
	if (backend->gpu == NULL) {
		wlr_log(WLR_DEBUG, "PureDarwin VirtIO GPU present interface unavailable");
	}

	backend->output = puredarwin_output_create(backend);
	if (backend->output == NULL) {
		wl_list_remove(&backend->event_loop_destroy.link);
		wlr_backend_finish(&backend->backend);
		free(backend);
		return NULL;
	}

	wlr_log(WLR_INFO, "PureDarwin IOGOP backend ready (%ux%u)",
		backend->output->framebuffer.width, backend->output->framebuffer.height);
	return &backend->backend;
}

bool wlr_backend_is_puredarwin(struct wlr_backend *backend) {
	return backend != NULL && backend->impl == &backend_impl;
}
