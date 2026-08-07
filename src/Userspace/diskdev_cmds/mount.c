#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mount.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#define MSDOSFS_ARGSMAGIC 0xe4eff301U

struct msdosfs_args {
    char *fspec;
    uid_t uid;
    gid_t gid;
    mode_t mask;
    uint32_t flags;
    uint32_t magic;
    int32_t secondsWest;
    uint8_t label[64];
};

struct generic_mount_args {
    char *fspec;
};

/* Matches the kernel's struct hfs_mount_args (hfs_mount.h); the kernel
 * copies in this full struct, so the generic one-pointer args above would
 * feed stack garbage into hfs_changefs on -o update mounts. Fields left as
 * VNOVAL (-1) mean "no change". */
#define VNOVAL (-1)

struct hfs_timezone {
    int tz_minuteswest;
    int tz_dsttime;
};

struct hfs_mount_args {
    char *fspec;               /* block special device to mount */
    uid_t hfs_uid;             /* uid that owns hfs files (standard HFS only) */
    gid_t hfs_gid;             /* gid that owns hfs files (standard HFS only) */
    mode_t hfs_mask;           /* mask applied for hfs perms (standard HFS only) */
    uint32_t hfs_encoding;     /* encoding for this volume (standard HFS only) */
    struct hfs_timezone hfs_timezone; /* user time zone info (standard HFS only) */
    int flags;                 /* HFSFSMNT_* mounting flags */
    int journal_tbuffer_size;  /* size in bytes of the journal transaction buffer */
    int journal_flags;         /* flags to pass to journal_open/create */
    int journal_disable;       /* don't use journaling */
};

static void
usage(void)
{
    fprintf(stderr, "usage: mount -t type [-o options] special node\n");
    exit(64);
}

static int
parse_options(const char *options)
{
    char buf[256];
    char *opt;
    char *next;
    int flags = 0;

    if (!options || !*options)
        return 0;

    strncpy(buf, options, sizeof(buf) - 1);
    buf[sizeof(buf) - 1] = '\0';
    for (opt = buf; opt && *opt; opt = next) {
        next = strchr(opt, ',');
        if (next)
            *next++ = '\0';

        if (strcmp(opt, "ro") == 0 || strcmp(opt, "rdonly") == 0)
            flags |= MNT_RDONLY;
        else if (strcmp(opt, "rw") == 0)
            flags &= ~MNT_RDONLY;
        else if (strcmp(opt, "nosuid") == 0)
            flags |= MNT_NOSUID;
        else if (strcmp(opt, "nodev") == 0)
            flags |= MNT_NODEV;
        else if (strcmp(opt, "noexec") == 0)
            flags |= MNT_NOEXEC;
        else if (strcmp(opt, "sync") == 0 || strcmp(opt, "synchronous") == 0)
            flags |= MNT_SYNCHRONOUS;
        else if (strcmp(opt, "async") == 0)
            flags |= MNT_ASYNC;
        else if (strcmp(opt, "update") == 0)
            flags |= MNT_UPDATE;
        else if (strcmp(opt, "force") == 0)
            flags |= MNT_FORCE;
        else
            fprintf(stderr, "mount: ignoring unsupported option '%s'\n", opt);
    }

    return flags;
}

static int
mount_msdos(const char *device, const char *dir, int flags)
{
    struct msdosfs_args args;

    memset(&args, 0, sizeof(args));
    args.fspec = (char *)device;
    args.uid = getuid();
    args.gid = getgid();
    args.mask = 022;
    args.magic = MSDOSFS_ARGSMAGIC;

    return mount("msdos", dir, flags, &args);
}

static int
mount_hfs(const char *device, const char *dir, int flags)
{
    struct hfs_mount_args args;

    memset(&args, 0, sizeof(args));
    args.fspec = (char *)device;
    args.hfs_uid = (uid_t)VNOVAL;
    args.hfs_gid = (gid_t)VNOVAL;
    args.hfs_mask = (mode_t)VNOVAL;
    args.hfs_encoding = (uint32_t)VNOVAL;
    args.hfs_timezone.tz_minuteswest = VNOVAL;
    args.flags = VNOVAL;

    return mount("hfs", dir, flags, &args);
}

static int
mount_generic(const char *type, const char *device, const char *dir, int flags)
{
    struct generic_mount_args args;

    memset(&args, 0, sizeof(args));
    args.fspec = (char *)device;
    return mount(type, dir, flags, &args);
}

int
main(int argc, char **argv)
{
    const char *type = NULL;
    const char *options = NULL;
    const char *device;
    const char *dir;
    int ch;
    int flags;
    int rc;

    while ((ch = getopt(argc, argv, "rt:o:")) != -1) {
        switch (ch) {
        case 'r':
            options = options ? options : "ro";
            break;
        case 't':
            type = optarg;
            break;
        case 'o':
            options = optarg;
            break;
        default:
            usage();
        }
    }

    if (!type || argc - optind != 2)
        usage();

    device = argv[optind];
    dir = argv[optind + 1];
    flags = parse_options(options);

    /* mkdir("/") fails EISDIR rather than EEXIST; either means "already there". */
    if (mkdir(dir, 0755) < 0 && errno != EEXIST && errno != EISDIR) {
        perror(dir);
        return 1;
    }

    if (strcmp(type, "msdos") == 0 || strcmp(type, "msdosfs") == 0)
        rc = mount_msdos(device, dir, flags);
    else if (strcmp(type, "hfs") == 0)
        rc = mount_hfs(device, dir, flags);
    else
        rc = mount_generic(type, device, dir, flags);

    if (rc < 0) {
        fprintf(stderr, "mount: %s on %s as %s: %s\n",
            device, dir, type, strerror(errno));
        return 1;
    }

    return 0;
}
