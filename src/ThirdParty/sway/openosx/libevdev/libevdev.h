/* SPDX-License-Identifier: MIT */
#ifndef OPENOSX_LIBEVDEV_H
#define OPENOSX_LIBEVDEV_H

#define EV_KEY 0x01
#define BTN_LEFT 0x110
#define BTN_RIGHT 0x111
#define BTN_MIDDLE 0x112
#define BTN_SIDE 0x113
#define BTN_EXTRA 0x114

int libevdev_event_code_from_name(unsigned int type, const char *name);
const char *libevdev_event_code_get_name(unsigned int type, unsigned int code);

#endif
