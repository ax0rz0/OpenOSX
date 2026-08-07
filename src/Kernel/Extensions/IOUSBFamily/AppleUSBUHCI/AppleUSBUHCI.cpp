#include <IOKit/IOLib.h>
#include <IOKit/pci/IOPCIDevice.h>
#include <IOKit/usb/IOUSBControllerV3.h>
#include <architecture/i386/pio.h>

#include "UHCI.h"

#define super IOUSBControllerV3

enum {
    kUHCI_BAR = 0x20
};

class AppleUSBUHCI : public IOUSBControllerV3
{
    OSDeclareDefaultStructors(AppleUSBUHCI)

public:
    virtual bool init(OSDictionary *properties) override;
    virtual bool start(IOService *provider) override;
    virtual void stop(IOService *provider) override;
    virtual void free() override;

    virtual IOReturn UIMInitialize(IOService *provider);
    virtual IOReturn UIMFinalize();
    virtual IOReturn UIMOpenPipe(USBDeviceAddress address, UInt8 speed, Endpoint *endpoint) override;
    virtual IOReturn UIMClosePipe(USBDeviceAddress address, Endpoint *endpoint) override;
    virtual IOReturn UIMAbortPipe(USBDeviceAddress address, Endpoint *endpoint) override;
    virtual IOReturn UIMClearPipeStall(USBDeviceAddress address, Endpoint *endpoint) override;
    virtual IOReturn UIMDeviceRequest(IOUSBDevRequest *request, USBDeviceAddress address) override;
    virtual IOReturn UIMReadWrite(IOMemoryDescriptor *buffer, USBDeviceAddress address,
                                  Endpoint *endpoint, bool isWrite) override;
    virtual IOReturn UIMCreateControlEndpoint(UInt8 functionNumber, UInt8 endpointNumber,
                                              UInt16 maxPacketSize, UInt8 speed,
                                              USBDeviceAddress highSpeedHub,
                                              int highSpeedPort) override;
    virtual IOReturn UIMCreateBulkEndpoint(UInt8 functionNumber, UInt8 endpointNumber,
                                           UInt8 direction, UInt8 speed,
                                           UInt16 maxPacketSize,
                                           USBDeviceAddress highSpeedHub,
                                           int highSpeedPort) override;
    virtual IOReturn UIMCreateInterruptEndpoint(short functionAddress,
                                                short endpointNumber, UInt8 direction,
                                                short speed, UInt16 maxPacketSize,
                                                short pollingRate,
                                                USBDeviceAddress highSpeedHub,
                                                int highSpeedPort) override;
    virtual IOReturn UIMCreateIsochEndpoint(short functionAddress, short endpointNumber,
                                            UInt32 maxPacketSize, UInt8 direction,
                                            USBDeviceAddress highSpeedHub,
                                            int highSpeedPort) override;
    virtual UInt32 GetBandwidthAvailable();
    virtual UInt64 GetFrameNumber();
    virtual UInt32 GetFrameNumber32();
    virtual IOReturn GetFrameNumberWithTime(UInt64 *frameNumber, AbsoluteTime *theTime);
    static IOReturn GatedGetFrameNumberWithTime(OSObject *owner, void *arg0,
                                                void *arg1, void *arg2, void *arg3);

private:
    IOPCIDevice *fDevice;
    IOPhysicalAddress fIOBase;
    bool fInitialized;

    UInt16 read16(UInt16 offset);
    void write16(UInt16 offset, UInt16 value);
    bool findBAR4(IOPCIDevice *device, IOPhysicalAddress *baseOut);
};

OSDefineMetaClassAndStructors(AppleUSBUHCI, IOUSBControllerV3)

static void UHCI_Log(const char *fmt, ...)
{
    char buf[256];
    va_list args;
    va_start(args, fmt);
    vsnprintf(buf, sizeof(buf), fmt, args);
    va_end(args);
    IOLog("AppleUSBUHCI: %s\n", buf);
}

bool AppleUSBUHCI::init(OSDictionary *properties)
{
    if (!super::init(properties))
        return false;
    fDevice = NULL;
    fIOBase = 0;
    fInitialized = false;
    return true;
}

bool AppleUSBUHCI::start(IOService *provider)
{
    if (!super::start(provider))
        return false;

    fDevice = OSDynamicCast(IOPCIDevice, provider);
    if (!fDevice)
        return false;
    fDevice->retain();
    fDevice->setIOEnable(true);
    fDevice->setBusMasterEnable(true);

    if (UIMInitialize(provider) != kIOReturnSuccess) {
        UHCI_Log("UIMInitialize failed");
        return false;
    }

    registerService();
    return true;
}

void AppleUSBUHCI::stop(IOService *provider)
{
    UIMFinalize();
    super::stop(provider);
}

void AppleUSBUHCI::free()
{
    if (fDevice) {
        fDevice->release();
        fDevice = NULL;
    }
    super::free();
}

