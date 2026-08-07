/*
 * OpenOSX minimal _os_crash.
 *
 * Real Apple's implementation (os/assumes.c) logs the crash message through
 * os_log/ASL and annotates the crash report before trapping. That machinery
 * (os/log_private.h, os/reason_private.h) pulls in a much larger private-SDK
 * surface than is warranted for this bring-up. secure/chk_fail.c's os_crash()
 * macro expands to `_os_crash(msg); os_hardware_trap();` (os/assumes.h) --
 * i.e. report-then-terminate. This is a genuine (not faked) minimal version
 * of the same contract: write the message to stderr (best-effort, the fd may
 * not exist this early) then let the caller's os_hardware_trap() do the
 * actual termination -- we don't fake success or swallow the crash, we just
 * skip the structured-logging step we don't have infrastructure for yet.
 */

#include <unistd.h>
#include <string.h>

void
_os_crash(const char *message)
{
	if (message != NULL) {
		size_t len = strlen(message);
		write(2, message, len);
		write(2, "\n", 1);
	}
}
