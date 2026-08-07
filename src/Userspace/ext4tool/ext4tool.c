#include <sys/types.h>
#include <sys/stat.h>
#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define EXT4_SUPER_MAGIC 0xef53
#define EXT4_ROOT_INO 2
#define EXT4_N_BLOCKS 15
#define EXT4_EXTENTS_FL 0x00080000
#define EXT4_EXT_MAGIC 0xf30a
#define EXT4_FEATURE_INCOMPAT_64BIT 0x0080
#define EXT4_FT_DIR_CSUM 0xde
#define RAW_IO_ALIGN 512

#define EXT4_S_IFMT  0170000
#define EXT4_S_IFIFO 0010000
#define EXT4_S_IFCHR 0020000
#define EXT4_S_IFDIR 0040000
#define EXT4_S_IFBLK 0060000
#define EXT4_S_IFREG 0100000
#define EXT4_S_IFLNK 0120000
#define EXT4_S_IFSOCK 0140000

struct ext4_super_block {
    uint32_t s_inodes_count;
    uint32_t s_blocks_count_lo;
    uint32_t s_r_blocks_count_lo;
    uint32_t s_free_blocks_count_lo;
    uint32_t s_free_inodes_count;
    uint32_t s_first_data_block;
    uint32_t s_log_block_size;
    uint32_t s_log_cluster_size;
    uint32_t s_blocks_per_group;
    uint32_t s_clusters_per_group;
    uint32_t s_inodes_per_group;
    uint32_t s_mtime;
    uint32_t s_wtime;
    uint16_t s_mnt_count;
    uint16_t s_max_mnt_count;
    uint16_t s_magic;
    uint16_t s_state;
    uint16_t s_errors;
    uint16_t s_minor_rev_level;
    uint32_t s_lastcheck;
    uint32_t s_checkinterval;
    uint32_t s_creator_os;
    uint32_t s_rev_level;
    uint16_t s_def_resuid;
    uint16_t s_def_resgid;
    uint32_t s_first_ino;
    uint16_t s_inode_size;
    uint16_t s_block_group_nr;
    uint32_t s_feature_compat;
    uint32_t s_feature_incompat;
    uint32_t s_feature_ro_compat;
    uint8_t  s_uuid[16];
    uint8_t  s_volume_name[16];
    uint8_t  s_last_mounted[64];
    uint32_t s_algorithm_usage_bitmap;
    uint8_t  s_prealloc_blocks;
    uint8_t  s_prealloc_dir_blocks;
    uint16_t s_reserved_gdt_blocks;
    uint8_t  pad[760];
} __attribute__((packed));

struct ext4_group_desc {
    uint32_t bg_block_bitmap_lo;
    uint32_t bg_inode_bitmap_lo;
    uint32_t bg_inode_table_lo;
    uint16_t bg_free_blocks_count_lo;
    uint16_t bg_free_inodes_count_lo;
    uint16_t bg_used_dirs_count_lo;
    uint16_t bg_flags;
    uint32_t bg_exclude_bitmap_lo;
    uint16_t bg_block_bitmap_csum_lo;
    uint16_t bg_inode_bitmap_csum_lo;
    uint16_t bg_itable_unused_lo;
    uint16_t bg_checksum;
    uint32_t bg_block_bitmap_hi;
    uint32_t bg_inode_bitmap_hi;
    uint32_t bg_inode_table_hi;
    uint16_t bg_free_blocks_count_hi;
    uint16_t bg_free_inodes_count_hi;
    uint16_t bg_used_dirs_count_hi;
    uint16_t bg_itable_unused_hi;
    uint32_t bg_exclude_bitmap_hi;
    uint16_t bg_block_bitmap_csum_hi;
    uint16_t bg_inode_bitmap_csum_hi;
    uint32_t bg_reserved;
} __attribute__((packed));

struct ext4_inode {
    uint16_t i_mode;
    uint16_t i_uid;
    uint32_t i_size_lo;
    uint32_t i_atime;
    uint32_t i_ctime;
    uint32_t i_mtime;
    uint32_t i_dtime;
    uint16_t i_gid;
    uint16_t i_links_count;
    uint32_t i_blocks_lo;
    uint32_t i_flags;
    uint32_t i_osd1;
    uint32_t i_block[EXT4_N_BLOCKS];
    uint32_t i_generation;
    uint32_t i_file_acl_lo;
    uint32_t i_size_high;
    uint32_t i_obso_faddr;
    uint8_t  i_osd2[12];
    uint16_t i_extra_isize;
    uint16_t i_checksum_hi;
} __attribute__((packed));

