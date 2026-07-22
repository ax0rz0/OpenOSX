/*
 * PureDarwin console login helper.
 *
 * launchd opens StandardInPath with O_NOCTTY, so a direct /bin/zsh
 * LaunchDaemon has file descriptors for /dev/console but no controlling tty.
 * Keep the tty setup out of PID 1: launchd supervises this helper, and this
 * helper creates the console session then execs the real login shell.
 */
#include <sys/ioctl.h>
#include <errno.h>
#include <fcntl.h>
#include <stdlib.h>
#include <unistd.h>

int
main(void)
{
	const char *tty = "/dev/console";

	if (setsid() < 0 && errno != EPERM) {
		_exit(126);
	}

	int fd = open(tty, O_RDWR);
	if (fd < 0) {
		_exit(126);
	}

	(void)ioctl(fd, TIOCSCTTY, 1);
	(void)dup2(fd, STDIN_FILENO);
	(void)dup2(fd, STDOUT_FILENO);
	(void)dup2(fd, STDERR_FILENO);
	if (fd > STDERR_FILENO) {
		(void)close(fd);
	}

	setenv("HOME", "/var/root", 1);
	setenv("USER", "root", 1);
	setenv("LOGNAME", "root", 1);
	setenv("SHELL", "/bin/zsh", 1);
	setenv("PATH", "/bin:/sbin:/usr/bin:/usr/sbin", 1);
	setenv("TERM", "vt220", 0);

	char *argv[] = { "/bin/zsh", "-l", NULL };
	execv(argv[0], argv);
	_exit(127);
}
