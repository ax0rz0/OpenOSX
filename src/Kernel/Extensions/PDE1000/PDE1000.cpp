/*
 * PDE1000: minimal Intel 8254x (e1000) driver. See PDE1000.h for scope notes.
 * Register offsets/bits below are from the public Intel 8254x GbE Controller
 * software developer's manual (PCI/PCIe register map), not from any Apple
 * source - none was ever released for this family.
 */

#include "PDE1000.h"
#include <IOKit/assert.h>
#include <IOKit/IOLib.h>
#include <IOKit/network/IOMbufMemoryCursor.h>
#include <IOKit/network/IONetworkMedium.h>
#include <libkern/OSByteOrder.h>

#define super IOEthernetController

OSDefineMetaClassAndStructors(PDE1000, IOEthernetController);

// ---- Register offsets (byte offsets into BAR0 MMIO space) ----
enum {
    REG_CTRL     = 0x0000,
    REG_STATUS   = 0x0008,
    REG_EECD     = 0x0010,
    REG_EERD     = 0x0014,
    REG_ICR      = 0x00C0,
    REG_IMS      = 0x00D0,
    REG_IMC      = 0x00D8,
    REG_RCTL     = 0x0100,
    REG_TCTL     = 0x0400,
    REG_TIPG     = 0x0410,
    REG_RDBAL    = 0x2800,
    REG_RDBAH    = 0x2804,
    REG_RDLEN    = 0x2808,
    REG_RDH      = 0x2810,
    REG_RDT      = 0x2818,
    REG_TDBAL    = 0x3800,
    REG_TDBAH    = 0x3804,
    REG_TDLEN    = 0x3808,
    REG_TDH      = 0x3810,
    REG_TDT      = 0x3818,
    REG_RAL0     = 0x5400,
    REG_RAH0     = 0x5404,
    REG_MTA      = 0x5200,
};

enum {
    CTRL_RST  = (1u << 26),
    CTRL_SLU  = (1u << 6),
    CTRL_ASDE = (1u << 5),
};

enum {
    RCTL_EN     = (1u << 1),
    RCTL_UPE    = (1u << 3),
    RCTL_MPE    = (1u << 4),
    RCTL_BAM    = (1u << 15),
    RCTL_BSIZE_2048 = 0,
    RCTL_SECRC  = (1u << 26),
};

enum {
    TCTL_EN  = (1u << 1),
    TCTL_PSP = (1u << 3),
};

enum {
    EERD_START = (1u << 0),
    EERD_DONE  = (1u << 4),
};

enum {
    TXDESC_CMD_EOP = (1u << 0),
    TXDESC_CMD_IFCS = (1u << 1),
    TXDESC_CMD_RS  = (1u << 3),
    TXDESC_STATUS_DD = (1u << 0),
    RXDESC_STATUS_DD = (1u << 0),
    RXDESC_STATUS_EOP = (1u << 1),
};

static uint16_t readBE16(const uint8_t *p)
{
    return (uint16_t)((p[0] << 8) | p[1]);
}

static uint32_t readBE32(const uint8_t *p)
{
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) |
        ((uint32_t)p[2] << 8) | p[3];
}

static void logEtherFrame(const char *dir, uint32_t seq, const void *data, size_t len)
{
    const uint8_t *p = (const uint8_t *)data;
    uint16_t type;

    if (len < 14) {
        IOLog("PDE1000: %s[%u] short len=%lu\n", dir, seq, (unsigned long)len);
        return;
    }

    type = readBE16(p + 12);
    IOLog("PDE1000: %s[%u] len=%lu dst=%02x:%02x:%02x:%02x:%02x:%02x "
          "src=%02x:%02x:%02x:%02x:%02x:%02x type=0x%04x\n",
        dir, seq, (unsigned long)len,
        p[0], p[1], p[2], p[3], p[4], p[5],
        p[6], p[7], p[8], p[9], p[10], p[11], type);

    if (type == 0x0806 && len >= 42) {
        IOLog("PDE1000: %s[%u] ARP op=%u sender=%u.%u.%u.%u target=%u.%u.%u.%u\n",
            dir, seq, readBE16(p + 20),
            p[28], p[29], p[30], p[31], p[38], p[39], p[40], p[41]);
    } else if (type == 0x0800 && len >= 34) {
        IOLog("PDE1000: %s[%u] IPv4 proto=%u %u.%u.%u.%u -> %u.%u.%u.%u\n",
            dir, seq, p[23],
            p[26], p[27], p[28], p[29], p[30], p[31], p[32], p[33]);
    }
}

