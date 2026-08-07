/*
 * Ext4FileSystemDriver.cpp - publish boot-uuid-media for an ext4 boot volume.
 * Structured after AppleFileSystemDriver, minus the HFS/APFS specifics: ext4's
 * superblock s_uuid is already a 16-byte UUID, so matching is a direct compare.
 */
#include "Ext4FileSystemDriver.h"

#include <IOKit/IOLib.h>
#include <IOKit/IOBSD.h>
#include <IOKit/IOBufferMemoryDescriptor.h>
#include <libkern/OSByteOrder.h>
#include <libkern/OSAtomic.h>

#define kClassName        "Ext4FileSystemDriver"
#define kMediaMatchKey    "media-match"
#define kBootUUIDKey      "boot-uuid"
#define kBootUUIDMediaKey "boot-uuid-media"

/* ext4 on-disk superblock: lives at byte offset 1024; s_magic (le16) at
 * offset 0x38, s_uuid (16 bytes) at offset 0x68. */
#define EXT4_SB_OFFSET    1024
#define EXT4_SB_MAGIC_OFF 0x38
#define EXT4_SB_UUID_OFF  0x68
#define EXT4_SUPER_MAGIC  0xEF53

#define E4D_LOG(fmt, args...)  kprintf(kClassName ": " fmt "\n", ## args)

#define super IOService
OSDefineMetaClassAndStructors(Ext4FileSystemDriver, IOService)

IOReturn
Ext4FileSystemDriver::readExt4UUID(IOMedia *media, uuid_t uuidOut)
{
    bool                       mediaIsOpen = false;
    UInt64                     mediaBlockSize;
    IOBufferMemoryDescriptor * buffer = NULL;
    uint8_t *                  bytes;
    UInt64                     bufferReadAt;
    UInt64                     sbInBuffer;
    vm_size_t                  bufferSize;
    IOReturn                   status = kIOReturnError;

    do {
        mediaBlockSize = media->getPreferredBlockSize();
        if (mediaBlockSize == 0) break;

        /* Read a block-aligned window covering the superblock at byte 1024. */
        bufferReadAt = IOTrunc(EXT4_SB_OFFSET, mediaBlockSize);
        sbInBuffer   = EXT4_SB_OFFSET - bufferReadAt;   /* sb offset within buffer */
        bufferSize   = IORound(sbInBuffer + 0x100, mediaBlockSize);

        buffer = IOBufferMemoryDescriptor::withCapacity(bufferSize, kIODirectionIn);
        if (buffer == NULL) break;
        bytes = (uint8_t *)buffer->getBytesNoCopy();

        mediaIsOpen = media->open(media, 0, kIOStorageAccessReader);
        if (!mediaIsOpen) break;

        status = media->read(media, bufferReadAt, buffer);
        if (status != kIOReturnSuccess) break;

        uint8_t *sb = bytes + sbInBuffer;
        uint16_t magic = OSSwapLittleToHostInt16(*(uint16_t *)(sb + EXT4_SB_MAGIC_OFF));
        if (magic != EXT4_SUPER_MAGIC) {
            status = kIOReturnBadMedia;
            break;
        }
        memcpy(uuidOut, sb + EXT4_SB_UUID_OFF, sizeof(uuid_t));
        status = kIOReturnSuccess;
    } while (false);

    if (mediaIsOpen) media->close(media);
    if (buffer)      buffer->release();
    return status;
}

bool
Ext4FileSystemDriver::mediaNotificationHandler(void * target, void * /*ref*/,
                                               IOService * service,
                                               IONotifier * /*notifier*/)
{
    Ext4FileSystemDriver *fs;
    IOMedia              *media;
    uuid_t                uuid;
    bool                  matched = false;

    do {
        fs = OSDynamicCast(Ext4FileSystemDriver, (IOService *)target);
        if (fs == NULL) break;
        media = OSDynamicCast(IOMedia, service);
        if (media == NULL) break;
        if (!media->isFormatted()) break;

        if (readExt4UUID(media, uuid) != kIOReturnSuccess) break;

        if (uuid_compare(uuid, fs->_uuid) == 0) {
            E4D_LOG("s_uuid matched on volume %s", media->getName());
            matched = true;
        }
    } while (false);

    if (matched) {
        if (OSCompareAndSwap(false, true, &fs->_matched) != true)
            return false;
        if (fs->_notifier != NULL) {
            fs->_notifier->remove();
            fs->_notifier = NULL;
        }
        E4D_LOG("publishing boot-uuid-media '%s'", media->getName());
        IOService::publishResource(kBootUUIDMediaKey, media);
        fs->getResourceService()->removeProperty(kBootUUIDKey);
        fs->terminate(kIOServiceRequired);
        fs->release();   /* drop the retain taken for async notification */
        return true;
    }
    return false;
}

bool
Ext4FileSystemDriver::start(IOService * provider)
{
    OSDictionary *matching;
    OSString     *uuidString;
    IOService    *resourceService;
    OSDictionary *dict;

    _matched = false;

    do {
        resourceService = getResourceService();
        if (resourceService == NULL) break;

        uuidString = OSDynamicCast(OSString, resourceService->getProperty(kBootUUIDKey));
        if (uuidString == NULL) {
            E4D_LOG("no boot-uuid property; nothing to match");
            break;
        }
        _uuidString = uuidString;
        _uuidString->retain();
        if (uuid_parse(uuidString->getCStringNoCopy(), _uuid) != 0) {
            E4D_LOG("invalid boot-uuid '%s'", uuidString->getCStringNoCopy());
            break;
        }

        dict = OSDynamicCast(OSDictionary, getProperty(kMediaMatchKey));
        if (dict == NULL) break;
        dict = OSDictionary::withDictionary(dict);
        if (dict == NULL) break;

        matching = IOService::serviceMatching("IOMedia", dict);
        if (matching == NULL) break;

        retain();   /* held until we match (or free) */
        _notifier = IOService::addMatchingNotification(gIOMatchedNotification, matching,
                                                       &mediaNotificationHandler, this, 0);
        matching->release();
        E4D_LOG("watching for ext4 boot volume (boot-uuid %s)",
                _uuidString->getCStringNoCopy());
        return true;
    } while (false);

    return false;
}

void
Ext4FileSystemDriver::free()
{
    if (_notifier) _notifier->remove();
    if (_uuidString) _uuidString->release();
    super::free();
}