struct ext4_extent_header {
    uint16_t eh_magic;
    uint16_t eh_entries;
    uint16_t eh_max;
    uint16_t eh_depth;
    uint32_t eh_generation;
} __attribute__((packed));

struct ext4_extent_idx {
    uint32_t ei_block;
    uint32_t ei_leaf_lo;
    uint16_t ei_leaf_hi;
    uint16_t ei_unused;
} __attribute__((packed));

struct ext4_extent {
    uint32_t ee_block;
    uint16_t ee_len;
    uint16_t ee_start_hi;
    uint32_t ee_start_lo;
} __attribute__((packed));

struct ext4_dir_entry_2 {
    uint32_t inode;
    uint16_t rec_len;
    uint8_t  name_len;
    uint8_t  file_type;
    char     name[];
} __attribute__((packed));

struct fs {
    int fd;
    struct ext4_super_block sb;
    uint32_t block_size;
    uint32_t inode_size;
    uint32_t desc_size;
    uint32_t groups_count;
};

static uint16_t rd16(uint16_t v) { return v; }
static uint32_t rd32(uint32_t v) { return v; }
static uint64_t inode_size(const struct ext4_inode *in)
{
    return rd32(in->i_size_lo) | ((uint64_t)rd32(in->i_size_high) << 32);
}

static int read_full_at(int fd, void *buf, size_t len, uint64_t off)
{
    char *p = (char *)buf;
    while (len != 0) {
        ssize_t n;
        if (lseek(fd, (off_t)off, SEEK_SET) < 0)
            return errno;
        n = read(fd, p, len);
        if (n < 0)
            return errno;
        if (n == 0)
            return EIO;
        p += n;
        off += (uint64_t)n;
        len -= (size_t)n;
    }
    return 0;
}

static int read_exact(int fd, void *buf, size_t len, uint64_t off)
{
    uint64_t aligned_off = off & ~(uint64_t)(RAW_IO_ALIGN - 1);
    uint64_t end = off + len;
    uint64_t aligned_end = (end + RAW_IO_ALIGN - 1) & ~(uint64_t)(RAW_IO_ALIGN - 1);
    size_t aligned_len = (size_t)(aligned_end - aligned_off);
    char *tmp;
    int err;

    if ((off & (RAW_IO_ALIGN - 1)) == 0 && (len & (RAW_IO_ALIGN - 1)) == 0)
        return read_full_at(fd, buf, len, off);

    tmp = malloc(aligned_len);
    if (tmp == NULL)
        return ENOMEM;
    err = read_full_at(fd, tmp, aligned_len, aligned_off);
    if (err == 0)
        memcpy(buf, tmp + (off - aligned_off), len);
    free(tmp);
    return err;
}

static int fs_read_block(struct fs *fs, uint64_t pblk, void *buf)
{
    return read_exact(fs->fd, buf, fs->block_size, pblk * fs->block_size);
}

static int fs_open(struct fs *fs, const char *dev)
{
    uint64_t blocks;

    memset(fs, 0, sizeof(*fs));
    fs->fd = open(dev, O_RDONLY);
    if (fs->fd < 0)
        return errno;
    if (read_exact(fs->fd, &fs->sb, sizeof(fs->sb), 1024) != 0)
        return EIO;
    if (rd16(fs->sb.s_magic) != EXT4_SUPER_MAGIC)
        return EINVAL;
    fs->block_size = 1024u << rd32(fs->sb.s_log_block_size);
    fs->inode_size = rd16(fs->sb.s_inode_size);
    if (fs->inode_size == 0)
        fs->inode_size = 128;
    fs->desc_size = (rd32(fs->sb.s_feature_incompat) & EXT4_FEATURE_INCOMPAT_64BIT) ? 64 : 32;
    blocks = rd32(fs->sb.s_blocks_count_lo);
    fs->groups_count = (uint32_t)((blocks - rd32(fs->sb.s_first_data_block) +
        rd32(fs->sb.s_blocks_per_group) - 1) / rd32(fs->sb.s_blocks_per_group));
    return 0;
}

