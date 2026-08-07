#ifndef __APPLE__
#define _GNU_SOURCE
#endif

/*
 * Copyright (c) 2012 Apple, Inc. All rights reserved.
 *
 * @APPLE_LICENSE_HEADER_START@
 *
 * This file contains Original Code and/or Modifications of Original Code
 * as defined in and that are subject to the Apple Public Source License
 * Version 2.0 (the 'License'). You may not use this file except in
 * compliance with the License. Please obtain a copy of the License at
 * http://www.opensource.apple.com/apsl/ and read it before using this
 * file.
 *
 * The Original Code and all software distributed under the License are
 * distributed on an 'AS IS' basis, WITHOUT WARRANTY OF ANY KIND, EITHER
 * EXPRESS OR IMPLIED, AND APPLE HEREBY DISCLAIMS ALL SUCH WARRANTIES,
 * INCLUDING WITHOUT LIMITATION, ANY WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE, QUIET ENJOYMENT OR NON-INFRINGEMENT.
 * Please see the License for the specific language governing rights and
 * limitations under the License.
 *
 * @APPLE_LICENSE_HEADER_END@
 */

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <stdbool.h>
#include <errno.h>
#include <err.h>
#include <sysexits.h>

#include <fcntl.h>
#include <sys/stat.h>
#include <sys/param.h>
#include <sys/time.h>
#include <string.h>

#ifdef __APPLE__
#include <copyfile.h>
#include <sys/types.h>
#else
#include <sys/sendfile.h>
#endif

void usage(void);

#ifndef __APPLE__
// Linux fallback: copy using sendfile or read/write
int copy_file(int srcfd, int dstfd, off_t size) {
	off_t offset = 0;
	ssize_t sent;

	while (offset < size) {
		sent = sendfile(dstfd, srcfd, &offset, size - offset);
		if (sent <= 0) {
			if (errno == EINTR || errno == EAGAIN)
				continue;
			return -1;
		}
	}
	return 0;
}
#endif

int main(int argc, char *argv[]) {
	struct stat sb;
	mode_t mode = 0;
	bool gotmode = false;
	int ch;
	int ret;
	int srcfd, dstfd;
	const char *src = NULL;
	const char *dst = NULL;
	char *dsttmpname = NULL;

	while ((ch = getopt(argc, argv, "cSm:")) != -1) {
		switch (ch) {
		case 'c':
		case 'S':
			// ignored for compatibility
			break;
		case 'm':
			gotmode = true;
#ifdef __APPLE__
			{
				void *mset = setmode(optarg);
				if (!mset) errx(EX_USAGE, "Unrecognized mode %s", optarg);
				mode = getmode(mset, 0);
				free(mset);
			}
#else
			mode = strtol(optarg, NULL, 8); // Linux: parse as octal
#endif
			break;
		case '?':
		default:
			usage();
		}
	}

	argc -= optind;
	argv += optind;

	if (argc < 2) {
		usage();
	}

	src = argv[0];
	dst = argv[1];

	srcfd = open(src, O_RDONLY);
	if (srcfd < 0) err(EX_NOINPUT, "open(%s)", src);

	ret = fstat(srcfd, &sb);
	if (ret < 0) err(EX_NOINPUT, "fstat(%s)", src);

	if (!S_ISREG(sb.st_mode)) err(EX_USAGE, "%s is not a regular file", src);

	if (asprintf(&dsttmpname, "%s.XXXXXX", dst) < 0) {
		err(EX_UNAVAILABLE, "asprintf(%s)", dst);
	}
	dstfd = mkstemp(dsttmpname);
	if (dstfd < 0) err(EX_UNAVAILABLE, "mkstemp(%s)", dsttmpname);

#ifdef __APPLE__
	ret = fcopyfile(srcfd, dstfd, NULL, COPYFILE_DATA);
#else
	ret = copy_file(srcfd, dstfd, sb.st_size);
#endif
	if (ret < 0) err(EX_UNAVAILABLE, "file copy failed (%s -> %s)", src, dsttmpname);

	ret = futimes(dstfd, NULL);
	if (ret < 0) err(EX_UNAVAILABLE, "futimes(%s)", dsttmpname);

	if (gotmode) {
		ret = fchmod(dstfd, mode);
		if (ret < 0) err(EX_NOINPUT, "fchmod(%s, %o)", dsttmpname, mode);
	}

	ret = rename(dsttmpname, dst);
	if (ret < 0) err(EX_NOINPUT, "rename(%s, %s)", dsttmpname, dst);

	close(dstfd);
	close(srcfd);
	free(dsttmpname);
	return 0;
}

void
usage(void)
{
	fprintf(stderr, "Usage: %s [-c] [-S] [-m <mode>] <src> <dst>\n",
	    "installfile");
	exit(EX_USAGE);
}
