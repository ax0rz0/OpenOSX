#include <errno.h>
#include <fcntl.h>
#ifdef OPENOSX
#include <stdlib.h>
#endif
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>
#include <wlr/config.h>
#include "util/shm.h"

#ifdef OPENOSX
#define RANDNAME_PATTERN "/tmp/wlroots-XXXXXX"
#else
#define RANDNAME_PATTERN "/wlroots-XXXXXX"
#endif

#ifndef OPENOSX
static void randname(char *buf) {
	struct timespec ts;
	clock_gettime(CLOCK_REALTIME, &ts);
	long r = ts.tv_nsec;
	for (int i = 0; i < 6; ++i) {
		buf[i] = 'A'+(r&15)+(r&16)*2;
		r >>= 5;
	}
}
#endif

static int excl_shm_open(char *name) {
	#ifdef OPENOSX
	int fd = mkstemp(name);
	if (fd >= 0) {
		(void)fcntl(fd, F_SETFD, FD_CLOEXEC);
	}
	return fd;
	#else
	int retries = 100;
	do {
		randname(name + strlen(RANDNAME_PATTERN) - 6);

		--retries;
		// CLOEXEC is guaranteed to be set by shm_open
		int fd = shm_open(name, O_RDWR | O_CREAT | O_EXCL, 0600);
		if (fd >= 0) {
			return fd;
		}
	} while (retries > 0 && errno == EEXIST);

	return -1;
	#endif
}

int allocate_shm_file(size_t size) {
	char name[] = RANDNAME_PATTERN;
	int fd = excl_shm_open(name);
	if (fd < 0) {
		return -1;
	}
	unlink(name);
#ifndef OPENOSX
	shm_unlink(name);
#endif

	int ret;
	do {
		ret = ftruncate(fd, size);
	} while (ret < 0 && errno == EINTR);
	if (ret < 0) {
		close(fd);
		return -1;
	}

	return fd;
}

bool allocate_shm_file_pair(size_t size, int *rw_fd_ptr, int *ro_fd_ptr) {
	char name[] = RANDNAME_PATTERN;
	int rw_fd = excl_shm_open(name);
	if (rw_fd < 0) {
		return false;
	}

	#ifdef OPENOSX
	// Darwin's shm_open namespace is unavailable in the guest. A duplicated
	// descriptor is still read-only in wlroots' use of this pair; the server
	// keeps the writable descriptor and the client only maps the duplicate.
	int ro_fd = dup(rw_fd);
	if (ro_fd >= 0) {
		(void)fcntl(ro_fd, F_SETFD, FD_CLOEXEC);
	}
	#else
	// CLOEXEC is guaranteed to be set by shm_open
	int ro_fd = shm_open(name, O_RDONLY, 0);
	#endif
	if (ro_fd < 0) {
		unlink(name);
#ifndef OPENOSX
		shm_unlink(name);
#endif
		close(rw_fd);
		return false;
	}

	unlink(name);
	#ifndef OPENOSX
	shm_unlink(name);
	#endif

	// Make sure the file cannot be re-opened in read-write mode (e.g. via
	// "/proc/self/fd/" on Linux)
	if (fchmod(rw_fd, 0) != 0) {
		close(rw_fd);
		close(ro_fd);
		return false;
	}

	int ret;
	do {
		ret = ftruncate(rw_fd, size);
	} while (ret < 0 && errno == EINTR);
	if (ret < 0) {
		close(rw_fd);
		close(ro_fd);
		return false;
	}

	*rw_fd_ptr = rw_fd;
	*ro_fd_ptr = ro_fd;
	return true;
}
