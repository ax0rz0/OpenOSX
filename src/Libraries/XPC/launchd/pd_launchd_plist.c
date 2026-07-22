/*
 * PureDarwin: real launchd (and launchctl) load job definitions from
 * .plist bundles under /System/Library/LaunchDaemons (and friends) via
 * CFPropertyList, then hand the result to job_import()/job_import_bulk()
 * (jobmgr_import2() in core.c). PD didn't have that front end wired up -
 * jobs were hand-built as launch_data_t in C (see pd_launchd_boot.c's
 * former pd_launchd_boot_import_notifyd()). This is that front end: read
 * each on-disk .plist with the same CFPropertyListCreateFromXMLData +
 * mmap() pattern SystemStarter/StartupItems.c already uses successfully,
 * convert the resulting CFPropertyList tree into launch_data_t, and
 * job_import() it - so LaunchDaemons plists are real files, not C
 * literals baked into launchd.
 */
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <dirent.h>
#include <fcntl.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

/* Umbrella header, same as SystemStarter/StartupItems.c: individual CF
 * headers split declarations across files inconsistently between our
 * vendored source and the real Apple SDK (e.g. CFBooleanRef lives in
 * CFNumber.h in the real SDK, not its own CFBoolean.h). */
#include <CoreFoundation/CoreFoundation.h>

#include "launch.h"
#include "launch_priv.h"
#include "core.h"

static launch_data_t pd_cf_to_launch_data(CFTypeRef value);

struct pd_cf_dict_ctx {
	launch_data_t ldict;
};

static void
pd_cf_dict_apply(const void *key, const void *value, void *ctxp)
{
	struct pd_cf_dict_ctx *ctx = ctxp;
	char keybuf[256];

	if (CFGetTypeID(key) != CFStringGetTypeID()) {
		return;
	}
	if (!CFStringGetCString((CFStringRef)key, keybuf, sizeof(keybuf),
			kCFStringEncodingUTF8)) {
		return;
	}

	launch_data_t lval = pd_cf_to_launch_data((CFTypeRef)value);
	if (lval == NULL) {
		return;
	}
	launch_data_dict_insert(ctx->ldict, lval, keybuf);
}

static launch_data_t
pd_cf_to_launch_data(CFTypeRef value)
{
	CFTypeID type = CFGetTypeID(value);

	if (type == CFDictionaryGetTypeID()) {
		launch_data_t ldict = launch_data_alloc(LAUNCH_DATA_DICTIONARY);
		struct pd_cf_dict_ctx ctx = { ldict };
		CFDictionaryApplyFunction((CFDictionaryRef)value, pd_cf_dict_apply, &ctx);
		return ldict;
	}

	if (type == CFArrayGetTypeID()) {
		CFArrayRef arr = (CFArrayRef)value;
		CFIndex count = CFArrayGetCount(arr);
		launch_data_t larray = launch_data_alloc(LAUNCH_DATA_ARRAY);
		for (CFIndex i = 0; i < count; i++) {
			launch_data_t lval = pd_cf_to_launch_data(CFArrayGetValueAtIndex(arr, i));
			if (lval != NULL) {
				launch_data_array_set_index(larray, lval, (size_t)i);
			}
		}
		return larray;
	}

	if (type == CFStringGetTypeID()) {
		char buf[1024];
		if (!CFStringGetCString((CFStringRef)value, buf, sizeof(buf),
				kCFStringEncodingUTF8)) {
			buf[0] = '\0';
		}
		return launch_data_new_string(buf);
	}

	if (type == CFBooleanGetTypeID()) {
		return launch_data_new_bool(CFBooleanGetValue((CFBooleanRef)value));
	}

	if (type == CFNumberGetTypeID()) {
		long long n = 0;
		CFNumberGetValue((CFNumberRef)value, kCFNumberLongLongType, &n);
		return launch_data_new_integer(n);
	}

	/* Unsupported plist value type (CFData/CFDate/...): no launch_data_t
	 * equivalent in this subset, drop the key rather than fabricate one. */
	return NULL;
}

/*
 * Load and job_import() a single LaunchDaemon .plist. Returns true on
 * success (parsed + imported), matching the mmap/CFPropertyListCreateFromXMLData
 * pattern already proven by SystemStarter/StartupItems.c.
 */
static bool
pd_launchd_import_plist(const char *path)
{
	int fd;
	struct stat st;
	void *map = MAP_FAILED;
	bool ok = false;

	fd = open(path, O_RDONLY | O_NOCTTY);
	if (fd < 0) {
		return false;
	}
	if (fstat(fd, &st) < 0 || st.st_size == 0) {
		close(fd);
		return false;
	}

	map = mmap(NULL, (size_t)st.st_size, PROT_READ, MAP_PRIVATE, fd, 0);
	close(fd);
	if (map == MAP_FAILED) {
		fprintf(stderr, "pd_launchd_boot: mmap(%s) failed\n", path);
		return false;
	}

	CFDataRef data = CFDataCreateWithBytesNoCopy(NULL, (const UInt8 *)map,
			st.st_size, kCFAllocatorNull);
	if (data != NULL) {
		CFPropertyListRef plist = CFPropertyListCreateFromXMLData(NULL, data,
				kCFPropertyListImmutable, NULL);
		if (plist != NULL) {
			if (CFGetTypeID(plist) == CFDictionaryGetTypeID()) {
				launch_data_t job = pd_cf_to_launch_data(plist);
				if (job != NULL) {
					if (job_import(job) == NULL) {
						fprintf(stderr,
							"pd_launchd_boot: job_import(%s) failed\n", path);
					} else {
						ok = true;
					}
					launch_data_free(job);
				}
			} else {
				fprintf(stderr,
					"pd_launchd_boot: %s is not a dictionary plist\n", path);
			}
			CFRelease(plist);
		} else {
			fprintf(stderr, "pd_launchd_boot: failed to parse %s\n", path);
		}
		CFRelease(data);
	}

	munmap(map, (size_t)st.st_size);
	return ok;
}

/*
 * Scan a LaunchDaemons-style directory and job_import() every *.plist in
 * it. Mirrors real launchd's jobmgr_init()->load_launchd_jobs_at_path()
 * bulk-load of /System/Library/LaunchDaemons at boot.
 */
void
pd_launchd_load_daemons_dir(const char *dir)
{
	DIR *dp = opendir(dir);
	if (dp == NULL) {
		return;
	}

	struct dirent *de;
	while ((de = readdir(dp)) != NULL) {
		size_t namelen = strlen(de->d_name);
		if (namelen < 7 || strcmp(de->d_name + namelen - 6, ".plist") != 0) {
			continue;
		}

		char path[1024];
		if (snprintf(path, sizeof(path), "%s/%s", dir, de->d_name) >= (int)sizeof(path)) {
			continue;
		}

		pd_launchd_import_plist(path);
	}

	closedir(dp);
}
