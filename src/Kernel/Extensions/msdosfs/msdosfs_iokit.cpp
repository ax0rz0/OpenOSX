#include <IOKit/IOService.h>
#include <mach/kmod.h>

extern "C" {
    int msdosfs_module_start(kmod_info_t *ki, void *data);
    int msdosfs_module_stop(kmod_info_t *ki, void *data);
}

class com_apple_filesystems_msdosfs : public IOService
{
    OSDeclareDefaultStructors(com_apple_filesystems_msdosfs)
public:
    virtual bool start(IOService *provider) APPLE_KEXT_OVERRIDE;
    virtual void stop(IOService *provider) APPLE_KEXT_OVERRIDE;
};

#define super IOService
OSDefineMetaClassAndStructors(com_apple_filesystems_msdosfs, IOService)

bool
com_apple_filesystems_msdosfs::start(IOService *provider)
{
    if (!super::start(provider))
        return false;
    if (msdosfs_module_start(NULL, NULL) != KERN_SUCCESS)
        return false;
    registerService();
    return true;
}

void
com_apple_filesystems_msdosfs::stop(IOService *provider)
{
    msdosfs_module_stop(NULL, NULL);
    super::stop(provider);
}