bool PDE1000::init(OSDictionary *properties)
{
    if (!super::init(properties))
        return false;

    fPCIDevice = NULL;
    fRegMap = NULL;
    fRegDesc = NULL;
    fRegs = NULL;
    fWorkLoop = NULL;
    fPollTimer = NULL;
    fInterruptSource = NULL;
    fInterface = NULL;
    fRxDescBuf = NULL;
    fTxDescBuf = NULL;
    fRxDesc = NULL;
    fTxDesc = NULL;
    fRxTail = 0;
    fRxPackets = 0;
    fRxLogBudget = 32;
    fTxTail = 0;
    fTxPackets = 0;
    fTxLogBudget = 32;
    fEnabled = false;
    bzero(&fMACAddress, sizeof(fMACAddress));
    bzero(fRxPacketBuf, sizeof(fRxPacketBuf));
    bzero(fTxPacketBuf, sizeof(fTxPacketBuf));

    return true;
}

IOService *PDE1000::probe(IOService *provider, SInt32 *score)
{
    IOPCIDevice *pci = OSDynamicCast(IOPCIDevice, provider);
    if (!pci)
        return NULL;

    UInt16 vendor = pci->configRead16(kIOPCIConfigVendorID);
    UInt16 device = pci->configRead16(kIOPCIConfigDeviceID);

    IOLog("PDE1000: probe on network-class PCI device vendor=0x%04x device=0x%04x\n",
        vendor, device);

    // Intel 8254x family - matches QEMU's -device e1000-82545em (82545EM
    // Copper, 0x100F) and related e1000 device IDs it might be run with.
    if (vendor != 0x8086)
        return NULL;
    if (device != 0x100E && device != 0x100F && device != 0x1004 &&
        device != 0x1019 && device != 0x1010 && device != 0x1011)
        return NULL;

    if (score)
        *score = 5000;
    return this;
}

// Read a 32/64-bit memory BAR's base and size directly from config space using
// the standard sizing dance (write all-ones, read back the mask, restore). Needed
// because QEMU's IODeviceMemory ranges are often mis-tagged or absent. Adapted
// from RavynAHCIPort's readMemoryBARBaseAndSize.
static bool readMemoryBAR(IOPCIDevice *pci, UInt8 reg, uint64_t *outBase, uint64_t *outSize)
{
    if (!pci || !outBase || !outSize) return false;

    const uint16_t savedCmd = pci->configRead16(kIOPCIConfigCommand);
    const uint32_t savedLo  = pci->configRead32(reg);
    uint32_t savedHi = 0;

    if (savedLo & 0x1)              // I/O space BAR, not memory
        return false;

    const bool is64 = ((savedLo & 0x6) == 0x4);
    if (is64) {
        if (reg > kIOPCIConfigBaseAddress4) return false;
        savedHi = pci->configRead32(reg + 4);
    }

    // Disable decode while sizing, then restore.
    pci->configWrite16(kIOPCIConfigCommand, savedCmd & ~(uint16_t)0x3);
    pci->configWrite32(reg, 0xffffffffU);
    if (is64) pci->configWrite32(reg + 4, 0xffffffffU);

    const uint32_t maskLo = pci->configRead32(reg);
    const uint32_t maskHi = is64 ? pci->configRead32(reg + 4) : 0xffffffffU;

    pci->configWrite32(reg, savedLo);
    if (is64) pci->configWrite32(reg + 4, savedHi);
    pci->configWrite16(kIOPCIConfigCommand, savedCmd);

    uint64_t base = savedLo & ~0x0fULL;
    uint64_t sizeMask = maskLo & ~0x0fULL;
    if (is64) {
        base |= ((uint64_t)savedHi << 32);
        sizeMask |= ((uint64_t)maskHi << 32);
    }
    if (!base || !sizeMask) return false;

    uint64_t size = (~sizeMask) + 1;
    if (!is64) size &= 0xffffffffULL;
    if (size < 0x1000) size = 0x1000;
    if (size > 0x1000000ULL) return false;   // implausible; bail

    *outBase = base;
    *outSize = size;
    return true;
}

