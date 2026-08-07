/*
 * Ext4FileSystemDriver - boot-uuid-media publisher for ext4 volumes.
 *
 * The read-only ext4 VFS driver (ext4.kext) registers the "ext4" filesystem
 * but nothing ever mounts it: root mount blocks on the "boot-uuid-media"
 * IOResource, which is published by a driver that recognizes the boot
 * volume's on-disk filesystem and matches its UUID against the boot-uuid the
 * loader put in /chosen. AppleFileSystemDriver does exactly this for HFS/
 * APFS but only for Apple content hints. This is the ext4 analogue: it
 * matches Linux-filesystem IOMedia, reads the ext4 superblock, and publishes
 * boot-uuid-media when the superblock s_uuid equals boot-uuid.
 *
 * Unlike HFS (whose 8-byte volume id is folded into a v3 UUID via an MD5
 * namespace hash), ext4's superblock s_uuid is already a full 16-byte UUID,
 * so the match is a straight uuid_compare - no hashing. The loader
 * (xnu-loader find_ext4_boot_uuid) formats that same s_uuid into boot-uuid.
 */
#ifdef KERNEL
#ifdef __cplusplus

#include <IOKit/IOService.h>
#include <IOKit/storage/IOMedia.h>
#include <uuid/uuid.h>

class Ext4FileSystemDriver : public IOService
{
    OSDeclareDefaultStructors(Ext4FileSystemDriver)

protected:
    IONotifier *_notifier;
    uuid_t      _uuid;
    OSString   *_uuidString;
    UInt32      _matched;

public:
    virtual bool start(IOService * provider) APPLE_KEXT_OVERRIDE;
    virtual void free() APPLE_KEXT_OVERRIDE;

private:
    static bool mediaNotificationHandler(void * target, void * ref,
                                         IOService * newService,
                                         IONotifier * notifier);
    /* Read the ext4 superblock from media and copy its 16-byte s_uuid out.
     * Returns kIOReturnSuccess only if the ext4 magic is present. */
    static IOReturn readExt4UUID(IOMedia *media, uuid_t uuidOut);
};

#endif /* __cplusplus */
#endif /* KERNEL */
