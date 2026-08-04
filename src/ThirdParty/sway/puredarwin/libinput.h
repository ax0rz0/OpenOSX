/* SPDX-License-Identifier: MIT */
/* PureDarwin build-time definitions used when wlroots has no libinput backend. */
#ifndef PUREDARWIN_LIBINPUT_H
#define PUREDARWIN_LIBINPUT_H

enum libinput_config_accel_profile {
	LIBINPUT_CONFIG_ACCEL_PROFILE_ADAPTIVE = 0,
	LIBINPUT_CONFIG_ACCEL_PROFILE_FLAT = 1,
};

enum libinput_config_click_method {
	LIBINPUT_CONFIG_CLICK_METHOD_NONE = 0,
	LIBINPUT_CONFIG_CLICK_METHOD_BUTTON_AREAS = 1,
	LIBINPUT_CONFIG_CLICK_METHOD_CLICKFINGER = 2,
};

enum libinput_config_drag_state {
	LIBINPUT_CONFIG_DRAG_DISABLED = 0,
	LIBINPUT_CONFIG_DRAG_ENABLED = 1,
};

enum libinput_config_drag_lock_state {
	LIBINPUT_CONFIG_DRAG_LOCK_DISABLED = 0,
	LIBINPUT_CONFIG_DRAG_LOCK_ENABLED = 1,
};

enum libinput_config_dwt_state {
	LIBINPUT_CONFIG_DWT_DISABLED = 0,
	LIBINPUT_CONFIG_DWT_ENABLED = 1,
};

enum libinput_config_dwtp_state {
	LIBINPUT_CONFIG_DWTP_DISABLED = 0,
	LIBINPUT_CONFIG_DWTP_ENABLED = 1,
};

enum libinput_config_tap_state {
	LIBINPUT_CONFIG_TAP_DISABLED = 0,
	LIBINPUT_CONFIG_TAP_ENABLED = 1,
};

enum libinput_config_tap_button_map {
	LIBINPUT_CONFIG_TAP_MAP_LRM = 0,
	LIBINPUT_CONFIG_TAP_MAP_LMR = 1,
};

enum libinput_config_middle_emulation_state {
	LIBINPUT_CONFIG_MIDDLE_EMULATION_DISABLED = 0,
	LIBINPUT_CONFIG_MIDDLE_EMULATION_ENABLED = 1,
};

enum libinput_config_scroll_method {
	LIBINPUT_CONFIG_SCROLL_NO_SCROLL = 0,
	LIBINPUT_CONFIG_SCROLL_2FG = 1,
	LIBINPUT_CONFIG_SCROLL_EDGE = 2,
	LIBINPUT_CONFIG_SCROLL_ON_BUTTON_DOWN = 3,
};

enum libinput_config_scroll_button_lock_state {
	LIBINPUT_CONFIG_SCROLL_BUTTON_LOCK_DISABLED = 0,
	LIBINPUT_CONFIG_SCROLL_BUTTON_LOCK_ENABLED = 1,
};

enum libinput_config_send_events_mode {
	LIBINPUT_CONFIG_SEND_EVENTS_ENABLED = 0,
	LIBINPUT_CONFIG_SEND_EVENTS_DISABLED = 1,
	LIBINPUT_CONFIG_SEND_EVENTS_DISABLED_ON_EXTERNAL_MOUSE = 2,
};

enum libinput_config_clickfinger_button_map {
	LIBINPUT_CONFIG_CLICKFINGER_MAP_LRM = 0,
	LIBINPUT_CONFIG_CLICKFINGER_MAP_LMR = 1,
};

#endif
