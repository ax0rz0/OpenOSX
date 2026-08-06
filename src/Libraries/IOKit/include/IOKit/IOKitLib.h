/*
 * OpenOSX's small userspace IOKit client surface.
 *
 * This intentionally avoids CoreFoundation so early userspace and simple X
 * drivers can talk to the kernel's real IOKit MIG interface before the full
 * framework stack exists.
 */
#ifndef _IOKIT_IOKITLIB_H
#define _IOKIT_IOKITLIB_H

#include <PDIOKitLib.h>

#endif /* _IOKIT_IOKITLIB_H */
