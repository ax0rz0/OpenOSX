#include <gbm.h>

#include <PDSurface.h>

#include <stdlib.h>
#include <string.h>

struct gbm_device {
    PDSurfaceDeviceRef device;
    int                fd;   /* whatever the caller passed in; unused */
};

struct gbm_bo {
    struct gbm_device *gbm;
    PDSurfaceRef       surface;
    void             (*destroy_user_data)(struct gbm_bo *, void *);
    void              *user_data;
};

static uint32_t
pd_format_from_gbm(uint32_t format)
{
    switch (format) {
    case GBM_FORMAT_ARGB8888: return kPDSurfaceFormatARGB8888;
    case GBM_FORMAT_XRGB8888: return kPDSurfaceFormatXRGB8888;
    default:                  return 0;
    }
}

static uint32_t
pd_usage_from_gbm(uint32_t flags)
{
    /* Everything here is linear: PDSurface has no tiling concept, and a caller
     * that did not ask for LINEAR still expects a stride it can reason about. */
    uint32_t usage = kPDSurfaceUsageLinear;
    if (flags & GBM_BO_USE_SCANOUT)
        usage |= kPDSurfaceUsageScanout;
    if (flags & GBM_BO_USE_CURSOR)
        usage |= kPDSurfaceUsageCursor;
    if (flags & GBM_BO_USE_RENDERING)
        usage |= kPDSurfaceUsageRender;
    return usage;
}

struct gbm_device *
gbm_create_device(int fd)
{
    struct gbm_device *gbm = calloc(1, sizeof(*gbm));
    if (gbm == NULL)
        return NULL;

    /* The fd is meaningless here - there is no DRM node behind it - but it is
     * kept so gbm_device_get_fd() returns what the caller handed over. */
    gbm->fd = fd;
    if (PDSurfaceDeviceOpen(&gbm->device) != KERN_SUCCESS) {
        free(gbm);
        return NULL;
    }
    return gbm;
}

void
gbm_device_destroy(struct gbm_device *gbm)
{
    if (gbm == NULL)
        return;
    PDSurfaceDeviceClose(gbm->device);
    free(gbm);
}

int
gbm_device_get_fd(struct gbm_device *gbm)
{
    return gbm != NULL ? gbm->fd : -1;
}

const char *
gbm_device_get_backend_name(struct gbm_device *gbm)
{
    (void)gbm;
    return "puredarwin";
}

int
gbm_device_is_format_supported(struct gbm_device *gbm, uint32_t format,
                               uint32_t usage)
{
    (void)gbm;
    (void)usage;
    return pd_format_from_gbm(format) != 0;
}

struct gbm_bo *
gbm_bo_create(struct gbm_device *gbm, uint32_t width, uint32_t height,
              uint32_t format, uint32_t flags)
{
    if (gbm == NULL)
        return NULL;

    PDSurfaceDescriptor descriptor = {
        .width = width,
        .height = height,
        .format = pd_format_from_gbm(format),
        .usage = pd_usage_from_gbm(flags),
    };
    if (descriptor.format == 0)
        return NULL;

    struct gbm_bo *bo = calloc(1, sizeof(*bo));
    if (bo == NULL)
        return NULL;

    if (PDSurfaceCreate(gbm->device, &descriptor, &bo->surface) != KERN_SUCCESS) {
        free(bo);
        return NULL;
    }
    bo->gbm = gbm;
    return bo;
}

/* Modifiers describe tiling layouts, which PDSurface does not have. Callers
 * offering a modifier list are answered with the linear buffer they would have
 * got anyway rather than being failed outright. */
struct gbm_bo *
gbm_bo_create_with_modifiers(struct gbm_device *gbm, uint32_t width,
                             uint32_t height, uint32_t format,
                             const uint64_t *modifiers, unsigned int count)
{
    (void)modifiers;
    (void)count;
    return gbm_bo_create(gbm, width, height, format, GBM_BO_USE_LINEAR);
}

struct gbm_bo *
gbm_bo_create_with_modifiers2(struct gbm_device *gbm, uint32_t width,
                              uint32_t height, uint32_t format,
                              const uint64_t *modifiers, unsigned int count,
                              uint32_t flags)
{
    (void)modifiers;
    (void)count;
    return gbm_bo_create(gbm, width, height, format, flags | GBM_BO_USE_LINEAR);
}

