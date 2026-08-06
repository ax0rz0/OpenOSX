/* Minimal OpenOSX chown(1). */
#include <sys/types.h>

#include <errno.h>
#include <grp.h>
#include <pwd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static void
usage(void)
{
	fprintf(stderr, "usage: chown owner[:group] file ...\n");
	exit(2);
}

static int
parse_id(const char *s, int is_group, uid_t *uid, gid_t *gid)
{
	char *end;
	unsigned long n;

	if (s == NULL || *s == '\0') {
		return 0;
	}
	errno = 0;
	n = strtoul(s, &end, 10);
	if (errno == 0 && end != s && *end == '\0') {
		if (is_group) {
			*gid = (gid_t)n;
		} else {
			*uid = (uid_t)n;
		}
		return 0;
	}
	if (is_group) {
		struct group *gr = getgrnam(s);
		if (gr == NULL) {
			fprintf(stderr, "chown: unknown group: %s\n", s);
			return -1;
		}
		*gid = gr->gr_gid;
	} else {
		struct passwd *pw = getpwnam(s);
		if (pw == NULL) {
			fprintf(stderr, "chown: unknown user: %s\n", s);
			return -1;
		}
		*uid = pw->pw_uid;
	}
	return 0;
}

static int
parse_owner(char *spec, uid_t *uid, gid_t *gid)
{
	char *sep;

	*uid = (uid_t)-1;
	*gid = (gid_t)-1;
	sep = strchr(spec, ':');
	if (sep == NULL) {
		sep = strchr(spec, '.');
	}
	if (sep != NULL) {
		*sep++ = '\0';
	}
	if (parse_id(spec, 0, uid, gid) != 0) {
		return -1;
	}
	if (sep != NULL && parse_id(sep, 1, uid, gid) != 0) {
		return -1;
	}
	if (*uid == (uid_t)-1 && *gid == (gid_t)-1) {
		fprintf(stderr, "chown: empty owner/group\n");
		return -1;
	}
	return 0;
}

int
main(int argc, char **argv)
{
	uid_t uid;
	gid_t gid;
	int i;
	int rc = 0;

	if (argc < 3) {
		usage();
	}
	if (parse_owner(argv[1], &uid, &gid) != 0) {
		return 1;
	}
	for (i = 2; i < argc; i++) {
		if (chown(argv[i], uid, gid) != 0) {
			fprintf(stderr, "chown: %s: %s\n", argv[i], strerror(errno));
			rc = 1;
		}
	}
	return rc;
}
