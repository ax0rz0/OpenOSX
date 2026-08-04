/* PureDarwin device discovery for libdrm consumers. */
#ifndef XF86DRM_PUREDARWIN_H
#define XF86DRM_PUREDARWIN_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define DRM_PUREDARWIN_NAME_MAX 128

typedef struct drmPuredarwinDevice {
    char name[DRM_PUREDARWIN_NAME_MAX];
    char class_name[DRM_PUREDARWIN_NAME_MAX];
} drmPuredarwinDevice;

/* Returns the number of matching devices, or a negative errno-style result. */
int drmPuredarwinGetDevices(drmPuredarwinDevice devices[], int max_devices);

/* Open a PureDarwin IOKit user client. The returned value is a Mach port,
 * intentionally distinct from the Linux DRM fd APIs. */
int drmPuredarwinOpen(const char *class_name, uint32_t user_client_type,
                      uint32_t *connection);
int drmPuredarwinClose(uint32_t connection);

#ifdef __cplusplus
}
#endif

#endif /* XF86DRM_PUREDARWIN_H */