static int read_group_desc(struct fs *fs, uint32_t grp, struct ext4_group_desc *gd)
{
    uint64_t gdt_block = rd32(fs->sb.s_first_data_block) + 1;
    uint64_t off = gdt_block * fs->block_size + (uint64_t)grp * fs->desc_size;
    memset(gd, 0, sizeof(*gd));
    return read_exact(fs->fd, gd, fs->desc_size < sizeof(*gd) ? fs->desc_size : sizeof(*gd), off);
}

static uint64_t gd_inode_table(struct fs *fs, const struct ext4_group_desc *gd)
{
    uint64_t pblk = rd32(gd->bg_inode_table_lo);
    if (fs->desc_size >= 64)
        pblk |= (uint64_t)rd32(gd->bg_inode_table_hi) << 32;
    return pblk;
}

static int read_inode(struct fs *fs, uint32_t ino, struct ext4_inode *out)
{
    struct ext4_group_desc gd;
    uint32_t grp, index;
    uint64_t itable, byteoff, off;
    int err;

    if (ino == 0 || ino > rd32(fs->sb.s_inodes_count))
        return EINVAL;
    grp = (ino - 1) / rd32(fs->sb.s_inodes_per_group);
    index = (ino - 1) % rd32(fs->sb.s_inodes_per_group);
    err = read_group_desc(fs, grp, &gd);
    if (err)
        return err;
    itable = gd_inode_table(fs, &gd);
    byteoff = (uint64_t)index * fs->inode_size;
    off = itable * fs->block_size + byteoff;
    memset(out, 0, sizeof(*out));
    return read_exact(fs->fd, out, fs->inode_size < sizeof(*out) ? fs->inode_size : sizeof(*out), off);
}

static int inode_location(struct fs *fs, uint32_t ino, uint64_t *pblk_out,
    uint32_t *off_out)
{
    struct ext4_group_desc gd;
    uint32_t grp, index;
    uint64_t itable, byteoff;
    int err;

    if (ino == 0 || ino > rd32(fs->sb.s_inodes_count))
        return EINVAL;
    grp = (ino - 1) / rd32(fs->sb.s_inodes_per_group);
    index = (ino - 1) % rd32(fs->sb.s_inodes_per_group);
    err = read_group_desc(fs, grp, &gd);
    if (err)
        return err;
    itable = gd_inode_table(fs, &gd);
    byteoff = (uint64_t)index * fs->inode_size;
    *pblk_out = itable + byteoff / fs->block_size;
    *off_out = (uint32_t)(byteoff % fs->block_size);
    return 0;
}

static int extent_lookup(struct fs *fs, const void *node, uint32_t lblk, uint64_t *pblk_out)
{
    const struct ext4_extent_header *eh = (const struct ext4_extent_header *)node;
    uint16_t entries = rd16(eh->eh_entries);
    uint16_t depth = rd16(eh->eh_depth);
    uint16_t i;

    if (rd16(eh->eh_magic) != EXT4_EXT_MAGIC)
        return EIO;
    if (depth == 0) {
        const struct ext4_extent *ex = (const struct ext4_extent *)(eh + 1);
        for (i = 0; i < entries; i++) {
            uint32_t first = rd32(ex[i].ee_block);
            uint16_t len = rd16(ex[i].ee_len);
            uint64_t start;
            if (len > 32768)
                len -= 32768;
            if (lblk < first || lblk >= first + len)
                continue;
            start = rd32(ex[i].ee_start_lo) |
                ((uint64_t)rd16(ex[i].ee_start_hi) << 32);
            *pblk_out = start + (lblk - first);
            return 0;
        }
        *pblk_out = 0;
        return 0;
    }

    {
        const struct ext4_extent_idx *ix = (const struct ext4_extent_idx *)(eh + 1);
        uint64_t child = 0;
        char *buf;
        int err;
        for (i = 0; i < entries; i++) {
            if (lblk >= rd32(ix[i].ei_block))
                child = rd32(ix[i].ei_leaf_lo) |
                    ((uint64_t)rd16(ix[i].ei_leaf_hi) << 32);
            else
                break;
        }
        if (child == 0) {
            *pblk_out = 0;
            return 0;
        }
        buf = malloc(fs->block_size);
        if (buf == NULL)
            return ENOMEM;
        err = fs_read_block(fs, child, buf);
        if (err == 0)
            err = extent_lookup(fs, buf, lblk, pblk_out);
        free(buf);
        return err;
    }
}

