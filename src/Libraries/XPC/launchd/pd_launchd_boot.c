/*
 * PureDarwin: real launchd's jobmgr_init() (core.c) does not itself scan
 * /System/Library/LaunchDaemons - real launchd_real callers (launchctl
 * bootstrap, or PID 1's own boot path) do that separately. This is the
 * glue for launchd_real's pid1_magic path, called once from launchd.c's
 * main() right after jobmgr_init(): mounts /dev, loads every real
 * LaunchDaemons plist (notifyd, SystemStarter, ...) via
 * pd_launchd_load_daemons_dir() - which now covers all of PD's own boot
 * scripts too, as real /System/Library/StartupItems bundles SystemStarter
 * runs - and starts a console shell.
 *
 * The console shell is spawned via a single long-lived supervisor process
 * (forked once here) that internally fork()+waitpid()s /bin/zsh in a loop
 * and re-execs it every time it exits, so a `logout`/Ctrl-D doesn't strand
 * the console. The supervisor itself is intentionally NOT tracked as a
 * real launchd job (no job_import/job_new call) - real launchd's own
 * SIGCHLD-driven reaper (jobmgr_reap_bulk in core.c) only ever sees this
 * one constant supervisor pid exit at shutdown, never the short-lived zsh
 * children it reaps itself, so there's no race with launchd's own reaper.
 */
#include <sys/stat.h>
#include <sys/mount.h>
#include <sys/wait.h>
#include <sys/ioctl.h>
#include <errno.h>
#include <fcntl.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "launch.h"
#include "launch_priv.h"
#include "core.h"

static void
pd_launchd_boot_mkdir_p(const char *path, mode_t mode)
{
	if (mkdir(path, mode) < 0 && errno != EEXIST) {
		perror(path);
	}
}

static void
pd_launchd_boot_try_mount(const char *src, const char *target, int flags, void *data)
{
	pd_launchd_boot_mkdir_p(target, 0755);
	if (mount(src, target, flags, data) < 0 && errno != EBUSY) {
		fprintf(stderr, "mount %s on %s failed: ", src, target);
		perror("");
	}
}

/*
 * Real LaunchDaemons: /System/Library/LaunchDaemons/*.plist (notifyd,
 * SystemStarter, ...) are now real on-disk plists, loaded via
 * pd_launchd_load_daemons_dir() (pd_launchd_plist.c), which parses each
 * with CFPropertyList and job_import()s the result - the same
 * jobmgr_import2() machinery a bare fork()+execv() could never reach (a
 * directly-forked child inherits launchd's own MACH_PORT_NULL bootstrap
 * port; only job_start()'s runtime_fork(j->mgr->jm_port) hands out the
 * real one). MachServices declared in a plist (e.g. notifyd's
 * com.apple.system.notification_center) are demand-started on first
 * lookup, same as real launchd - no RunAtLoad needed.
 */
extern void pd_launchd_load_daemons_dir(const char *dir);

static void
pd_launchd_boot_exec_shell_once(const char *tty)
{
	setsid();
	int fd = open(tty, O_RDWR | O_NOCTTY);
	if (fd >= 0) {
		ioctl(fd, TIOCSCTTY, 1);
		dup2(fd, STDIN_FILENO);
		dup2(fd, STDOUT_FILENO);
		dup2(fd, STDERR_FILENO);
		if (fd > STDERR_FILENO) {
			close(fd);
		}
	}
	setenv("HOME", "/var/root", 1);
	setenv("USER", "root", 1);
	setenv("LOGNAME", "root", 1);
	setenv("SHELL", "/bin/zsh", 1);
	setenv("PATH", "/bin:/sbin:/usr/bin:/usr/sbin", 1);
	setenv("TERM", "vt220", 0);
	char *argv[] = { "/bin/zsh", "-l", NULL };
	execv(argv[0], argv);
	perror(argv[0]);
	_exit(127);
}

/*
 * Runs as the long-lived supervisor child forked by
 * pd_launchd_boot_spawn_console_shell(): fork()+exec()s the console shell,
 * waits for it, and does it again - so exiting the shell (logout, Ctrl-D)
 * gets you another login prompt instead of a dead console.
 */
static void
pd_launchd_boot_console_shell_supervisor(const char *tty)
{
	for (;;) {
		pid_t pid = fork();
		if (pid < 0) {
			perror("fork");
			sleep(1);
			continue;
		}
		if (pid == 0) {
			pd_launchd_boot_exec_shell_once(tty);
			/* unreachable: pd_launchd_boot_exec_shell_once() always execs or _exit()s */
		}

		int status = 0;
		while (waitpid(pid, &status, 0) < 0 && errno == EINTR) {
			continue;
		}
	}
}

static void
pd_launchd_boot_spawn_console_shell(void)
{
	const char *tty = "/dev/console";

	if (access(tty, R_OK | W_OK) < 0) {
		return;
	}

	pid_t pid = fork();
	if (pid < 0) {
		perror("fork");
		return;
	}
	if (pid == 0) {
		pd_launchd_boot_console_shell_supervisor(tty);
		_exit(0);
	}
}

void
pd_launchd_boot(void)
{
	pd_launchd_boot_mkdir_p("/dev", 0755);
	pd_launchd_boot_try_mount("devfs", "/dev", 0, NULL);
	pd_launchd_load_daemons_dir("/System/Library/LaunchDaemons");
	pd_launchd_boot_spawn_console_shell();
}
