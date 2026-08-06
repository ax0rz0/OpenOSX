/*
 * os/thread_self_restrict.h  (OpenOSX reconstruction)
 *
 * Apple ships the real <os/thread_self_restrict.h> only in its internal SDK; the
 * open drops (incl. the xnu libsyscall copy) contain just an empty guard stub.
 * The three entry points below implement per-thread RWX (JIT) memory
 * restriction, which is an Apple Silicon feature (MAP_JIT + pthread_jit_write_
 * protect). On x86_64 there is no such hardware restriction, so the honest
 * implementation is: unsupported, and the to_rx/to_rw transitions are no-ops.
 * libpthread's pthread_jit_write_protect_np() consults is_supported() first and
 * therefore never calls the transitions on x86.
 */
#ifndef OS_THREAD_SELF_RESTRICT_H
#define OS_THREAD_SELF_RESTRICT_H

#include <sys/cdefs.h>
#include <stdbool.h>

__BEGIN_DECLS

/* Whether per-thread RWX restriction (JIT write-protect) is available. x86_64:
 * no (this is an arm64e/MAP_JIT capability). */
__attribute__((always_inline))
static inline bool
os_thread_self_restrict_rwx_is_supported(void)
{
	return false;
}

/* Transition this thread's JIT region to RX (executable). No-op on x86_64. */
__attribute__((always_inline))
static inline void
os_thread_self_restrict_rwx_to_rx(void)
{
}

/* Transition this thread's JIT region to RW (writable). No-op on x86_64. */
__attribute__((always_inline))
static inline void
os_thread_self_restrict_rwx_to_rw(void)
{
}

__END_DECLS

#endif /* OS_THREAD_SELF_RESTRICT_H */