bool PDE1000::start(IOService *provider)
{
    if (!super::start(provider))
        return false;

    fPCIDevice = OSDynamicCast(IOPCIDevice, provider);
    if (!fPCIDevice)
        return false;

    fPCIDevice->retain();
    if (!fPCIDevice->open(this)) {
        IOLog("PDE1000: failed to open PCI device\n");
        return false;
    }

    fPCIDevice->setBusMasterEnable(true);
    fPCIDevice->setMemoryEnable(true);

    // Map BAR0 (register space). Two paths, mirroring RavynAHCIPort:
    //  1. The provider's IODeviceMemory range (must map kIOMapAnywhere; map() with
    //     no options attempts a fixed mapping at 0 and always fails).
    //  2. QEMU frequently mis-tags / omits the IODeviceMemory ranges, so fall back
    //     to reading BAR0 straight from config space (standard sizing dance) and
    //     building the descriptor with withPhysicalAddress.
    IODeviceMemory *bar0 = fPCIDevice->getDeviceMemoryWithRegister(kIOPCIConfigBaseAddress0);
    if (bar0 && bar0->getLength())
        fRegMap = bar0->map(kIOMapAnywhere);

    if (!fRegMap) {
        uint64_t base = 0, size = 0;
        if (readMemoryBAR(fPCIDevice, kIOPCIConfigBaseAddress0, &base, &size)) {
            fRegDesc = IOMemoryDescriptor::withPhysicalAddress(
                (IOPhysicalAddress)base, (IOByteCount)size,
                kIODirectionNone | kIOMemoryMapperNone);
            if (fRegDesc)
                fRegMap = fRegDesc->map(kIOMapAnywhere);
            IOLog("PDE1000: BAR0 config fallback base=0x%llx size=0x%llx map=%p\n",
                (unsigned long long)base, (unsigned long long)size, fRegMap);
        }
    }
    if (!fRegMap) {
        IOLog("PDE1000: failed to map BAR0\n");
        return false;
    }
    fRegs = (volatile uint32_t *)fRegMap->getVirtualAddress();

    fWorkLoop = getWorkLoop();
    if (!fWorkLoop)
        return false;

    // Try MSI first: IOPCIFamily's resolveMSIInterrupts() already ran at nub
    // publish and, if MSI allocation succeeded, added an "IOInterruptSpecifiers"
    // entry at source index 0 pointing at the family's messaged-interrupt
    // controller. IOInterruptEventSource::interruptEventSource() ends up
    // calling fPCIDevice->registerInterrupt(0, ...), which routes there and
    // programs the device's MSI capability registers (enableDeviceMSI()). RX
    // still gets timer-polled below because early OpenOSX/QEMU interrupt
    // routing is not stable enough to make networking depend on MSI delivery.
    fInterruptSource = IOInterruptEventSource::interruptEventSource(
        this, &PDE1000::interruptOccurredStatic, fPCIDevice, 0);
    if (fInterruptSource && fWorkLoop->addEventSource(fInterruptSource) == kIOReturnSuccess) {
        IOLog("PDE1000: using MSI interrupt (source 0)\n");
    } else {
        if (fInterruptSource) { fInterruptSource->release(); fInterruptSource = NULL; }
        IOLog("PDE1000: MSI registration failed, using polling only\n");
    }

    fPollTimer = IOTimerEventSource::timerEventSource(this, &PDE1000::pollTimerAction);
    if (!fPollTimer || fWorkLoop->addEventSource(fPollTimer) != kIOReturnSuccess) {
        IOLog("PDE1000: failed to create poll timer\n");
        return false;
    }

    // Reset the controller, then wait for it to come out of reset.
    regWrite(REG_CTRL, regRead(REG_CTRL) | CTRL_RST);
    IOSleep(10);

    // Mask all interrupt causes for now; enable() unmasks RX causes only if
    // we ended up on the MSI path, otherwise we stay fully polling-driven.
    regWrite(REG_IMC, 0xFFFFFFFF);

    if (!readMACAddress()) {
        IOLog("PDE1000: failed to read MAC address from EEPROM\n");
        return false;
    }

    IOLog("PDE1000: MAC %02x:%02x:%02x:%02x:%02x:%02x\n",
        fMACAddress.bytes[0], fMACAddress.bytes[1], fMACAddress.bytes[2],
        fMACAddress.bytes[3], fMACAddress.bytes[4], fMACAddress.bytes[5]);

    // Set link up + auto speed detect.
    regWrite(REG_CTRL, regRead(REG_CTRL) | CTRL_SLU | CTRL_ASDE);

    // Zero the multicast table.
    for (int i = 0; i < 128; i++)
        regWrite(REG_MTA + i * 4, 0);

    if (!initRxRing() || !initTxRing())
        return false;
    if (!publishLinkMedium())
        return false;

    // attachInterface() creates the interface via createInterface() (overridden by
    // IOEthernetController to make an IOEthernetInterface), configures it, attaches
    // it to the data-link layer and registers it. Don't hand-roll alloc/init.
    if (!attachInterface((IONetworkInterface **)&fInterface, true)) {
        IOLog("PDE1000: attachInterface failed\n");
        return false;
    }
    setLinkStatus(kIONetworkLinkValid | kIONetworkLinkActive, getCurrentMedium());

    registerService();
    return true;
}

