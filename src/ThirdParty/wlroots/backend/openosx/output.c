/* SPDX-License-Identifier: MIT */
#include <assert.h>
#include <inttypes.h>
#include <stdlib.h>
#include <string.h>

#include <drm_fourcc.h>
#include <wlr/interfaces/wlr_output.h>
#include <wlr/interfaces/wlr_buffer.h>
#include <wlr/render/drm_format_set.h>
#include <wlr/util/log.h>

#include "backend.h"
#include "types/wlr_output.h"

static const uint32_t supported_state =
	WLR_OUTPUT_STATE_BACKEND_OPTIONAL |
	WLR_OUTPUT_STATE_BUFFER |
	WLR_OUTPUT_STATE_ENABLED |
	WLR_OUTPUT_STATE_MODE;

struct wlr_puredarwin_output *puredarwin_output_from_output(
		struct wlr_output *wlr_output) {
	assert(wlr_output_is_puredarwin(wlr_output));
	return wl_container_of(wlr_output,
		(struct wlr_puredarwin_output *)0, wlr_output);
}

static bool output_test(struct wlr_output *wlr_output,
		const struct wlr_output_state *state) {
	uint32_t unsupported = state->committed & ~supported_state;
	if (unsupported != 0) {
		wlr_log(WLR_DEBUG, "OpenOSX output rejected state 0x%" PRIx32,
			unsupported);
		return false;
	}
	if ((state->committed & WLR_OUTPUT_STATE_MODE) &&
		state->mode_type != WLR_OUTPUT_STATE_MODE_CUSTOM) {
		return false;
	}
	return true;
}

static bool output_commit(struct wlr_output *wlr_output,
		const struct wlr_output_state *state) {
	struct wlr_puredarwin_output *output =
		puredarwin_output_from_output(wlr_output);
	if (!output_test(wlr_output, state)) {
		return false;
	}

	if (state->committed & WLR_OUTPUT_STATE_BUFFER && state->buffer != NULL) {
		void *data = NULL;
		uint32_t format = 0;
		size_t stride = 0;
		if (!wlr_buffer_begin_data_ptr_access(state->buffer,
			WLR_BUFFER_DATA_PTR_ACCESS_READ, &data, &format, &stride)) {
			wlr_log(WLR_ERROR, "OpenOSX output could not access buffer data");
			return false;
		}

		if (data == NULL || stride == 0 || output->framebuffer.address == 0) {
			wlr_buffer_end_data_ptr_access(state->buffer);
			return false;
		}

		/* Widest run this framebuffer and this buffer can both address,
		 * in whole pixels. */
		int max_width = (int)output->framebuffer.width;
		if (state->buffer->width < max_width) {
			max_width = state->buffer->width;
		}
		if ((int)(stride / 4) < max_width) {
			max_width = (int)(stride / 4);
		}
		if ((int)(output->framebuffer.stride / 4) < max_width) {
			max_width = (int)(output->framebuffer.stride / 4);
		}
		int max_height = (int)output->framebuffer.height;
		if (state->buffer->height < max_height) {
			max_height = state->buffer->height;
		}

		/* An allocator-provided buffer is already the display's storage, so
		 * there is nothing to copy: publish the writes and point the scanout
		 * at it. */
		PDSurfaceRef surface = puredarwin_buffer_get_surface(state->buffer);
		if (surface != NULL) {
			wlr_buffer_end_data_ptr_access(state->buffer);
			PDSurfaceFlush(surface, 0, 0, 0, 0);
			if (surface != output->scanout_surface) {
				PDSurfaceDeviceRef device =
					puredarwin_buffer_get_device(state->buffer);
				kern_return_t kr = PDSurfaceSetScanout(device, surface);
				if (kr != KERN_SUCCESS) {
					wlr_log(WLR_ERROR, "PDSurfaceSetScanout failed: 0x%x", kr);
					return false;
				}
				output->scanout_surface = surface;
			}

			struct wlr_output_event_present present = {
				.commit_seq = wlr_output->commit_seq + 1,
				.presented = true,
			};
			wlr_output_send_present(wlr_output, &present);

			if (output_pending_enabled(wlr_output, state) &&
					output->frame_timer != NULL) {
				wl_event_source_timer_update(output->frame_timer, 16);
			}
			return true;
		}

		/* Otherwise the framebuffer is guest memory and only what is handed to
		 * pd_virgl_present() is transferred to the host, so the copy and the
		 * present are two separate repair-what-changed caches with no way to
		 * resync. A region either of them misses once stays wrong until
		 * something happens to touch those pixels again. Copying and presenting
		 * the whole frame costs a memcpy and a full transfer but cannot drift. */
		for (int y = 0; y < max_height; y++) {
			memcpy((uint8_t *)(uintptr_t)output->framebuffer.address +
				(size_t)y * output->framebuffer.stride,
				(uint8_t *)data + (size_t)y * stride,
				(size_t)max_width * 4);
		}
		wlr_buffer_end_data_ptr_access(state->buffer);

		if (output->backend->gpu != NULL) {
			int present_result = pd_virgl_present(output->backend->gpu, 0, 0,
				max_width, max_height);
			if (present_result != 0) {
				wlr_log(WLR_ERROR, "OpenOSX GPU present failed: %d",
					present_result);
			}
		}

		struct wlr_output_event_present present = {
			.commit_seq = wlr_output->commit_seq + 1,
			.presented = true,
		};
		wlr_output_send_present(wlr_output, &present);
	}

	/* There is no hardware vblank event, so this timer is the only frame
	 * source - it has to be re-armed after every commit, presented or not,
	 * exactly like wlroots' headless backend. Gating it on whether anything
	 * was shown stops the clock, and then nothing that relies on a frame
	 * callback (a status bar repainting on its own schedule, say) ever runs
	 * again. */
	if (output_pending_enabled(wlr_output, state)) {
		if (output->frame_timer != NULL) {
			wl_event_source_timer_update(output->frame_timer, 16);
		}
	}
	return true;
}

