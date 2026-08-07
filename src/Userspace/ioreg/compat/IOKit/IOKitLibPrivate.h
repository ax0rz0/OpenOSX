/*
 * Minimal stand-in for the real IOKitUser/IOKitLibPrivate.h: ioreg.c only
 * pulls _IOObjectGetClass/_IOObjectConformsTo (kIOClassNameOverrideNone
 * itself lives in IOKitKeysPrivate.h, already real and in-tree) from this
 * header - the real file also declares IONotificationPort/
 * IODispatchCalloutFromCFMessage/iokit_user_client_trap and friends, none
 * of which ioreg.c uses, so they're left out rather than dragging in
 * libdispatch/CFRunLoop types this build doesn't need here.
 */
#ifndef __IOKIT_IOKITLIBPRIVATE_H
#define __IOKIT_IOKITLIBPRIVATE_H

#include <mach/mach.h>

#if defined(__cplusplus)
extern "C" {
#endif

kern_return_t
_IOObjectGetClass(io_object_t object, uint64_t options, io_name_t className);

boolean_t
_IOObjectConformsTo(io_object_t object, const io_name_t className,
    uint64_t options);

kern_return_t
IOServiceGetBusyStateAndTime(io_service_t service, uint64_t *state,
    uint32_t *busy_state, uint64_t *accumulated_busy_time);

enum {
	kIOServiceInactiveState     = 0x00000001,
	kIOServiceRegisteredState   = 0x00000002,
	kIOServiceMatchedState      = 0x00000004,
	kIOServiceFirstPublishState = 0x00000008,
	kIOServiceFirstMatchState   = 0x00000010
};

#if defined(__cplusplus)
}
#endif

#endif /* __IOKIT_IOKITLIBPRIVATE_H */
