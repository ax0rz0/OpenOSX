/* SPDX-License-Identifier: MIT */
#ifndef WLR_BACKEND_PUREDARWIN_INTERNAL_H
#define WLR_BACKEND_PUREDARWIN_INTERNAL_H

#include <stdbool.h>
#include <wayland-server-core.h>
#include <wlr/backend/openosx.h>

#include <PDGOP.h>
#include <PDSurface.h>
#include <pd_virgl_shim.h>
#include <wlr/interfaces/wlr_keyboard.h>
#include <wlr/interfaces/wlr_pointer.h>

struct wlr_puredarwin_output;

struct wlr_puredarwin_backend {
	struct wlr_backend backend;
	struct wl_event_loop *event_loop;
	struct wlr_puredarwin_output *output;
	struct wlr_keyboard keyboard;
	struct wlr_pointer pointer;
	struct wl_event_source *keyboard_source;
	struct wl_event_source *mouse_source;
	struct wl_event_source *input_retry_timer;
	int keyboard_fd;
	int mouse_fd;
	uint8_t mouse_buttons;
	bool input_ready;
	bool keyboard_emitted;
	bool pointer_emitted;
	pd_virgl_conn *gpu;
	PDSurfaceDeviceRef surface_device;
	struct wl_listener event_loop_destroy;
	bool started;
};

struct wlr_puredarwin_output {
	struct wlr_output wlr_output;
	struct wlr_puredarwin_backend *backend;
	PDGOPFramebuffer framebuffer;
	struct wl_event_source *frame_timer;
	PDSurfaceRef scanout_surface;
};

struct wlr_puredarwin_backend *puredarwin_backend_from_backend(
	struct wlr_backend *backend);
struct wlr_puredarwin_output *puredarwin_output_from_output(
	struct wlr_output *output);
struct wlr_puredarwin_output *puredarwin_output_create(
	struct wlr_puredarwin_backend *backend);
/* The surface behind an allocator-provided buffer, or NULL when the buffer
 * came from somewhere else and has to be copied. */
PDSurfaceRef puredarwin_buffer_get_surface(struct wlr_buffer *buffer);
PDSurfaceDeviceRef puredarwin_buffer_get_device(struct wlr_buffer *buffer);

bool puredarwin_input_init(struct wlr_puredarwin_backend *backend);
void puredarwin_input_finish(struct wlr_puredarwin_backend *backend);

#endif
