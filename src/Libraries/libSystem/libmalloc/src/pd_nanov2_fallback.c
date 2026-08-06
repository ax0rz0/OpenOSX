/*
 * OpenOSX: nanov2 (nano allocator v2) fallback.
 *
 * nanov2_malloc.c's actual implementation is gated behind OS_VARIANT_RESOLVED/
 * OS_VARIANT_NOTRESOLVED -- Apple's ifunc-based CPU/OS-variant multiversioning
 * system (os_variant_resolved.h and friends), which is internal-SDK-only and
 * not something meaningful to port for a single-target QEMU bring-up. With
 * neither macro defined, nanov2_malloc.c's own body compiles to nothing, but
 * its 4 externally-called entry points (nanov2_create_zone/_init/_configure/
 * _forked_zone, called from malloc.c and nano_malloc_common.c) still need
 * definitions to link.
 *
 * This is a genuine, valid configuration Apple's own code anticipates: on a
 * platform where nanov2 was never resolved/built, nanov2_create_zone returning
 * NULL is exactly what malloc.c's caller (create the "helper" nano zone, fall
 * back to nano v1 or the scalable zone if unavailable) already handles -- see
 * malloc.c's `nano_zone = nanov2_create_zone(...)` followed by an existing
 * NULL check before falling through. init/configure/forked_zone are no-ops
 * for the same reason (nothing to configure if the zone was never created).
 */

#include <stddef.h>
#include <malloc/malloc.h>

typedef struct nanozonev2_s nanozonev2_t;

malloc_zone_t *
nanov2_create_zone(malloc_zone_t *helper_zone, unsigned debug_flags)
{
	(void)helper_zone;
	(void)debug_flags;
	return NULL;
}

void
nanov2_init(const char *envp[], const char *apple[], const char *bootargs)
{
	(void)envp;
	(void)apple;
	(void)bootargs;
}

void
nanov2_configure(void)
{
}

void
nanov2_forked_zone(nanozonev2_t *nanozone)
{
	(void)nanozone;
}
