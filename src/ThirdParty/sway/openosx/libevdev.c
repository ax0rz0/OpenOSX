/* SPDX-License-Identifier: MIT */
#include <string.h>

#include <libevdev/libevdev.h>

struct button_name {
	const char *name;
	int code;
};

static const struct button_name buttons[] = {
	{ "BTN_LEFT", BTN_LEFT },
	{ "BTN_RIGHT", BTN_RIGHT },
	{ "BTN_MIDDLE", BTN_MIDDLE },
	{ "BTN_SIDE", BTN_SIDE },
	{ "BTN_EXTRA", BTN_EXTRA },
};

int libevdev_event_code_from_name(unsigned int type, const char *name) {
	if (type != EV_KEY || name == NULL) {
		return -1;
	}
	for (unsigned int i = 0; i < sizeof(buttons) / sizeof(buttons[0]); i++) {
		if (strcmp(buttons[i].name, name) == 0) {
			return buttons[i].code;
		}
	}
	return -1;
}

const char *libevdev_event_code_get_name(unsigned int type, unsigned int code) {
	if (type != EV_KEY) {
		return NULL;
	}
	for (unsigned int i = 0; i < sizeof(buttons) / sizeof(buttons[0]); i++) {
		if ((unsigned int)buttons[i].code == code) {
			return buttons[i].name;
		}
	}
	return NULL;
}
