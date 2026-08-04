/*
 * copyfile(3) for PureDarwin.
 *
 * What is carried out, and what is not:
 *
 *   COPYFILE_DATA    copies file contents.
 *   COPYFILE_STAT    copies mode and timestamps, and attempts owner/group.
 *   COPYFILE_XATTR   copies extended attributes.
 *   COPYFILE_ACL     accepted and ignored - there is no ACL support here, so
 *                    there is nothing to copy. COPYFILE_METADATA therefore
 *                    succeeds, copying everything except the ACL.
 *   COPYFILE_CLONE   copies rather than clones. This matches the documented
 *                    contract: clone when possible, otherwise copy the data
 *                    and metadata. No filesystem here supports cloning.
 *   COPYFILE_CLONE_FORCE  fails with ENOTSUP, since it demands a real clone.
 *   COPYFILE_RECURSIVE, COPYFILE_PACK and COPYFILE_UNPACK fail with ENOTSUP.
 *
 * Symlinks, COPYFILE_EXCL, COPYFILE_UNLINK, COPYFILE_MOVE, COPYFILE_CHECK and
 * the NOFOLLOW flags all behave as documented.
 */

#include <copyfile.h>

#include <sys/stat.h>
#include <sys/time.h>
#include <sys/xattr.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

struct _copyfile_state {
	int src_fd;
	int dst_fd;
	char *src_filename;
	char *dst_filename;
	off_t copied;
};

#define COPYFILE_UNSUPPORTED_OPS \
	(COPYFILE_RECURSIVE | COPYFILE_PACK | COPYFILE_UNPACK | COPYFILE_CLONE_FORCE)

copyfile_state_t
copyfile_state_alloc(void)
{
	copyfile_state_t state = calloc(1, sizeof(*state));

	if (state != NULL) {
		state->src_fd = -2;
		state->dst_fd = -2;
	}
	return state;
}

int
copyfile_state_free(copyfile_state_t state)
{
	if (state == NULL) {
		errno = EINVAL;
		return -1;
	}
	free(state->src_filename);
	free(state->dst_filename);
	free(state);
	return 0;
}

int
copyfile_state_get(copyfile_state_t state, uint32_t flag, void *dst)
{
	if (state == NULL || dst == NULL) {
		errno = EINVAL;
		return -1;
	}
	switch (flag) {
	case COPYFILE_STATE_SRC_FD:
		*(int *)dst = state->src_fd;
		return 0;
	case COPYFILE_STATE_DST_FD:
		*(int *)dst = state->dst_fd;
		return 0;
	case COPYFILE_STATE_SRC_FILENAME:
		*(char **)dst = state->src_filename;
		return 0;
	case COPYFILE_STATE_DST_FILENAME:
		*(char **)dst = state->dst_filename;
		return 0;
	case COPYFILE_STATE_COPIED:
		*(off_t *)dst = state->copied;
		return 0;
	default:
		errno = EINVAL;
		return -1;
	}
}

int
copyfile_state_set(copyfile_state_t state, uint32_t flag, const void *src)
{
	char **slot;

	if (state == NULL) {
		errno = EINVAL;
		return -1;
	}
	switch (flag) {
	case COPYFILE_STATE_SRC_FD:
		state->src_fd = *(const int *)src;
		return 0;
	case COPYFILE_STATE_DST_FD:
		state->dst_fd = *(const int *)src;
		return 0;
	case COPYFILE_STATE_SRC_FILENAME:
	case COPYFILE_STATE_DST_FILENAME:
		slot = (flag == COPYFILE_STATE_SRC_FILENAME)
		    ? &state->src_filename : &state->dst_filename;
		free(*slot);
		*slot = (src == NULL) ? NULL : strdup((const char *)src);
		return (src != NULL && *slot == NULL) ? -1 : 0;
	default:
		errno = EINVAL;
		return -1;
	}
}

static int
copy_data(int from_fd, int to_fd, off_t *copied)
{
	char buf[65536];
	ssize_t n;

	for (;;) {
		n = read(from_fd, buf, sizeof(buf));
		if (n == 0)
			return 0;
		if (n < 0) {
			if (errno == EINTR)
				continue;
			return -1;
		}
		for (ssize_t off = 0; off < n; ) {
			ssize_t w = write(to_fd, buf + off, (size_t)(n - off));
			if (w < 0) {
				if (errno == EINTR)
					continue;
				return -1;
			}
			off += w;
			if (copied != NULL)
				*copied += w;
		}
	}
}

/* Best effort: a failure to carry metadata across is not a failed copy. */
static void
copy_xattrs(const char *from, const char *to, int nofollow)
{
	int options = nofollow ? XATTR_NOFOLLOW : 0;
	char *names, *value;
	ssize_t names_len, value_len;

	names_len = listxattr(from, NULL, 0, options);
	if (names_len <= 0)
		return;
	if ((names = malloc((size_t)names_len)) == NULL)
		return;
	names_len = listxattr(from, names, (size_t)names_len, options);
	if (names_len <= 0) {
		free(names);
		return;
	}

	for (ssize_t i = 0; i < names_len; i += (ssize_t)strlen(names + i) + 1) {
		const char *name = names + i;

		value_len = getxattr(from, name, NULL, 0, 0, options);
		if (value_len < 0)
			continue;
		if ((value = malloc((size_t)value_len + 1)) == NULL)
			continue;
		value_len = getxattr(from, name, value, (size_t)value_len, 0, options);
		if (value_len >= 0)
			(void)setxattr(to, name, value, (size_t)value_len, 0, options);
		free(value);
	}
	free(names);
}