void PDE1000::stop(IOService *provider)
{
    if (fEnabled)
        disable(fInterface);

    if (fInterface) {
        detachInterface(fInterface, true);
        fInterface->release();
        fInterface = NULL;
    }

    if (fWorkLoop && fPollTimer)
        fWorkLoop->removeEventSource(fPollTimer);
    if (fWorkLoop && fInterruptSource)
        fWorkLoop->removeEventSource(fInterruptSource);

    super::stop(provider);
}

void PDE1000::free()
{
    for (int i = 0; i < kPDE1000RxDescCount; i++)
        if (fRxPacketBuf[i]) { fRxPacketBuf[i]->release(); fRxPacketBuf[i] = NULL; }
    for (int i = 0; i < kPDE1000TxDescCount; i++)
        if (fTxPacketBuf[i]) { fTxPacketBuf[i]->release(); fTxPacketBuf[i] = NULL; }

    if (fRxDescBuf) { fRxDescBuf->release(); fRxDescBuf = NULL; }
    if (fTxDescBuf) { fTxDescBuf->release(); fTxDescBuf = NULL; }

    if (fPollTimer) { fPollTimer->release(); fPollTimer = NULL; }
    if (fInterruptSource) { fInterruptSource->release(); fInterruptSource = NULL; }

    if (fRegMap) { fRegMap->release(); fRegMap = NULL; }
    if (fRegDesc) { fRegDesc->release(); fRegDesc = NULL; }

    if (fPCIDevice) {
        fPCIDevice->close(this);
        fPCIDevice->release();
        fPCIDevice = NULL;
    }

    super::free();
}

uint32_t PDE1000::regRead(uint32_t offset)
{
    return OSReadLittleInt32((volatile void *)fRegs, offset);
}

void PDE1000::regWrite(uint32_t offset, uint32_t value)
{
    OSWriteLittleInt32((volatile void *)fRegs, offset, value);
}

uint16_t PDE1000::eepromRead(uint8_t addr)
{
    regWrite(REG_EERD, EERD_START | ((uint32_t)addr << 8));

    for (int spin = 0; spin < 100000; spin++) {
        uint32_t val = regRead(REG_EERD);
        if (val & EERD_DONE)
            return (uint16_t)(val >> 16);
    }
    return 0xFFFF;
}

bool PDE1000::readMACAddress()
{
    // QEMU's e1000 always programs a valid RAL0/RAH0 (link-local unicast
    // filter) at reset with the NIC's MAC, which is simpler and more
    // reliable to read back than the EEPROM shadow copy.
    uint32_t ral = regRead(REG_RAL0);
    uint32_t rah = regRead(REG_RAH0);

    if (!(rah & (1u << 31))) {
        // RAL0/RAH0 "address valid" bit not set - fall back to EEPROM words
        // 0..2 (standard layout: word0=bytes0-1, word1=bytes2-3, word2=bytes4-5).
        uint16_t w0 = eepromRead(0);
        uint16_t w1 = eepromRead(1);
        uint16_t w2 = eepromRead(2);
        if (w0 == 0xFFFF && w1 == 0xFFFF && w2 == 0xFFFF)
            return false;
        fMACAddress.bytes[0] = (uint8_t)(w0 & 0xFF);
        fMACAddress.bytes[1] = (uint8_t)(w0 >> 8);
        fMACAddress.bytes[2] = (uint8_t)(w1 & 0xFF);
        fMACAddress.bytes[3] = (uint8_t)(w1 >> 8);
        fMACAddress.bytes[4] = (uint8_t)(w2 & 0xFF);
        fMACAddress.bytes[5] = (uint8_t)(w2 >> 8);
        return true;
    }

    fMACAddress.bytes[0] = (uint8_t)(ral & 0xFF);
    fMACAddress.bytes[1] = (uint8_t)((ral >> 8) & 0xFF);
    fMACAddress.bytes[2] = (uint8_t)((ral >> 16) & 0xFF);
    fMACAddress.bytes[3] = (uint8_t)((ral >> 24) & 0xFF);
    fMACAddress.bytes[4] = (uint8_t)(rah & 0xFF);
    fMACAddress.bytes[5] = (uint8_t)((rah >> 8) & 0xFF);
    return true;
}

