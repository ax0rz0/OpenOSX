/*
 * gen/FreeBSD/wait.c implements __wait and publishes wait as a weak alias
 * via __weak_reference(__wait, wait). That macro does not emit a usable
 * Mach-O alias under this (osxcross) toolchain - see pd_strtok_r.c for the
 * same issue with strtok_r - so provide the public wait() as a thin
 * forwarder instead.
 */
#include <sys/types.h>
#include <sys/wait.h>

extern pid_t __wait(int *);

pid_t
wait(int *istat)
{
	return __wait(istat);
}