static void
copy_stat(const struct stat *sb, const char *to, int to_fd)
{
	struct timeval times[2];

	if (to_fd >= 0) {
		(void)fchmod(to_fd, sb->st_mode & ALLPERMS);
		(void)fchown(to_fd, sb->st_uid, sb->st_gid);
	} else {
		(void)chmod(to, sb->st_mode & ALLPERMS);
		(void)chown(to, sb->st_uid, sb->st_gid);
	}

	TIMESPEC_TO_TIMEVAL(&times[0], &sb->st_atimespec);
	TIMESPEC_TO_TIMEVAL(&times[1], &sb->st_mtimespec);
	if (to_fd >= 0)
		(void)futimes(to_fd, times);
	else
		(void)utimes(to, times);
}

static int
copy_symlink(const char *from, const char *to, copyfile_flags_t flags)
{
	char target[PATH_MAX];
	ssize_t n = readlink(from, target, sizeof(target) - 1);

	if (n < 0)
		return -1;
	target[n] = '\0';

	if (flags & COPYFILE_UNLINK)
		(void)unlink(to);
	return symlink(target, to);
}

int
fcopyfile(int from_fd, int to_fd, copyfile_state_t state, copyfile_flags_t flags)
{
	struct stat sb;
	off_t *copied = (state != NULL) ? &state->copied : NULL;

	if (flags & COPYFILE_UNSUPPORTED_OPS) {
		errno = ENOTSUP;
		return -1;
	}
	if (fstat(from_fd, &sb) < 0)
		return -1;
	if (flags & COPYFILE_CHECK)
		return 0;

	if ((flags & COPYFILE_DATA) || (flags & COPYFILE_CLONE)) {
		if (lseek(from_fd, 0, SEEK_SET) < 0 && errno != ESPIPE)
			return -1;
		if (copy_data(from_fd, to_fd, copied) < 0)
			return -1;
	}
	if ((flags & COPYFILE_STAT) || (flags & COPYFILE_CLONE))
		copy_stat(&sb, NULL, to_fd);

	return 0;
}

int
copyfile(const char *from, const char *to, copyfile_state_t state,
    copyfile_flags_t flags)
{
	struct stat sb;
	int from_fd = -1, to_fd = -1, oflags, nofollow;
	off_t *copied = (state != NULL) ? &state->copied : NULL;

	if (from == NULL || to == NULL) {
		errno = EINVAL;
		return -1;
	}
	if (flags & COPYFILE_UNSUPPORTED_OPS) {
		errno = ENOTSUP;
		return -1;
	}

	/* COPYFILE_CLONE implies not following the source symlink. */
	nofollow = (flags & (COPYFILE_NOFOLLOW_SRC | COPYFILE_CLONE)) != 0;

	if ((nofollow ? lstat(from, &sb) : stat(from, &sb)) < 0)
		return -1;

	if (flags & COPYFILE_CHECK)
		return 0;

	if (S_ISLNK(sb.st_mode))
		return copy_symlink(from, to, flags);
	if (!S_ISREG(sb.st_mode)) {
		errno = ENOTSUP;
		return -1;
	}

	if ((from_fd = open(from, O_RDONLY | (nofollow ? O_NOFOLLOW : 0))) < 0)
		return -1;

	if (flags & COPYFILE_UNLINK)
		(void)unlink(to);

	oflags = O_WRONLY | O_CREAT | O_TRUNC;
	if (flags & (COPYFILE_EXCL | COPYFILE_CLONE))
		oflags |= O_EXCL;
	if (flags & COPYFILE_NOFOLLOW_DST)
		oflags |= O_NOFOLLOW;
	if ((to_fd = open(to, oflags, sb.st_mode & ALLPERMS)) < 0)
		goto fail;

	if ((flags & COPYFILE_DATA) || (flags & COPYFILE_CLONE)) {
		if (copy_data(from_fd, to_fd, copied) < 0)
			goto fail;
	}
	if ((flags & COPYFILE_STAT) || (flags & COPYFILE_CLONE))
		copy_stat(&sb, to, to_fd);

	close(from_fd);
	close(to_fd);
	from_fd = to_fd = -1;

	if ((flags & COPYFILE_XATTR) || (flags & COPYFILE_CLONE))
		copy_xattrs(from, to, nofollow);

	if (flags & COPYFILE_MOVE)
		(void)unlink(from);

	return 0;

fail:
	{
		int saved = errno;

		if (from_fd >= 0)
			close(from_fd);
		if (to_fd >= 0) {
			close(to_fd);
			(void)unlink(to);
		}
		errno = saved;
	}
	return -1;
}