void
gbm_bo_destroy(struct gbm_bo *bo)
{
    if (bo == NULL)
        return;
    if (bo->destroy_user_data != NULL)
        bo->destroy_user_data(bo, bo->user_data);
    PDSurfaceRelease(bo->surface);
    free(bo);
}

uint32_t gbm_bo_get_width(struct gbm_bo *bo)
{
    return bo != NULL ? PDSurfaceGetWidth(bo->surface) : 0;
}

uint32_t gbm_bo_get_height(struct gbm_bo *bo)
{
    return bo != NULL ? PDSurfaceGetHeight(bo->surface) : 0;
}

uint32_t gbm_bo_get_stride(struct gbm_bo *bo)
{
    return bo != NULL ? PDSurfaceGetStride(bo->surface) : 0;
}

uint32_t gbm_bo_get_format(struct gbm_bo *bo)
{
    if (bo == NULL)
        return 0;
    return PDSurfaceGetFormat(bo->surface) == kPDSurfaceFormatARGB8888 ?
        GBM_FORMAT_ARGB8888 : GBM_FORMAT_XRGB8888;
}

uint64_t gbm_bo_get_modifier(struct gbm_bo *bo)
{
    (void)bo;
    return 0; /* DRM_FORMAT_MOD_LINEAR */
}

int gbm_bo_get_plane_count(struct gbm_bo *bo)
{
    return bo != NULL ? 1 : 0;
}

uint32_t gbm_bo_get_stride_for_plane(struct gbm_bo *bo, int plane)
{
    return plane == 0 ? gbm_bo_get_stride(bo) : 0;
}

uint32_t gbm_bo_get_offset(struct gbm_bo *bo, int plane)
{
    (void)bo;
    (void)plane;
    return 0;
}

struct gbm_device *gbm_bo_get_device(struct gbm_bo *bo)
{
    return bo != NULL ? bo->gbm : NULL;
}

union gbm_bo_handle
gbm_bo_get_handle(struct gbm_bo *bo)
{
    union gbm_bo_handle handle;
    memset(&handle, 0, sizeof(handle));
    if (bo != NULL)
        handle.u64 = PDSurfaceGetID(bo->surface);
    return handle;
}

union gbm_bo_handle
gbm_bo_get_handle_for_plane(struct gbm_bo *bo, int plane)
{
    union gbm_bo_handle handle;
    memset(&handle, 0, sizeof(handle));
    if (plane == 0)
        return gbm_bo_get_handle(bo);
    return handle;
}

int gbm_bo_get_fd(struct gbm_bo *bo)
{
    (void)bo;
    return -1;
}

int gbm_bo_get_fd_for_plane(struct gbm_bo *bo, int plane)
{
    (void)bo;
    (void)plane;
    return -1;
}

void *
gbm_bo_map(struct gbm_bo *bo, uint32_t x, uint32_t y, uint32_t width,
           uint32_t height, uint32_t flags, uint32_t *stride, void **map_data)
{
    (void)width;
    (void)height;
    (void)flags;

    if (bo == NULL || stride == NULL || map_data == NULL)
        return NULL;

    void *base = PDSurfaceGetBaseAddress(bo->surface);
    if (base == NULL)
        return NULL;

    /* The surface is mapped for its whole lifetime, so there is nothing to
     * tear down and map_data only has to be non-NULL. */
    *stride = PDSurfaceGetStride(bo->surface);
    *map_data = bo;
    return (uint8_t *)base + (size_t)y * (*stride) + (size_t)x * 4;
}

void
gbm_bo_unmap(struct gbm_bo *bo, void *map_data)
{
    (void)bo;
    (void)map_data;
}

void
gbm_bo_set_user_data(struct gbm_bo *bo, void *data,
                     void (*destroy_user_data)(struct gbm_bo *, void *))
{
    if (bo == NULL)
        return;
    bo->user_data = data;
    bo->destroy_user_data = destroy_user_data;
}

void *
gbm_bo_get_user_data(struct gbm_bo *bo)
{
    return bo != NULL ? bo->user_data : NULL;
}
