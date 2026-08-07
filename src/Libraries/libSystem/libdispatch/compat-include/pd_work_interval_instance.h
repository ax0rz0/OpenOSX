#ifndef PD_WORK_INTERVAL_INSTANCE_H
#define PD_WORK_INTERVAL_INSTANCE_H

/*
 * work_interval_instance_{alloc,free,clear,set_*,start,update,finish} is a
 * real macOS frame-pacing/QoS API (os/work_interval_private.h-ish) whose
 * FUNCTIONS aren't vendored here at all - only its opaque handle type,
 * work_interval_instance_t, already exists (sys/work_interval.h). workgroup.c's
 * os_workgroup_interval_* API (used by queue.c/init.c for real, unlike this
 * one) is built on top of it. Stub the missing functions as an
 * always-degraded no-op: instances "alloc" to a non-NULL sentinel so callers
 * don't treat every workgroup interval as an allocation failure, and every
 * operation succeeds without doing anything - work intervals just never get
 * real kernel-side timing hints, same class of degradation as AppleVTD's
 * no-op stand-in.
 */

#include <stdint.h>
#include <sys/work_interval.h>
/* work_interval_instance_t is already typedef'd here to
 * struct work_interval_instance * - reuse it, don't redeclare it. */

static inline work_interval_instance_t
work_interval_instance_alloc(work_interval_t wi)
{
	(void)wi;
	return (work_interval_instance_t)(void *)-1;
}

static inline void
work_interval_instance_free(work_interval_instance_t wii)
{
	(void)wii;
}

static inline void
work_interval_instance_clear(work_interval_instance_t wii)
{
	(void)wii;
}

static inline void
work_interval_instance_set_start(work_interval_instance_t wii, uint64_t start)
{
	(void)wii; (void)start;
}

static inline void
work_interval_instance_set_deadline(work_interval_instance_t wii, uint64_t deadline)
{
	(void)wii; (void)deadline;
}

static inline void
work_interval_instance_set_finish(work_interval_instance_t wii, uint64_t finish)
{
	(void)wii; (void)finish;
}

static inline int
work_interval_instance_start(work_interval_instance_t wii)
{
	(void)wii;
	return 0;
}

static inline int
work_interval_instance_update(work_interval_instance_t wii)
{
	(void)wii;
	return 0;
}

static inline int
work_interval_instance_finish(work_interval_instance_t wii)
{
	(void)wii;
	return 0;
}

#endif /* PD_WORK_INTERVAL_INSTANCE_H */
