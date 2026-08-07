#ifndef _PD_IOUSB_HID_MOUSE_H
#define _PD_IOUSB_HID_MOUSE_H

#include <IOKit/IOService.h>
#include <IOKit/IOBufferMemoryDescriptor.h>
#include <IOKit/usb/IOUSBInterface.h>
#include <IOKit/usb/IOUSBPipe.h>

class IOUSBHIDMouse : public IOService
{
    OSDeclareDefaultStructors(IOUSBHIDMouse);

public:
    bool init(OSDictionary *dict = NULL) override;
    bool start(IOService *provider) override;
    void stop(IOService *provider) override;
    void free() override;

private:
    static void pollThread(void *arg, wait_result_t);
    void pollLoop();
    void handleReport(const UInt8 report[8]);
    bool setBootProtocol();

    IOUSBInterface *fInterface;
    IOUSBPipe *fInterruptPipe;
    IOBufferMemoryDescriptor *fReportMem;
    volatile bool fRunning;
    UInt8 fLastButtons;
    UInt8 fMouseIndex;
};

#endif