static int bmap(struct fs *fs, const struct ext4_inode *in, uint32_t lblk, uint64_t *pblk_out)
{
    if ((rd32(in->i_flags) & EXT4_EXTENTS_FL) == 0)
        return ENOTSUP;
    return extent_lookup(fs, in->i_block, lblk, pblk_out);
}

static char type_char(uint16_t mode)
{
    switch (mode & EXT4_S_IFMT) {
    case EXT4_S_IFREG: return '-';
    case EXT4_S_IFDIR: return 'd';
    case EXT4_S_IFLNK: return 'l';
    case EXT4_S_IFCHR: return 'c';
    case EXT4_S_IFBLK: return 'b';
    case EXT4_S_IFIFO: return 'p';
    case EXT4_S_IFSOCK: return 's';
    default: return '?';
    }
}

static int dir_lookup(struct fs *fs, const struct ext4_inode *dir, const char *name, uint32_t *ino_out)
{
    uint64_t size = inode_size(dir);
    uint64_t off;
    char *buf = malloc(fs->block_size);
    size_t nlen = strlen(name);
    int err = 0;

    if (buf == NULL)
        return ENOMEM;
    for (off = 0; off < size; off += fs->block_size) {
        uint64_t pblk = 0;
        uint32_t p = 0;
        uint32_t limit = fs->block_size;
        err = bmap(fs, dir, (uint32_t)(off / fs->block_size), &pblk);
        if (err || pblk == 0)
            continue;
        err = fs_read_block(fs, pblk, buf);
        if (err)
            break;
        if (limit >= 12) {
            const uint8_t *tail = (const uint8_t *)buf + fs->block_size - 12;
            if (tail[6] == 0 && tail[7] == EXT4_FT_DIR_CSUM && tail[4] == 12 && tail[5] == 0)
                limit -= 12;
        }
        while (p + 8 <= limit) {
            struct ext4_dir_entry_2 *de = (struct ext4_dir_entry_2 *)(buf + p);
            uint16_t reclen = rd16(de->rec_len);
            if (reclen < 8 || p + reclen > limit)
                break;
            if (rd32(de->inode) != 0 && de->name_len == nlen &&
                memcmp(de->name, name, nlen) == 0) {
                *ino_out = rd32(de->inode);
                free(buf);
                return 0;
            }
            p += reclen;
        }
    }
    free(buf);
    return err ? err : ENOENT;
}

static int resolve_path(struct fs *fs, const char *path, uint32_t *ino_out, struct ext4_inode *inode_out)
{
    struct ext4_inode cur;
    uint32_t ino = EXT4_ROOT_INO;
    const char *p = path;
    int err = read_inode(fs, ino, &cur);

    if (err)
        return err;
    while (*p == '/')
        p++;
    while (*p != '\0') {
        char name[256];
        size_t n = 0;
        while (p[n] != '\0' && p[n] != '/')
            n++;
        if (n == 0) {
            p++;
            continue;
        }
        if (n >= sizeof(name))
            return ENAMETOOLONG;
        memcpy(name, p, n);
        name[n] = '\0';
        if ((rd16(cur.i_mode) & EXT4_S_IFMT) != EXT4_S_IFDIR)
            return ENOTDIR;
        err = dir_lookup(fs, &cur, name, &ino);
        if (err)
            return err;
        err = read_inode(fs, ino, &cur);
        if (err)
            return err;
        p += n;
        while (*p == '/')
            p++;
    }
    *ino_out = ino;
    *inode_out = cur;
    return 0;
}

static void print_mode(uint16_t mode)
{
    static const unsigned bits[] = { 0400, 0200, 0100, 0040, 0020, 0010, 0004, 0002, 0001 };
    int i;
    putchar(type_char(mode));
    for (i = 0; i < 9; i++)
        putchar((mode & bits[i]) ? "rwx"[i % 3] : '-');
}