bool PDE1000::initRxRing()
{
    size_t descSize = sizeof(PDE1000RxDesc) * kPDE1000RxDescCount;
    fRxDescBuf = IOBufferMemoryDescriptor::inTaskWithPhysicalMask(
        kernel_task, kIODirectionInOut | kIOMemoryPhysicallyContiguous,
        descSize, 0xFFFFFFFFull);
    if (!fRxDescBuf)
        return false;
    fRxDescBuf->prepare();
    fRxDesc = (PDE1000RxDesc *)fRxDescBuf->getBytesNoCopy();
    bzero((void *)fRxDesc, descSize);

    for (int i = 0; i < kPDE1000RxDescCount; i++) {
        IOBufferMemoryDescriptor *buf = IOBufferMemoryDescriptor::inTaskWithPhysicalMask(
            kernel_task, kIODirectionInOut | kIOMemoryPhysicallyContiguous,
            kPDE1000RxBufferSize, 0xFFFFFFFFull);
        if (!buf)
            return false;
        buf->prepare();
        fRxPacketBuf[i] = buf;
        fRxDesc[i].addr = buf->getPhysicalAddress();
        fRxDesc[i].status = 0;
    }

    uint64_t descPhys = fRxDescBuf->getPhysicalAddress();
    regWrite(REG_RDBAL, (uint32_t)(descPhys & 0xFFFFFFFF));
    regWrite(REG_RDBAH, (uint32_t)(descPhys >> 32));
    regWrite(REG_RDLEN, (uint32_t)descSize);
    regWrite(REG_RDH, 0);
    regWrite(REG_RDT, kPDE1000RxDescCount - 1);
    fRxTail = kPDE1000RxDescCount - 1;

    regWrite(REG_RCTL, RCTL_EN | RCTL_UPE | RCTL_BAM | RCTL_BSIZE_2048 | RCTL_SECRC);
    return true;
}

bool PDE1000::initTxRing()
{
    size_t descSize = sizeof(PDE1000TxDesc) * kPDE1000TxDescCount;
    fTxDescBuf = IOBufferMemoryDescriptor::inTaskWithPhysicalMask(
        kernel_task, kIODirectionInOut | kIOMemoryPhysicallyContiguous,
        descSize, 0xFFFFFFFFull);
    if (!fTxDescBuf)
        return false;
    fTxDescBuf->prepare();
    fTxDesc = (PDE1000TxDesc *)fTxDescBuf->getBytesNoCopy();
    bzero((void *)fTxDesc, descSize);

    for (int i = 0; i < kPDE1000TxDescCount; i++) {
        IOBufferMemoryDescriptor *buf = IOBufferMemoryDescriptor::inTaskWithPhysicalMask(
            kernel_task, kIODirectionInOut | kIOMemoryPhysicallyContiguous,
            kPDE1000RxBufferSize, 0xFFFFFFFFull);
        if (!buf)
            return false;
        buf->prepare();
        fTxPacketBuf[i] = buf;
        fTxDesc[i].addr = buf->getPhysicalAddress();
        fTxDesc[i].status = TXDESC_STATUS_DD;
    }

    uint64_t descPhys = fTxDescBuf->getPhysicalAddress();
    regWrite(REG_TDBAL, (uint32_t)(descPhys & 0xFFFFFFFF));
    regWrite(REG_TDBAH, (uint32_t)(descPhys >> 32));
    regWrite(REG_TDLEN, (uint32_t)descSize);
    regWrite(REG_TDH, 0);
    regWrite(REG_TDT, 0);
    fTxTail = 0;

    regWrite(REG_TIPG, 0x0060200A);
    regWrite(REG_TCTL, TCTL_EN | TCTL_PSP | (0x0F << 4) | (0x40 << 12));
    return true;
}

