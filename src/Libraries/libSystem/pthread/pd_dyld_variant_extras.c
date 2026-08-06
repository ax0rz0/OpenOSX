/*
 * pd_dyld_variant_extras.c  (OpenOSX)
 *
 * Two small libpthread internals that live under #if !VARIANT_DYLD in their
 * upstream files (qos.c, pthread_dependency.c) yet are referenced by
 * NON-gated functions in pthread.c (pthread_introspection_hook_install,
 * the workqueue setdispatch compat path). Because pthread.c is one .o, dyld
 * pulls those functions and needs these two symbols resolved even though it
 * never calls them at runtime. We supply them verbatim from upstream so the
 * dyld variant links; behaviour is identical to the !VARIANT_DYLD build.
 */

#include "internal.h"

PTHREAD_NOEXPORT_VARIANT void *
_pthread_atomic_xchg_ptr(void **p, void *v)
{
	return os_atomic_xchg(p, v, seq_cst);
}

PTHREAD_NOEXPORT_VARIANT pthread_priority_t
_pthread_qos_class_encode_workqueue(int queue_priority, unsigned long flags)
{
	thread_qos_t qos;
	switch (queue_priority) {
	case WORKQ_HIGH_PRIOQUEUE:      qos = THREAD_QOS_USER_INTERACTIVE; break;
	case WORKQ_DEFAULT_PRIOQUEUE:   qos = THREAD_QOS_LEGACY; break;
	case WORKQ_NON_INTERACTIVE_PRIOQUEUE:
	case WORKQ_LOW_PRIOQUEUE:       qos = THREAD_QOS_UTILITY; break;
	case WORKQ_BG_PRIOQUEUE:        qos = THREAD_QOS_BACKGROUND; break;
	default:
		PTHREAD_CLIENT_CRASH(queue_priority, "Invalid priority");
	}
	return _pthread_priority_make_from_thread_qos(qos, 0, flags);
}
