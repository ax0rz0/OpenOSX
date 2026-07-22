/*
 * PureDarwin: XNU's load_init_program() execs /sbin/launchd directly as
 * PID 1 with no shell/env-setting mechanism in between, so there's no
 * normal way to set DYLD_PRINT_SEGMENTS=1 (dyld's real, documented
 * diagnostic env var that prints each loaded image's segment addresses
 * and slide) for a PID-1 process. This wrapper is installed AS
 * /sbin/launchd instead of launchd_real directly: it setenv()s the real
 * dyld diagnostic variable, then execve()s the real launchd_real binary
 * in its own place (same PID, same "is this PID 1" contract XNU already
 * established - execve() doesn't fork), so dyld's own real startup
 * logging tells us the actual runtime slide for symbolicating crashes,
 * instead of guessing at it post-mortem.
 */
#include <unistd.h>
#include <stdlib.h>

int
main(int argc, char *argv[])
{
	(void)argc;
	setenv("DYLD_PRINT_SEGMENTS", "1", 1);
	setenv("DYLD_PRINT_LIBRARIES", "1", 1);
	execv("/sbin/launchd_real", argv);
	_exit(127);
}