bool AppleUSBUHCI::findBAR4(IOPCIDevice *device, IOPhysicalAddress *baseOut)
{
    if (!device || !baseOut)
        return false;

    UInt32 bar = device->configRead32(kUHCI_BAR);
    if ((bar & 1U) && (bar & ~0x3U) && bar != 0xffffffffU) {
        *baseOut = (IOPhysicalAddress)(bar & ~0x3U);
        return true;
    }

    OSData *assigned = OSDynamicCast(OSData, device->copyProperty("assigned-addresses"));
    if (assigned) {
        const UInt8 *bytes = (const UInt8 *)assigned->getBytesNoCopy();
        UInt32 length = (UInt32)assigned->getLength();
        for (UInt32 off = 0; off + sizeof(IOPCIPhysicalAddress) <= length;
             off += sizeof(IOPCIPhysicalAddress)) {
            const IOPCIPhysicalAddress *addr =
                (const IOPCIPhysicalAddress *)(bytes + off);
            if (addr->physHi.s.registerNum != kUHCI_BAR)
                continue;
            UInt64 base = ((UInt64)addr->physMid << 32) | addr->physLo;
            if (addr->physHi.s.space == 1 && base) {
                *baseOut = (IOPhysicalAddress)(base & ~0x3ULL);
                assigned->release();
                return true;
            }
        }
        assigned->release();
    }

    return false;
}

UInt16 AppleUSBUHCI::read16(UInt16 offset)
{
    return inw((unsigned short)(fIOBase + offset));
}

void AppleUSBUHCI::write16(UInt16 offset, UInt16 value)
{
    outw((unsigned short)(fIOBase + offset), value);
}

IOReturn AppleUSBUHCI::UIMInitialize(IOService *)
{
    if (fInitialized)
        return kIOReturnSuccess;
    if (!findBAR4(fDevice, &fIOBase)) {
        UHCI_Log("failed to find BAR4 I/O base");
        return kIOReturnNoMemory;
    }

    write16(kUHCI_INTR, 0);
    write16(kUHCI_STS, kUHCI_STS_MASK);

    UInt16 cmd = read16(kUHCI_CMD);
    UInt16 sts = read16(kUHCI_STS);
    UInt16 port1 = read16(kUHCI_PORTSC1);
    UInt16 port2 = read16(kUHCI_PORTSC2);
    UHCI_Log("started io=0x%llx cmd=%04x sts=%04x port1=%04x port2=%04x",
             (unsigned long long)fIOBase, cmd, sts, port1, port2);

    fInitialized = true;
    return kIOReturnSuccess;
}

IOReturn AppleUSBUHCI::UIMFinalize()
{
    if (fInitialized && fIOBase) {
        write16(kUHCI_INTR, 0);
        write16(kUHCI_CMD, read16(kUHCI_CMD) & ~(UInt16)kUHCI_CMD_RS);
    }
    fInitialized = false;
    return kIOReturnSuccess;
}

IOReturn AppleUSBUHCI::UIMOpenPipe(USBDeviceAddress, UInt8, Endpoint *)
{
    return kIOReturnUnsupported;
}

IOReturn AppleUSBUHCI::UIMClosePipe(USBDeviceAddress, Endpoint *)
{
    return kIOReturnSuccess;
}

IOReturn AppleUSBUHCI::UIMAbortPipe(USBDeviceAddress, Endpoint *)
{
    return kIOReturnSuccess;
}

IOReturn AppleUSBUHCI::UIMClearPipeStall(USBDeviceAddress, Endpoint *)
{
    return kIOReturnSuccess;
}

IOReturn AppleUSBUHCI::UIMDeviceRequest(IOUSBDevRequest *, USBDeviceAddress)
{
    return kIOReturnUnsupported;
}

IOReturn AppleUSBUHCI::UIMReadWrite(IOMemoryDescriptor *, USBDeviceAddress,
                                    Endpoint *, bool)
{
    return kIOReturnUnsupported;
}

IOReturn AppleUSBUHCI::UIMCreateControlEndpoint(UInt8, UInt8, UInt16, UInt8,
                                                USBDeviceAddress, int)
{
    return kIOReturnUnsupported;
}

IOReturn AppleUSBUHCI::UIMCreateBulkEndpoint(UInt8, UInt8, UInt8, UInt8, UInt16,
                                             USBDeviceAddress, int)
{
    return kIOReturnUnsupported;
}

IOReturn AppleUSBUHCI::UIMCreateInterruptEndpoint(short, short, UInt8, short,
                                                  UInt16, short,
                                                  USBDeviceAddress, int)
{
    return kIOReturnUnsupported;
}

IOReturn AppleUSBUHCI::UIMCreateIsochEndpoint(short, short, UInt32, UInt8,
                                              USBDeviceAddress, int)
{
    return kIOReturnUnsupported;
}

UInt32 AppleUSBUHCI::GetBandwidthAvailable()
{
    return 0;
}

UInt64 AppleUSBUHCI::GetFrameNumber()
{
    return fInitialized ? (UInt64)(read16(kUHCI_FRNUM) & kUHCI_FRNUM_MASK) : 0;
}

UInt32 AppleUSBUHCI::GetFrameNumber32()
{
    return (UInt32)GetFrameNumber();
}

IOReturn AppleUSBUHCI::GetFrameNumberWithTime(UInt64 *frameNumber, AbsoluteTime *theTime)
{
    if (frameNumber)
        *frameNumber = GetFrameNumber();
    if (theTime)
        clock_get_uptime(theTime);
    return kIOReturnSuccess;
}

IOReturn AppleUSBUHCI::GatedGetFrameNumberWithTime(OSObject *owner, void *arg0,
                                                   void *arg1, void *, void *)
{
    AppleUSBUHCI *me = OSDynamicCast(AppleUSBUHCI, owner);
    if (!me)
        return kIOReturnBadArgument;
    return me->GetFrameNumberWithTime((UInt64 *)arg0, (AbsoluteTime *)arg1);
}
