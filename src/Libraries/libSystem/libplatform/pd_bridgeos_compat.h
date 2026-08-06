/*
 * pd_bridgeos_compat.h (force-included into libplatform_static)
 *
 * os/lock_private.h and friends use bridgeos(x) in __API_AVAILABLE lists, but
 * neither the MacOSX11.3 SDK nor OpenOSX's AvailabilityInternal.h define the
 * bridgeos platform-macro family, leaving an undefined token -> "expected ','".
 * Supply the missing macros (clang knows the "bridgeos" availability platform).
 */
#ifndef OPENOSX_LIBPLATFORM_BRIDGEOS_COMPAT_H
#define OPENOSX_LIBPLATFORM_BRIDGEOS_COMPAT_H

#include <Availability.h>
/* os/atomic_private_impl.h (pulled via os/lock.h) uses memory_order / the C11
 * atomics enum without including <stdatomic.h> itself. Provide it up front. */
#include <stdatomic.h>

#ifndef __API_AVAILABLE_PLATFORM_bridgeos
 #define __API_AVAILABLE_PLATFORM_bridgeos(x) bridgeos,introduced=x
#endif
#ifndef __API_DEPRECATED_PLATFORM_bridgeos
 #define __API_DEPRECATED_PLATFORM_bridgeos(x,y) bridgeos,introduced=x,deprecated=y
#endif
#ifndef __API_UNAVAILABLE_PLATFORM_bridgeos
 #define __API_UNAVAILABLE_PLATFORM_bridgeos bridgeos,unavailable
#endif

#endif /* OPENOSX_LIBPLATFORM_BRIDGEOS_COMPAT_H */
