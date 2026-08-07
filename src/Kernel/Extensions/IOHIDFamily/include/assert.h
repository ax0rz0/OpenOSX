/* Kernel build shim: HIDDriverKit sources include <assert.h> (a DriverKit
 * userspace header). In the kext build, map it to IOKit's kernel assert. */
#ifndef _PD_HID_ASSERT_SHIM_H
#define _PD_HID_ASSERT_SHIM_H
#include <IOKit/assert.h>
#endif