static int cmd_stat(struct fs *fs, const char *path)
{
    struct ext4_inode in;
    const struct ext4_extent_header *eh;
    uint64_t inode_pblk = 0;
    uint32_t inode_off = 0;
    uint32_t ino;
    int err = resolve_path(fs, path, &ino, &in);
    if (err)
        return err;
    err = inode_location(fs, ino, &inode_pblk, &inode_off);
    if (err)
        return err;
    printf("inode: %u\n", ino);
    printf("inode_table_block: %llu\n", (unsigned long long)inode_pblk);
    printf("inode_table_offset: %u\n", inode_off);
    printf("mode: ");
    print_mode(rd16(in.i_mode));
    printf(" 0%o\n", rd16(in.i_mode) & 07777);
    printf("uid: %u\n", rd16(in.i_uid));
    printf("gid: %u\n", rd16(in.i_gid));
    printf("size: %llu\n", (unsigned long long)inode_size(&in));
    printf("links: %u\n", rd16(in.i_links_count));
    printf("blocks512: %u\n", rd32(in.i_blocks_lo));
    printf("flags: 0x%x\n", rd32(in.i_flags));
    eh = (const struct ext4_extent_header *)in.i_block;
    if (rd32(in.i_flags) & EXT4_EXTENTS_FL) {
        printf("extent_magic: 0x%x\n", rd16(eh->eh_magic));
        printf("extent_entries: %u\n", rd16(eh->eh_entries));
        printf("extent_depth: %u\n", rd16(eh->eh_depth));
    }
    return 0;
}

static int cmd_ls(struct fs *fs, const char *path)
{
    struct ext4_inode dir;
    uint32_t ino;
    uint64_t size, off;
    char *buf;
    int err = resolve_path(fs, path, &ino, &dir);

    if (err)
        return err;
    if ((rd16(dir.i_mode) & EXT4_S_IFMT) != EXT4_S_IFDIR)
        return ENOTDIR;
    buf = malloc(fs->block_size);
    if (buf == NULL)
        return ENOMEM;
    size = inode_size(&dir);
    for (off = 0; off < size; off += fs->block_size) {
        uint64_t pblk = 0;
        uint32_t p = 0, limit = fs->block_size;
        err = bmap(fs, &dir, (uint32_t)(off / fs->block_size), &pblk);
        if (err || pblk == 0)
            continue;
        err = fs_read_block(fs, pblk, buf);
        if (err)
            break;
        if (limit >= 12) {
            const uint8_t *tail = (const uint8_t *)buf + fs->block_size - 12;
            if (tail[6] == 0 && tail[7] == EXT4_FT_DIR_CSUM && tail[4] == 12 && tail[5] == 0)
                limit -= 12;
        }
        while (p + 8 <= limit) {
            struct ext4_dir_entry_2 *de = (struct ext4_dir_entry_2 *)(buf + p);
            uint16_t reclen = rd16(de->rec_len);
            if (reclen < 8 || p + reclen > limit)
                break;
            if (rd32(de->inode) != 0) {
                printf("%10u %u %.*s\n", rd32(de->inode), de->file_type,
                    de->name_len, de->name);
            }
            p += reclen;
        }
    }
    free(buf);
    return err;
}

static void usage(void)
{
    fprintf(stderr, "usage: ext4tool stat|ls device path\n");
    fprintf(stderr, "example: ext4tool stat /dev/rdisk0s2 /usr/bin/xkbcomp\n");
}

int main(int argc, char **argv)
{
    struct fs fs;
    int err;

    if (argc != 4) {
        usage();
        return 2;
    }
    err = fs_open(&fs, argv[2]);
    if (err) {
        fprintf(stderr, "ext4tool: open %s: %s\n", argv[2], strerror(err));
        return 1;
    }
    if (strcmp(argv[1], "stat") == 0)
        err = cmd_stat(&fs, argv[3]);
    else if (strcmp(argv[1], "ls") == 0)
        err = cmd_ls(&fs, argv[3]);
    else {
        usage();
        close(fs.fd);
        return 2;
    }
    close(fs.fd);
    if (err) {
        fprintf(stderr, "ext4tool: %s: %s\n", argv[3], strerror(err));
        return 1;
    }
    return 0;
}
