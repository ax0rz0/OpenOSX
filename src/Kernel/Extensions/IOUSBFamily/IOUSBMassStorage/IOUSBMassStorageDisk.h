/*
 * IOUSBMassStorageDisk: IOBlockStorageDevice nub for USB Mass Storage
 * (bulk-only transport / SCSI) devices.
 */

#ifndef _IOUSB_MASS_STORAGE_DISK_H
#define _IOUSB_MASS_STORAGE_DISK_H

#include <IOKit/storage/IOBlockStorageDevice.h>
#include "IOUSBBOTTransport.h"

class IOUSBMassStorageDisk: public IOBlockStorageDevice
{
    OSDeclareDefaultStructors(IOUSBMassStorageDisk);

public:
    bool initWithTransport(IOUSBBOTTransport *transport, UInt32 unit);

    bool start(IOService *provider) override;
    void stop(IOService *provider) override;
    void free() override;

    IOReturn doAsyncReadWrite(
            IOMemoryDescriptor  *buffer,
            UInt64               block,
            UInt64               nblks,
            IOStorageAttributes *attributes,
            IOStorageCompletion *completion) override;

    IOReturn doSynchronize(
            UInt64 block,
            UInt64 nblks,
            IOStorageSynchronizeOptions options = 0) override;

    IOReturn doEjectMedia(void) override;
    IOReturn doFormatMedia(UInt64 byteCapacity) override;
    UInt32   doGetFormatCapacities(UInt64 *capacities,
                                   UInt32  capacitiesMaxCount) const override;

    char *getVendorString(void) override;
    char *getProductString(void) override;
    char *getRevisionString(void) override;
    char *getAdditionalDeviceInfoString(void) override;

    IOReturn reportBlockSize(UInt64 *blockSize) override;
    IOReturn reportEjectability(bool *isEjectable) override;
    IOReturn reportMaxValidBlock(UInt64 *maxBlock) override;
    IOReturn reportMediaState(bool *mediaPresent, bool *changedState = 0) override;
    IOReturn reportRemovability(bool *isRemovable) override;
    IOReturn reportWriteProtection(bool *isWriteProtected) override;

private:
    IOUSBBOTTransport *fTransport;  /* weak ref: provider outlives us */
    UInt32             fUnit;

    UInt64 fCapacityBlocks;
    UInt32 fBlockSize;

    char fVendorStr[9];
    char fProductStr[17];
    char fRevisionStr[5];

    bool scsiInquiry();
    bool scsiReadCapacity10();
    bool scsiReadWrite10(UInt64 block, UInt32 nblks, IOMemoryDescriptor *buffer,
                         UInt64 bufOff, bool write);
};

#endif /* _IOUSB_MASS_STORAGE_DISK_H */
