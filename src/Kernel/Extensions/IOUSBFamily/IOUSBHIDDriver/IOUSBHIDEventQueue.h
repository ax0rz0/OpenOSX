#ifndef _PD_IOUSB_HID_EVENT_QUEUE_H
#define _PD_IOUSB_HID_EVENT_QUEUE_H

#include <IOKit/IOTypes.h>

struct USBHIDKbdEvent {
    UInt32 sequence;
    UInt8  usage;
    UInt8  down;
    UInt8  reserved[2];
};

struct USBHIDMouseEvent {
    UInt32 sequence;
    UInt8  mouseIndex;
    UInt8  buttons;
    SInt8  dx;
    SInt8  dy;
    SInt8  wheel;
    UInt8  reserved[3];
};

void USBHIDPublishKeyboardDevice(void);
void USBHIDPublishMouseDevice(void);
void USBHIDPushKeyboardEvent(UInt8 usage, bool down);

/* True while a userspace client holds /dev/usb_hid_kbd open. A compositor or
 * X server reading the raw HID queue is the sole owner of the keyboard for as
 * long as it runs, so the driver must stop feeding the same keys to the
 * console keyboard path as well. */
bool USBHIDKeyboardIsGrabbed(void);

void USBHIDPushMouseEvent(UInt8 mouseIndex, UInt8 buttons, SInt8 dx, SInt8 dy, SInt8 wheel);

#endif
