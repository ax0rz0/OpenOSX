/*
 * os/alloc_once_private.h  (OpenOSX reconstruction)
 *
 * Apple ships <os/alloc_once_private.h> only in its internal SDK; the open
 * libplatform drop contains os/alloc_once_impl.h (which #errors unless included
 * via this wrapper) but not the wrapper itself. This recreates it faithfully
 * from the impl's documented contract: define the allocation-key enum, set the
 * __OS_ALLOC_INDIRECT__ guard, then pull in the impl (which supplies
 * os_alloc_token_t, struct _os_alloc_once_s, _os_alloc_once_table, _os_alloc_once
 * and the os_alloc_once() fast path). The _os_alloc_once symbol + table are
 * provided by libsystem_kernel's os/alloc_once.c.
 */
#ifndef __OS_ALLOC_ONCE_PRIVATE__
#define __OS_ALLOC_ONCE_PRIVATE__

#include <sys/cdefs.h>

__BEGIN_DECLS

/* Canonical libSystem allocation-once key assignments (stable table indices,
 * < OS_ALLOC_ONCE_KEY_MAX). libpthread uses OS_ALLOC_ONCE_KEY_LIBSYSTEM_PTHREAD. */
enum {
	OS_ALLOC_ONCE_KEY_LIBSYSTEM_NOTIFY       = 1,
	OS_ALLOC_ONCE_KEY_LIBSYSTEM_C            = 2,
	OS_ALLOC_ONCE_KEY_LIBSYSTEM_INFO         = 3,
	OS_ALLOC_ONCE_KEY_LIBSYSTEM_NETWORK      = 4,
	OS_ALLOC_ONCE_KEY_LIBSYSTEM_DYLD         = 5,
	OS_ALLOC_ONCE_KEY_LIBSYSTEM_PTHREAD      = 6,
	OS_ALLOC_ONCE_KEY_LIBSYSTEM_LIBDISPATCH  = 7,
	OS_ALLOC_ONCE_KEY_LIBSYSTEM_LIBXPC       = 8,
	OS_ALLOC_ONCE_KEY_LIBSYSTEM_CORESERVICES = 9,
	OS_ALLOC_ONCE_KEY_LIBCACHE               = 10,
	OS_ALLOC_ONCE_KEY_LIBCOMMONCRYPTO        = 11,
	OS_ALLOC_ONCE_KEY_LIBSYSTEM_PLATFORM_ASL = 12,
};

__END_DECLS

#define __OS_ALLOC_INDIRECT__
#include <os/alloc_once_impl.h>

#endif /* __OS_ALLOC_ONCE_PRIVATE__ */
