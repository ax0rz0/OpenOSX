/*
 * Copyright (c) 1999-2016 Apple Inc. All rights reserved.
 *
 * @APPLE_LICENSE_HEADER_START@
 *
 * "Portions Copyright (c) 1999 Apple Computer, Inc.  All Rights
 * Reserved.  This file contains Original Code and/or Modifications of
 * Original Code as defined in and that are subject to the
 * Apple Public Source License Version 1.0 (the 'License').  You may not use
 * this file except in compliance with the License.  Please obtain a copy of
 * the License at http://www.apple.com/publicsource and read it before using
 * this file.
 *
 * The Original Code and all software distributed under the License are
 * distributed on an 'AS IS' basis, WITHOUT WARRANTY OF ANY KIND, EITHER
 * EXPRESS OR IMPLIED, AND APPLE HEREBY DISCLAIMS ALL SUCH WARRANTIES,
 * INCLUDING WITHOUT LIMITATION, ANY WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE OR NON-INFRINGEMENT.  Please see the
 * License for the specific language governing rights and limitations under
 * the License."
 *
 * @APPLE_LICENSE_HEADER_END@
 */

#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#define BF_SZ 512

static const char *progname = "mkfile";

static void usage(const char *prog_name);
static void die_errno(int eval, const char *fmt, ...) __attribute__((noreturn, format(printf, 2, 3)));
static void create_file(const char *file_name, off_t size, int empty, int verbose);
static void err_rm(const char *filename, const char *msg);

int
main(int argc, char **argv)
{
    char *b_num;
    int c;
    off_t multiplier = 1;
    off_t file_size;
    size_t len;
    int empty = 0;
    int verbose = 0;
    char *endptr = NULL;

    progname = argv[0] ? argv[0] : "mkfile";

    if (argc == 1) {
        usage(argv[0]);
    }

    opterr = 1;
    while ((c = getopt(argc, argv, "nv")) != -1) {
        switch (c) {
        case 'v':
            verbose = 1;
            break;
        case 'n':
            empty = 1;
            break;
        default:
            usage(argv[0]);
        }
    }

    argv += optind;
    if (*argv == NULL) {
        usage("mkfile");
    }

    b_num = *argv++;
    len = strlen(b_num) - 1;

    if (!isdigit((unsigned char)b_num[len])) {
        switch (b_num[len]) {
        case 'B':
        case 'b':
            multiplier = 512;
            break;
        case 'K':
        case 'k':
            multiplier = 1024;
            break;
        case 'M':
        case 'm':
            multiplier = 1024 * 1024;
            break;
        case 'G':
        case 'g':
            multiplier = 1024 * 1024 * 1024;
            break;
        default:
            usage("mkfile");
        }
    }

    if (*argv == NULL) {
        usage("mkfile");
    }

    file_size = strtoll(b_num, &endptr, 10);
    if (file_size == 0 && (*endptr != 0 && endptr != &b_num[len])) {
        die_errno(1, "bad file size");
    }

    while (*argv != NULL) {
        create_file(*argv, file_size * multiplier, empty, verbose);
        argv++;
    }

    return 0;
}

static void
create_file(const char *file_name, off_t size, int empty, int verbose)
{
    char buff[BF_SZ];
    int fd;
    ssize_t bytes_written = BF_SZ;
    off_t i;
    mode_t mode = S_IRUSR | S_IWUSR;

    if (!geteuid()) {
        mode |= S_ISVTX;
    }

    fd = open(file_name, O_RDWR | O_CREAT | O_TRUNC, mode);
    if (fd == -1) {
        die_errno(1, "%s", file_name);
    }

    if (empty) {
        if (size > 0) {
            if (lseek(fd, size - 1, SEEK_SET) == -1) {
                err_rm(file_name, "lseek error");
            }
            if (write(fd, "\0", 1) != 1) {
                err_rm(file_name, "write error");
            }
        }
    } else {
        memset(buff, 0, sizeof(buff));

        for (i = size; i > BF_SZ; i -= bytes_written) {
            bytes_written = write(fd, buff, BF_SZ);
            if (bytes_written == -1) {
                err_rm(file_name, "write error");
            }
        }
        for (; i > 0; i -= bytes_written) {
            bytes_written = write(fd, buff, (size_t)i);
            if (bytes_written == -1) {
                err_rm(file_name, "write error");
            }
        }
    }

    if (fchmod(fd, mode)) {
        err_rm(file_name, NULL);
    }
    if (close(fd) == -1) {
        err_rm(file_name, NULL);
    }

    if (verbose) {
        fprintf(stderr, "%s %lld bytes\n", file_name, (long long)size);
    }
}

static void
die_errno(int eval, const char *fmt, ...)
{
    int saved_errno = errno;
    va_list ap;

    fprintf(stderr, "%s: ", progname);
    va_start(ap, fmt);
    vfprintf(stderr, fmt, ap);
    va_end(ap);
    fprintf(stderr, ": %s\n", strerror(saved_errno));
    exit(eval);
}

static void
err_rm(const char *filename, const char *msg)
{
    int saved_errno = errno;

    unlink(filename);
    errno = saved_errno;
    die_errno(1, "(%s removed) %s", filename, msg ? msg : "");
}

static void
usage(const char *prog_name)
{
    fprintf(stderr, "usage: %s [-nv] size[b|k|m|g] filename ...\n", prog_name);
    exit(1);
}