/* virtio-gpu cursors are a fixed 64x64 BGRA plane. */
#define OPENOSX_CURSOR_EDGE 64

static bool output_set_cursor(struct wlr_output *wlr_output,
		struct wlr_buffer *buffer, int hotspot_x, int hotspot_y) {
	struct wlr_puredarwin_output *output =
		puredarwin_output_from_output(wlr_output);
	if (output->backend->gpu == NULL) {
		return false;
	}

	if (buffer == NULL) {
		return pd_virgl_set_cursor(output->backend->gpu, NULL, 0, 0, 0, 0) == 0;
	}
	if (buffer->width > OPENOSX_CURSOR_EDGE ||
			buffer->height > OPENOSX_CURSOR_EDGE) {
		return false;
	}

	void *data = NULL;
	uint32_t format = 0;
	size_t stride = 0;
	if (!wlr_buffer_begin_data_ptr_access(buffer,
			WLR_BUFFER_DATA_PTR_ACCESS_READ, &data, &format, &stride)) {
		return false;
	}

	/* The shim wants tightly packed rows; the buffer rarely is one. */
	uint8_t packed[OPENOSX_CURSOR_EDGE * OPENOSX_CURSOR_EDGE * 4] = {0};
	size_t row_bytes = (size_t)buffer->width * 4;
	for (int y = 0; y < buffer->height; y++) {
		memcpy(packed + (size_t)y * row_bytes,
			(uint8_t *)data + (size_t)y * stride, row_bytes);
	}
	wlr_buffer_end_data_ptr_access(buffer);

	return pd_virgl_set_cursor(output->backend->gpu, packed, buffer->width,
		buffer->height, hotspot_x, hotspot_y) == 0;
}

static bool output_move_cursor(struct wlr_output *wlr_output, int x, int y) {
	struct wlr_puredarwin_output *output =
		puredarwin_output_from_output(wlr_output);
	if (output->backend->gpu == NULL || x < 0 || y < 0) {
		return false;
	}
	return pd_virgl_move_cursor(output->backend->gpu, x, y) == 0;
}

/* Without these wlroots picks its own cursor size and format, hands us a
 * buffer set_cursor has to reject, and quietly falls back to a software
 * cursor - which is what the plane is here to avoid. */
static const struct wlr_drm_format_set *output_get_cursor_formats(
		struct wlr_output *wlr_output, uint32_t buffer_caps) {
	static struct wlr_drm_format_set formats = {0};
	static bool initialized = false;

	if (!(buffer_caps & WLR_BUFFER_CAP_DATA_PTR)) {
		return NULL;
	}
	if (!initialized) {
		wlr_drm_format_set_add(&formats, DRM_FORMAT_ARGB8888,
			DRM_FORMAT_MOD_INVALID);
		initialized = true;
	}
	return &formats;
}

static const struct wlr_output_cursor_size *output_get_cursor_sizes(
		struct wlr_output *wlr_output, size_t *len) {
	static const struct wlr_output_cursor_size sizes[] = {
		{ .width = OPENOSX_CURSOR_EDGE, .height = OPENOSX_CURSOR_EDGE },
	};
	*len = sizeof(sizes) / sizeof(sizes[0]);
	return sizes;
}

static void output_destroy(struct wlr_output *wlr_output) {
	struct wlr_puredarwin_output *output =
		puredarwin_output_from_output(wlr_output);
	if (output->frame_timer != NULL) {
		wl_event_source_remove(output->frame_timer);
	}
	PDGOPClose(&output->framebuffer);
	wlr_output_finish(wlr_output);
	output->backend->output = NULL;
	free(output);
}

static const struct wlr_output_impl output_impl = {
	.destroy = output_destroy,
	.test = output_test,
	.commit = output_commit,
	.set_cursor = output_set_cursor,
	.move_cursor = output_move_cursor,
	.get_cursor_formats = output_get_cursor_formats,
	.get_cursor_sizes = output_get_cursor_sizes,
};

static int signal_frame(void *data) {
	struct wlr_puredarwin_output *output = data;
	wlr_output_send_frame(&output->wlr_output);
	return 0;
}

struct wlr_puredarwin_output *puredarwin_output_create(
		struct wlr_puredarwin_backend *backend) {
	struct wlr_puredarwin_output *output = calloc(1, sizeof(*output));
	if (output == NULL || PDGOPOpen(&output->framebuffer) != KERN_SUCCESS) {
		free(output);
		wlr_log(WLR_ERROR, "PDGOPOpen failed at %s", PDGOPLastErrorStage());
		return NULL;
	}

	struct wlr_output_state state;
	wlr_output_state_init(&state);
	wlr_output_state_set_custom_mode(&state, output->framebuffer.width,
		output->framebuffer.height, 60000);
	wlr_output_init(&output->wlr_output, &backend->backend, &output_impl,
		backend->event_loop, &state);
	wlr_output_state_finish(&state);

	output->backend = backend;
	wlr_output_set_name(&output->wlr_output, "OPENOSX-0");
	wlr_output_set_description(&output->wlr_output, "OpenOSX IOGOP framebuffer");
	output->frame_timer = wl_event_loop_add_timer(backend->event_loop,
		signal_frame, output);
	if (output->frame_timer == NULL) {
		wlr_output_destroy(&output->wlr_output);
		return NULL;
	}
	return output;
}

bool wlr_output_is_puredarwin(struct wlr_output *output) {
	return output != NULL && output->impl == &output_impl;
}
