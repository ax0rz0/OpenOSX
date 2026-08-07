/*
 * OpenOSX compat: <pthread_workqueue.h> as a top-level public header does
 * not exist in this SDK drop or in PD's own tree -- only the differently-named
 * private/pthread/workqueue_private.h. abort.c's only use is
 * __pthread_workqueue_setkill(1), to stop workqueue threads from picking up
 * new work while the process is mid-abort (a crash-safety nicety, not
 * essential correctness); the real declaration lives there, so just forward.
 */
#ifndef _PD_PTHREAD_WORKQUEUE_H
#define _PD_PTHREAD_WORKQUEUE_H

#include <pthread/workqueue_private.h>

#endif /* _PD_PTHREAD_WORKQUEUE_H */
