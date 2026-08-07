#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mount.h>
#include <unistd.h>

static void
usage(void)
{
    fprintf(stderr, "usage: umount [-f] target\n");
    exit(64);
}

int
main(int argc, char **argv)
{
    const char *target;
    int ch;
    int flags = 0;

    while ((ch = getopt(argc, argv, "f")) != -1) {
        switch (ch) {
        case 'f':
            flags |= MNT_FORCE;
            break;
        default:
            usage();
        }
    }

    if (argc - optind != 1)
        usage();

    target = argv[optind];
    if (unmount(target, flags) < 0) {
        fprintf(stderr, "umount: %s: %s\n", target, strerror(errno));
        return 1;
    }

    return 0;
}