bool PDE1000::publishLinkMedium()
{
    OSDictionary *mediumDict = OSDictionary::withCapacity(2);
    IONetworkMedium *autoMedium = NULL;
    IONetworkMedium *gigMedium = NULL;
    bool ok = false;

    if (!mediumDict)
        return false;

    autoMedium = IONetworkMedium::medium(
        kIOMediumEthernetAuto | kIOMediumOptionFullDuplex, 1000000000ULL);
    gigMedium = IONetworkMedium::medium(
        kIOMediumEthernet1000BaseT | kIOMediumOptionFullDuplex, 1000000000ULL);

    if (autoMedium && gigMedium &&
        IONetworkMedium::addMedium(mediumDict, autoMedium) &&
        IONetworkMedium::addMedium(mediumDict, gigMedium) &&
        publishMediumDictionary(mediumDict) &&
        setCurrentMedium(autoMedium)) {
        ok = true;
    }

    if (gigMedium)
        gigMedium->release();
    if (autoMedium)
        autoMedium->release();
    mediumDict->release();

    if (!ok)
        IOLog("PDE1000: failed to publish link medium\n");
    return ok;
}

IOReturn PDE1000::enable(IONetworkInterface *interface)
{
    if (fEnabled)
        return kIOReturnSuccess;

    IOLog("PDE1000: enable interface=%p\n", interface);

    if (fInterruptSource) {
        // RXT0 (receive timer, fires after a short delay following any RX
        // activity) and RXDMT0 (RX descriptor minimum threshold) are enough
        // to keep the RX ring drained without polling.
        regWrite(REG_IMS, (1u << 7) | (1u << 4));
        fInterruptSource->enable();
    }
    if (fPollTimer)
        fPollTimer->setTimeoutMS(5);

    fEnabled = true;
    return kIOReturnSuccess;
}

IOReturn PDE1000::disable(IONetworkInterface *interface)
{
    if (!fEnabled)
        return kIOReturnSuccess;

    IOLog("PDE1000: disable interface=%p\n", interface);

    if (fInterruptSource) {
        fInterruptSource->disable();
        regWrite(REG_IMC, 0xFFFFFFFF);
    }
    if (fPollTimer)
        fPollTimer->cancelTimeout();

    fEnabled = false;
    return kIOReturnSuccess;
}

void PDE1000::pollTimerAction(OSObject *owner, IOTimerEventSource *sender)
{
    PDE1000 *self = OSDynamicCast(PDE1000, owner);
    if (!self)
        return;
    self->pollReceive();
    if (self->fEnabled)
        sender->setTimeoutMS(5);
}

void PDE1000::interruptOccurredStatic(OSObject *owner, IOInterruptEventSource *sender, int count)
{
    PDE1000 *self = OSDynamicCast(PDE1000, owner);
    if (!self)
        return;
    self->interruptOccurred(sender, count);
}

void PDE1000::interruptOccurred(IOInterruptEventSource *sender, int count)
{
    // Reading ICR acknowledges/clears the causes (write-1-to-clear on read).
    uint32_t icr = regRead(REG_ICR);
    if (!icr)
        return;
    pollReceive();
}

void PDE1000::pollReceive()
{
    uint32_t head = regRead(REG_RDH);
    uint32_t idx = (fRxTail + 1) % kPDE1000RxDescCount;
    while (idx != head) {
        PDE1000RxDesc *desc = &fRxDesc[idx];
        if (!(desc->status & RXDESC_STATUS_DD))
            break;

        uint16_t len = desc->length;
        if (len > 0 && len <= kPDE1000RxBufferSize) {
            fRxPacketBuf[idx]->complete();
            fRxPackets++;
            if (fRxLogBudget) {
                //IOLog("PDE1000: RX desc=%u len=%u status=0x%02x errors=0x%02x rdh=%u rdt=%u\n", idx, len, desc->status, desc->errors, regRead(REG_RDH), regRead(REG_RDT));
                //logEtherFrame("RX", fRxPackets, fRxPacketBuf[idx]->getBytesNoCopy(), len);
                fRxLogBudget--;
            }
            mbuf_t m = allocatePacket(len);
            if (m) {
                if (mbuf_copyback(m, 0, len, fRxPacketBuf[idx]->getBytesNoCopy(), MBUF_WAITOK) == 0) {
                    mbuf_pkthdr_setlen(m, len);
                    mbuf_setlen(m, len);
                }
                fInterface->inputPacket(m, len, IONetworkInterface::kInputOptionQueuePacket);
            }
            fRxPacketBuf[idx]->prepare();
        } else if (fRxLogBudget) {
            //IOLog("PDE1000: RX desc=%u invalid len=%u status=0x%02x errors=0x%02x\n", idx, len, desc->status, desc->errors);
            fRxLogBudget--;
        }

        desc->status = 0;
        fRxTail = idx;
        regWrite(REG_RDT, fRxTail);
        idx = (fRxTail + 1) % kPDE1000RxDescCount;
        head = regRead(REG_RDH);
    }

    if (fInterface)
        fInterface->flushInputQueue();
}

