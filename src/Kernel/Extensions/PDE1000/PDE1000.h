/*
 * PDE1000: minimal IOEthernetController driver for the Intel 8254x family,
 * targeting QEMU's "e1000-82545em" emulated NIC (vendor 0x8086, device
 * 0x100F). No Apple/Intel driver source for this family was ever released
 * publicly, so this is written from the public Intel 8254x programmer's
 * datasheet register layout, not derived from any Apple driver.
 *
 * RX is timer-polled, with MSI used as an optional fast path when QEMU/the
 * platform routes it correctly. outputPacket() sends synchronously from the
 * calling thread.
 */

#ifndef _PDE1000_H
#define _PDE1000_H

#include <IOKit/IOService.h>
#include <IOKit/IOBufferMemoryDescriptor.h>
#include <IOKit/IOTimerEventSource.h>
#include <IOKit/IOInterruptEventSource.h>
#include <IOKit/IOWorkLoop.h>
#include <IOKit/pci/IOPCIDevice.h>
#include <IOKit/network/IOEthernetController.h>
#include <IOKit/network/IOEthernetInterface.h>
#include <IOKit/network/IOGatedOutputQueue.h>

#define kPDE1000RxDescCount   32
#define kPDE1000TxDescCount   32
#define kPDE1000RxBufferSize  2048

struct PDE1000RxDesc {
    volatile uint64_t addr;
    volatile uint16_t length;
    volatile uint16_t checksum;
    volatile uint8_t  status;
    volatile uint8_t  errors;
    volatile uint16_t special;
} __attribute__((packed));

struct PDE1000TxDesc {
    volatile uint64_t addr;
    volatile uint16_t length;
    volatile uint8_t  cso;
    volatile uint8_t  cmd;
    volatile uint8_t  status;
    volatile uint8_t  css;
    volatile uint16_t special;
} __attribute__((packed));

class PDE1000 : public IOEthernetController
{
    OSDeclareDefaultStructors(PDE1000);

public:
    bool init(OSDictionary *properties) override;
    void free() override;
    IOService *probe(IOService *provider, SInt32 *score) override;
    bool start(IOService *provider) override;
    void stop(IOService *provider) override;

    IOReturn enable(IONetworkInterface *interface) override;
    IOReturn disable(IONetworkInterface *interface) override;

    UInt32 outputPacket(mbuf_t m, void *param) override;

    IOReturn getHardwareAddress(IOEthernetAddress *addr) override;
    IOReturn setPromiscuousMode(bool active) override;
    IOReturn setMulticastMode(bool active) override;
    IOReturn setMulticastList(IOEthernetAddress *addrs, UInt32 count) override;

    const OSString *newVendorString() const override;
    const OSString *newModelString() const override;

    IOOutputQueue *createOutputQueue() override;

private:
    IOPCIDevice          *fPCIDevice;
    IOMemoryMap           *fRegMap;
    IOMemoryDescriptor    *fRegDesc;   // set only when BAR0 mapped via config fallback
    volatile uint32_t     *fRegs;      // MMIO BAR0, 32-bit register space
    IOWorkLoop            *fWorkLoop;
    IOTimerEventSource     *fPollTimer;      // fallback only; used if MSI registration fails
    IOInterruptEventSource *fInterruptSource;
    IOEthernetInterface    *fInterface;

    IOBufferMemoryDescriptor *fRxDescBuf;
    IOBufferMemoryDescriptor *fRxPacketBuf[kPDE1000RxDescCount];
    PDE1000RxDesc            *fRxDesc;
    uint32_t                  fRxTail;
    uint32_t                  fRxPackets;
    uint32_t                  fRxLogBudget;

    IOBufferMemoryDescriptor *fTxDescBuf;
    IOBufferMemoryDescriptor *fTxPacketBuf[kPDE1000TxDescCount];
    PDE1000TxDesc             *fTxDesc;
    uint32_t                  fTxTail;
    uint32_t                  fTxPackets;
    uint32_t                  fTxLogBudget;

    IOEthernetAddress fMACAddress;
    bool               fEnabled;

    uint32_t regRead(uint32_t offset);
    void     regWrite(uint32_t offset, uint32_t value);
    uint16_t eepromRead(uint8_t addr);
    bool     readMACAddress();
    bool     initRxRing();
    bool     initTxRing();
    void     pollReceive();
    bool     publishLinkMedium();
    static void pollTimerAction(OSObject *owner, IOTimerEventSource *sender);
    void     interruptOccurred(IOInterruptEventSource *sender, int count);
    static void interruptOccurredStatic(OSObject *owner, IOInterruptEventSource *sender, int count);
};

#endif /* !_PDE1000_H */
