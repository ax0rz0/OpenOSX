#ifndef APFSRW_APFSRW_H
#define APFSRW_APFSRW_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define APFSRW_BLOCK_SIZE 4096U
#define APFSRW_ROOT_FILEID 2ULL

enum apfsrw_error {
    APFSRW_OK = 0,
    APFSRW_EINVAL = -1,
    APFSRW_EIO = -2,
    APFSRW_ENOMEM = -3,
    APFSRW_ENOENT = -4,
    APFSRW_ENOTSUP = -5,
    APFSRW_EOVERFLOW = -6,
};

enum apfsrw_dirent_type {
    APFSRW_DT_UNKNOWN = 0,
    APFSRW_DT_REG = 8,
    APFSRW_DT_DIR = 4,
};

struct apfsrw;

struct apfsrw_volume_info {
    uint32_t block_size;
    uint64_t block_count;
    uint64_t xid;
    uint64_t fs_oid;
    uint64_t fs_paddr;
    uint64_t root_tree_oid;
    uint64_t root_tree_paddr;
    uint64_t next_obj_id;
    uint64_t num_files;
    uint64_t num_directories;
};

struct apfsrw_dirent {
    uint64_t file_id;
    uint8_t type;
    char name[256];
};

typedef int (*apfsrw_dirent_cb)(const struct apfsrw_dirent *entry,
    void *ctx);

int apfsrw_open(const char *path, int writable, struct apfsrw **out);
void apfsrw_close(struct apfsrw *fs);
const char *apfsrw_strerror(int error);

int apfsrw_get_volume_info(struct apfsrw *fs,
    struct apfsrw_volume_info *info);
int apfsrw_list_root(struct apfsrw *fs, apfsrw_dirent_cb cb, void *ctx);
int apfsrw_read_root_file(struct apfsrw *fs, const char *path,
    uint8_t **data_out, size_t *size_out);

int apfsrw_create_root_file(struct apfsrw *fs, const char *name,
    const void *data, size_t size);

#ifdef __cplusplus
}
#endif

#endif /* APFSRW_APFSRW_H */