UInt32 PDE1000::outputPacket(mbuf_t m, void *param)
{
    size_t pktLen = mbuf_pkthdr_len(m);
    if (pktLen == 0 || pktLen > kPDE1000RxBufferSize) {
        freePacket(m);
        return kIOReturnOutputDropped;
    }

    uint32_t idx = fTxTail;
    PDE1000TxDesc *desc = &fTxDesc[idx];

    // Wait (briefly) for the descriptor to be free - polling based, no
    // backpressure signaling to the output queue yet.
    for (int spin = 0; spin < 100000 && !(desc->status & TXDESC_STATUS_DD); spin++)
        ;
    if (!(desc->status & TXDESC_STATUS_DD)) {
        freePacket(m);
        return kIOReturnOutputDropped;
    }

    mbuf_copydata(m, 0, pktLen, fTxPacketBuf[idx]->getBytesNoCopy());
    fTxPacketBuf[idx]->complete();

    desc->length = (uint16_t)pktLen;
    desc->cmd = TXDESC_CMD_EOP | TXDESC_CMD_IFCS | TXDESC_CMD_RS;
    desc->status = 0;
    fTxPackets++;
    if (fTxLogBudget) {
        //IOLog("PDE1000: TX desc=%u len=%lu tdh=%u tdt=%u\n", idx, (unsigned long)pktLen, regRead(REG_TDH), regRead(REG_TDT));
        //logEtherFrame("TX", fTxPackets, fTxPacketBuf[idx]->getBytesNoCopy(), pktLen);
        fTxLogBudget--;
    }

    fTxTail = (idx + 1) % kPDE1000TxDescCount;
    regWrite(REG_TDT, fTxTail);
    (void)regRead(REG_STATUS);

    freePacket(m);
    return kIOReturnOutputSuccess;
}

IOReturn PDE1000::getHardwareAddress(IOEthernetAddress *addr)
{
    if (!addr)
        return kIOReturnBadArgument;
    *addr = fMACAddress;
    return kIOReturnSuccess;
}

IOReturn PDE1000::setPromiscuousMode(bool active)
{
    uint32_t rctl = regRead(REG_RCTL);
    if (active)
        rctl |= RCTL_UPE | RCTL_MPE;
    else
        rctl &= ~(RCTL_UPE | RCTL_MPE);
    regWrite(REG_RCTL, rctl);
    return kIOReturnSuccess;
}

IOReturn PDE1000::setMulticastMode(bool active)
{
    uint32_t rctl = regRead(REG_RCTL);
    if (active)
        rctl |= RCTL_MPE;
    else
        rctl &= ~RCTL_MPE;
    regWrite(REG_RCTL, rctl);
    return kIOReturnSuccess;
}

IOReturn PDE1000::setMulticastList(IOEthernetAddress *addrs, UInt32 count)
{
    // Not filtering by address yet - multicast promiscuous covers it.
    return kIOReturnSuccess;
}

const OSString *PDE1000::newVendorString() const
{
    return OSString::withCString("Intel");
}

const OSString *PDE1000::newModelString() const
{
    return OSString::withCString("82545EM Gigabit Ethernet Controller (PDE1000)");
}

IOOutputQueue *PDE1000::createOutputQueue()
{
    // This tree's IONetworkController does not currently start the optional
    // IOOutputQueue during doEnable(), so a gated queue traps packets before
    // they reach outputPacket(). Use the legacy direct output handler until the
    // generic queue lifecycle is wired.
    return NULL;
}
