/*
 * A GBM-shaped veneer over PDSurface.
 *
 * GBM's shape is an accident of Linux's DRM, not something OpenOSX wants to
 * be built on - PDSurface is the real interface and this exists so software
 * that assumes GBM links and runs unmodified. Anything native should use
 * <PDSurface.h> directly.
 *
 * The one thing that cannot be honoured is file-descriptor export: GBM hands
 * out dmabuf fds, Darwin has no such object, and inventing one would only move
 * the failure somewhere less obvious. Those entry points fail cleanly, and
 * gbm_bo_get_handle() carries the PDSurface id for callers able to use it.
 */
#ifndef _GBM_H_
#define _GBM_H_

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

struct gbm_device;
struct gbm_bo;
struct gbm_surface;

#define GBM_MAX_PLANES 4

union gbm_bo_handle {
    void    *ptr;
    int32_t  s32;
    uint32_t u32;
    int64_t  s64;
    uint64_t u64;
};

enum gbm_bo_format {
    GBM_BO_FORMAT_XRGB8888,
    GBM_BO_FORMAT_ARGB8888,
};

#define __gbm_fourcc_code(a, b, c, d) \
    ((uint32_t)(a) | ((uint32_t)(b) << 8) | ((uint32_t)(c) << 16) | \
     ((uint32_t)(d) << 24))

#define GBM_FORMAT_XRGB8888 __gbm_fourcc_code('X', 'R', '2', '4')
#define GBM_FORMAT_ARGB8888 __gbm_fourcc_code('A', 'R', '2', '4')
#define GBM_FORMAT_XBGR8888 __gbm_fourcc_code('X', 'B', '2', '4')
#define GBM_FORMAT_ABGR8888 __gbm_fourcc_code('A', 'B', '2', '4')

enum gbm_bo_flags {
    GBM_BO_USE_SCANOUT      = (1 << 0),
    GBM_BO_USE_CURSOR       = (1 << 1),
    GBM_BO_USE_CURSOR_64X64 = GBM_BO_USE_CURSOR,
    GBM_BO_USE_RENDERING    = (1 << 2),
    GBM_BO_USE_WRITE        = (1 << 3),
    GBM_BO_USE_LINEAR       = (1 << 4),
    GBM_BO_USE_PROTECTED    = (1 << 5),
};

enum gbm_bo_transfer_flags {
    GBM_BO_TRANSFER_READ       = (1 << 0),
    GBM_BO_TRANSFER_WRITE      = (1 << 1),
    GBM_BO_TRANSFER_READ_WRITE = (GBM_BO_TRANSFER_READ | GBM_BO_TRANSFER_WRITE),
};

struct gbm_device *gbm_create_device(int fd);
void               gbm_device_destroy(struct gbm_device *gbm);
int                gbm_device_get_fd(struct gbm_device *gbm);
const char        *gbm_device_get_backend_name(struct gbm_device *gbm);
int                gbm_device_is_format_supported(struct gbm_device *gbm,
                                                  uint32_t format, uint32_t usage);

struct gbm_bo *gbm_bo_create(struct gbm_device *gbm, uint32_t width,
                             uint32_t height, uint32_t format, uint32_t flags);
struct gbm_bo *gbm_bo_create_with_modifiers(struct gbm_device *gbm,
                                            uint32_t width, uint32_t height,
                                            uint32_t format,
                                            const uint64_t *modifiers,
                                            unsigned int count);
struct gbm_bo *gbm_bo_create_with_modifiers2(struct gbm_device *gbm,
                                             uint32_t width, uint32_t height,
                                             uint32_t format,
                                             const uint64_t *modifiers,
                                             unsigned int count, uint32_t flags);
void gbm_bo_destroy(struct gbm_bo *bo);

uint32_t            gbm_bo_get_width(struct gbm_bo *bo);
uint32_t            gbm_bo_get_height(struct gbm_bo *bo);
uint32_t            gbm_bo_get_stride(struct gbm_bo *bo);
uint32_t            gbm_bo_get_format(struct gbm_bo *bo);
uint64_t            gbm_bo_get_modifier(struct gbm_bo *bo);
int                 gbm_bo_get_plane_count(struct gbm_bo *bo);
uint32_t            gbm_bo_get_stride_for_plane(struct gbm_bo *bo, int plane);
uint32_t            gbm_bo_get_offset(struct gbm_bo *bo, int plane);
struct gbm_device  *gbm_bo_get_device(struct gbm_bo *bo);

/* Carries the PDSurface id rather than a driver-private GEM name. */
union gbm_bo_handle gbm_bo_get_handle(struct gbm_bo *bo);
union gbm_bo_handle gbm_bo_get_handle_for_plane(struct gbm_bo *bo, int plane);

/* No dmabuf on Darwin: these always fail. See the note at the top. */
int gbm_bo_get_fd(struct gbm_bo *bo);
int gbm_bo_get_fd_for_plane(struct gbm_bo *bo, int plane);

void *gbm_bo_map(struct gbm_bo *bo, uint32_t x, uint32_t y, uint32_t width,
                 uint32_t height, uint32_t flags, uint32_t *stride,
                 void **map_data);
void  gbm_bo_unmap(struct gbm_bo *bo, void *map_data);

void  gbm_bo_set_user_data(struct gbm_bo *bo, void *data,
                           void (*destroy_user_data)(struct gbm_bo *, void *));
void *gbm_bo_get_user_data(struct gbm_bo *bo);

#ifdef __cplusplus
}
#endif

#endif /* _GBM_H_ */
