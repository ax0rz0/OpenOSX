/*
 * RavynHDAudio: minimal Intel HD Audio (HDA) controller + codec driver,
 * playback-only, targeting QEMU's -device intel-hda + hda-duplex emulated
 * hardware (vendor 0x8086, device 0x2668). Written from the public Intel
 * High Definition Audio Specification register/verb layout, not derived
 * from any Apple driver - this does NOT use IOAudioFamily (doesn't exist
 * in this tree yet); it's a standalone IOService that programs the
 * controller directly and exposes a raw PCM write path at /dev/dsp0,
 * mirroring how IOGOPFramebuffer exposes /dev/fb0.
 *
 * Playback format is fixed at 48000 Hz / 16-bit / stereo (the one format
 * every HDA codec's default DAC is guaranteed to support) - no format
 * negotiation.
 */

#ifndef _RAVYNHDAUDIO_H
#define _RAVYNHDAUDIO_H

#include <IOKit/IOService.h>
#include <IOKit/IOBufferMemoryDescriptor.h>
#include <IOKit/IOLocks.h>
#include <IOKit/pci/IOPCIDevice.h>

#define kHDACorbEntries   32   // CORBSIZE=00 -> 2 entries is min; we request 256B/4=64 max, but keep it simple: 32 verbs at a time is plenty
#define kHDARirbEntries   64
#define kHDABDLEntries    4
#define kHDARingBufBytes  (256 * 1024)   // ~1.37s of buffering at 48kHz/16bit/stereo

class RavynHDAudio : public IOService
{
    OSDeclareDefaultStructors(RavynHDAudio);

public:
    bool init(OSDictionary *properties) override;
    void free() override;
    IOService *probe(IOService *provider, SInt32 *score) override;
    bool start(IOService *provider) override;
    void stop(IOService *provider) override;

    // Called from the /dev/dsp0 cdevsw write path.
    size_t writePCM(const uint8_t *data, size_t len);

    // Stops the output stream (clears SD_CTL.RUN) so a closed /dev/dsp0
    // fd doesn't leave the DMA engine looping over stale ring-buffer
    // contents forever.
    void stopStream();

private:
    IOPCIDevice        *fPCIDevice;
    IOMemoryMap        *fRegMap;
    IOMemoryDescriptor *fRegDesc;   // set only when BAR0 mapped via config fallback
    volatile uint8_t   *fRegs;

    IOBufferMemoryDescriptor *fCorbBuf;
    IOBufferMemoryDescriptor *fRirbBuf;
    volatile uint32_t  *fCorb;
    volatile uint64_t  *fRirb;    // each RIRB entry is 8 bytes: [0]=response,[1]=resp_ex
    uint16_t            fCorbWp;
    uint16_t            fRirbRp;

    IOBufferMemoryDescriptor *fBdlBuf;     // buffer descriptor list for the output stream
    IOBufferMemoryDescriptor *fPcmBuf;     // the actual PCM ring buffer, split across BDL entries
    uint8_t             *fPcmVirt;
    uint64_t             fPcmPhys;
    uint32_t             fOutStreamIndex;  // which SDn register block is our output stream
    uint32_t             fWriteOffset;     // producer position in fPcmVirt, bytes
    uint64_t             fTotalWritten;    // producer bytes since stream start
    uint64_t             fConsumedBase;    // LPIB wrap accumulator (bytes)
    uint32_t             fLastLpib;        // last LPIB sample, for wrap detection
    IOSimpleLock         *fLock;

    bool  resetController();
    bool  setupCorbRirb();
    bool  setupOutputStream();
    uint32_t sendVerb(uint8_t codecAddr, uint16_t nid, uint32_t verb, uint32_t payload);
    bool  enumerateCodec(uint8_t codecAddr);
    bool  programWidgets(uint8_t codecAddr, uint16_t afgNid);

    inline uint8_t  reg8(uint32_t off)  { return fRegs[off]; }
    inline uint16_t reg16(uint32_t off) { return *(volatile uint16_t *)(fRegs + off); }
    inline uint32_t reg32(uint32_t off) { return *(volatile uint32_t *)(fRegs + off); }
    inline void wreg8(uint32_t off, uint8_t v)   { fRegs[off] = v; }
    inline void wreg16(uint32_t off, uint16_t v) { *(volatile uint16_t *)(fRegs + off) = v; }
    inline void wreg32(uint32_t off, uint32_t v) { *(volatile uint32_t *)(fRegs + off) = v; }
};

#endif /* _RAVYNHDAUDIO_H */
