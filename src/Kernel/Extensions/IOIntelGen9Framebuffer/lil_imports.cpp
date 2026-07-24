#include <IOKit/pci/IOPCIDevice.h>
#include <IOKit/IOMemoryDescriptor.h>
#include <IOKit/IOLib.h>
#include <libkern/OSAtomic.h>

extern "C" {
#include <lil/imports.h>
}

// PCI config space
// lil offsets are raw byte offsets into PCI config space.

extern "C" void lil_pci_writeb(void *device, uint16_t offset, uint8_t val) {
    ((IOPCIDevice *)device)->configWrite8(offset, val);
}
extern "C" uint8_t lil_pci_readb(void *device, uint16_t offset) {
    return ((IOPCIDevice *)device)->configRead8(offset);
}
extern "C" void lil_pci_writew(void *device, uint16_t offset, uint16_t val) {
    ((IOPCIDevice *)device)->configWrite16(offset, val);
}
extern "C" uint16_t lil_pci_readw(void *device, uint16_t offset) {
    return ((IOPCIDevice *)device)->configRead16(offset);
}
extern "C" void lil_pci_writed(void *device, uint16_t offset, uint32_t val) {
    ((IOPCIDevice *)device)->configWrite32(offset, val);
}
extern "C" uint32_t lil_pci_readd(void *device, uint16_t offset) {
    return ((IOPCIDevice *)device)->configRead32(offset);
}

// BAR mapping
// lil BAR index N == PCI BAR N. IOPCIDevice memory index maps 1:1 to BAR index
// for the memory BARs we care about (BAR0 = MMIO+GTT, BAR2 = stolen aperture).
// We map write-combined and leak the mapping (driver-lifetime); a future
// revision can track these per-device for lil_unmap teardown.

extern "C" void lil_get_bar(void *device, int bar, uintptr_t *obase, uintptr_t *len) {
    IOPCIDevice *pci = (IOPCIDevice *)device;

    // BAR byte offset in config space: 0x10 + bar*4.
    IODeviceMemory *mem = pci->getDeviceMemoryWithIndex((unsigned)bar);
    if (!mem) {
        IOLog("lil_get_bar: no device memory for BAR%d\n", bar);
        *obase = 0;
        *len = 0;
        return;
    }

    IOMemoryMap *map = mem->map(kIOMapInhibitCache | kIOMapWriteCombineCache);
    if (!map) {
        IOLog("lil_get_bar: failed to map BAR%d\n", bar);
        *obase = 0;
        *len = 0;
        return;
    }

    // Intentionally retained for the driver's lifetime (see file note).
    *obase = (uintptr_t)map->getVirtualAddress();
    *len = (uintptr_t)map->getLength();
}

// timing

extern "C" void lil_sleep(uint64_t ms) {
    IOSleep((unsigned)ms);          // may block; lil calls this from start()
}
extern "C" void lil_usleep(uint64_t us) {
    IODelay((unsigned)us);          // busy-wait; safe at any context
}

// allocation
// IOFree needs the size, which lil_free() does not carry, so we stash the
// allocation size in an 8-byte header preceding the returned pointer.

extern "C" void *lil_malloc(size_t s) {
    size_t total = s + sizeof(size_t);
    void *raw = IOMalloc(total);
    if (!raw) {
        panic("lil_malloc: IOMalloc(%zu) failed", total);
    }
    *(size_t *)raw = total;
    return (void *)((uint8_t *)raw + sizeof(size_t));
}

extern "C" void lil_free(void *p) {
    void *raw = (void *)((uint8_t *)p - sizeof(size_t));
    size_t total = *(size_t *)raw;
    IOFree(raw, total);
}

// logging

extern "C" void lil_log(enum LilLogType type, const char *fmt, ...) {
    const char *tag;
    switch (type) {
    case ERROR:   tag = "lil ERROR: "; break;
    case WARNING: tag = "lil WARN: ";  break;
    case INFO:    tag = "lil: ";       break;
    default:      tag = "lil dbg: ";   break;
    }
    // IOLog has no vprintf variant exposed here; prefix then format.
    va_list ap;
    va_start(ap, fmt);
    char buf[256];
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    IOLog("%s%s", tag, buf);
}

extern "C" void lil_panic(const char *msg) {
    panic("lil_panic: %s", msg);
}
