/*
 * IOUSBBOTTransport: how IOUSBMassStorageDisk issues a bulk-only transport
 * command without knowing which controller stack carries it.
 */

#ifndef _IOUSB_BOT_TRANSPORT_H
#define _IOUSB_BOT_TRANSPORT_H

#include <IOKit/IOMemoryDescriptor.h>
#include <IOKit/IOReturn.h>

class IOUSBBOTTransport
{
public:
    virtual ~IOUSBBOTTransport() { }

    /*
     * Runs one command to completion. cb/cbLen is the SCSI CDB. dataLen may be
     * zero for commands with no data stage; when it is not, dataIn selects the
     * direction and the bytes are read from or written to buffer starting at
     * bufOff. Returns kIOReturnSuccess only if the CSW reported the command
     * good and the expected byte count moved.
     */
    virtual IOReturn botTransfer(const void *cb, UInt8 cbLen,
                                 UInt32 dataLen, bool dataIn,
                                 IOMemoryDescriptor *buffer, UInt64 bufOff) = 0;
};

#endif /* _IOUSB_BOT_TRANSPORT_H */
