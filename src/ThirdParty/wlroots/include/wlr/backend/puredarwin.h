/* SPDX-License-Identifier: MIT */
/*
 * This an unstable interface of wlroots. No guarantees are made regarding the
 * future consistency of this API.
 */
#ifndef WLR_USE_UNSTABLE
#error "Add -DWLR_USE_UNSTABLE to enable unstable wlroots features"
#endif

#ifndef WLR_BACKEND_PUREDARWIN_H
#define WLR_BACKEND_PUREDARWIN_H

#include <wlr/backend.h>

struct wlr_backend *wlr_puredarwin_backend_create(struct wl_event_loop *loop);
bool wlr_backend_is_puredarwin(struct wlr_backend *backend);
bool wlr_output_is_puredarwin(struct wlr_output *output);

/* Allocates buffers that are already scanout memory, so presenting them costs
 * a flush and a flip instead of a copy. NULL when no driver provides them. */
struct wlr_allocator *wlr_puredarwin_allocator_create(void);

#endif
