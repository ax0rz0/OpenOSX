#include "xf86drmPuredarwin.h"

#include <CoreFoundation/CoreFoundation.h>
#include <IOKit/IOKitLib.h>
#include <errno.h>
#include <string.h>

static const char *
device_class(const drmPuredarwinDevice *device)
{
    return device->class_name[0] ? device->class_name : "IOVirtIOGPU";
}

int
drmPuredarwinGetDevices(drmPuredarwinDevice devices[], int max_devices)
{
    io_iterator_t iterator = IO_OBJECT_NULL;
    io_object_t service;
    int count = 0;
    kern_return_t kr;

    if (max_devices < 0 || (max_devices > 0 && devices == NULL))
        return -EINVAL;

    kr = IOServiceGetMatchingServices(kIOMasterPortDefault,
                                      IOServiceMatching("IOVirtIOGPU"),
                                      &iterator);
    if (kr != KERN_SUCCESS)
        return -ENODEV;

    while ((service = IOIteratorNext(iterator)) != IO_OBJECT_NULL) {
        io_name_t name;
        io_name_t class_name;

        if (devices != NULL && count < max_devices) {
            memset(&devices[count], 0, sizeof(devices[count]));
            if (IORegistryEntryGetName(service, name) == KERN_SUCCESS)
                strncpy(devices[count].name, name,
                        sizeof(devices[count].name) - 1);
            if (IOObjectGetClass(service, class_name) == KERN_SUCCESS)
                strncpy(devices[count].class_name, class_name,
                        sizeof(devices[count].class_name) - 1);
        }
        count++;
        IOObjectRelease(service);
    }
    IOObjectRelease(iterator);

    return count;
}

int
drmPuredarwinOpen(const char *class_name, uint32_t user_client_type,
                  uint32_t *connection)
{
    io_service_t service = IO_OBJECT_NULL;
    io_connect_t connect = IO_OBJECT_NULL;
    kern_return_t kr;

    if (connection == NULL)
        return -EINVAL;
    *connection = 0;

    service = IOServiceGetMatchingService(kIOMasterPortDefault,
        IOServiceMatching(class_name ? class_name : "IOVirtIOGPU"));
    if (service == IO_OBJECT_NULL)
        return -ENODEV;

    kr = IOServiceOpen(service, mach_task_self(), user_client_type, &connect);
    IOObjectRelease(service);
    if (kr != KERN_SUCCESS)
        return -EIO;

    *connection = (uint32_t)connect;
    return 0;
}

int
drmPuredarwinClose(uint32_t connection)
{
    if (connection == 0)
        return -EINVAL;
    return IOServiceClose((io_connect_t)connection) == KERN_SUCCESS ? 0 : -EIO;
}
