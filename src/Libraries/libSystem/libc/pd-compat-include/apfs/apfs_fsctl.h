/*
 * apfs/apfs_fsctl.h  (OpenOSX reconstruction)
 *
 * Apple ships <apfs/apfs_fsctl.h> only in its internal SDK. libc's getcwd.c uses
 * exactly one thing from it: the firmlink-probe fsctl (APFSIOC_FIRMLINK_CTL with
 * an apfs_firmlink_control_t). getcwd's __check_for_firmlink() treats ANY fsctl
 * error as "assume firmlink" and falls back to lstat()-based path building, so a
 * kernel that doesn't implement this ioctl (OpenOSX is not APFS-rooted at
 * bring-up) degrades safely to the slow, correct path.
 *
 * NB: the APFSIOC_FIRMLINK_CTL encoding below is reconstructed; if/when
 * OpenOSX runs on real APFS this must be reconciled with the apfs kext's
 * definition so the firmlink fast-path actually engages.
 */
#ifndef _APFS_APFS_FSCTL_H_
#define _APFS_APFS_FSCTL_H_

#include <stdint.h>
#include <sys/ioccom.h>

/* firmlink control command selector */
#define FIRMLINK_GET	0
#define FIRMLINK_SET	1

typedef struct apfs_firmlink_control {
	uint32_t cmd;	/* FIRMLINK_GET / FIRMLINK_SET */
	uint32_t val;	/* nonzero => path is a firmlink (GET result) */
} apfs_firmlink_control_t;

/* fsctl selector for the firmlink control (reconstructed encoding). */
#define APFSIOC_FIRMLINK_CTL	_IOWR('J', 65, apfs_firmlink_control_t)

#endif /* _APFS_APFS_FSCTL_H_ */
