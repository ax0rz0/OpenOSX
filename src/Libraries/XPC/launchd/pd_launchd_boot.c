#include <sys/stat.h>
#include <sys/mount.h>
#include <errno.h>
#include <stdio.h>

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
 * lookup
 */
extern void pd_launchd_load_daemons_dir(const char *dir);

void
pd_launchd_boot(void)
{
	pd_launchd_boot_mkdir_p("/dev", 0755);
	pd_launchd_boot_try_mount("devfs", "/dev", 0, NULL);
	pd_launchd_load_daemons_dir("/System/Library/LaunchDaemons");
}
