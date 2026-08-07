/*
 * CarbonCore Multiprocessing Services - the surviving preemptive-task queries.
 * Deprecated since Mac OS 8; on Darwin every thread is preemptive, so the
 * answers are constant. Callers still link them, typically touching them early
 * from the main thread to avoid an MP init quirk that OpenOSX does not have.
 */

#include <stdint.h>
#include <stdbool.h>

typedef void *MPTaskID;
typedef unsigned char MPBoolean;

MPTaskID MPCurrentTaskID(void);
MPBoolean MPTaskIsPreemptive(MPTaskID taskID);

/* An opaque per-task token; callers never dereference it. */
MPTaskID
MPCurrentTaskID(void)
{
	static char pd_current_task;

	return &pd_current_task;
}

/* The cooperative "blue task" has not existed since Classic. */
MPBoolean
MPTaskIsPreemptive(MPTaskID taskID)
{
	(void)taskID;
	return 1;
}
