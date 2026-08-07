/*
 * ioreg.c includes this only for the IOCFSerialize() declaration and the
 * kIOCFSerializeToBinary constant - it never actually calls IOCFSerialize
 * itself, so this header-only shim (no .c body) is enough to satisfy the
 * #include without porting the real ~1400-line IOKitUser/IOCFSerialize.c.
 */
#ifndef __IOKIT_IOCFSERIALIZE_H
#define __IOKIT_IOCFSERIALIZE_H

#include <CoreFoundation/CFBase.h>
#include <CoreFoundation/CFData.h>

#if defined(__cplusplus)
extern "C" {
#endif

enum {
	kIOCFSerializeToBinary = 0x00000001
};

CFDataRef
IOCFSerialize(CFTypeRef object, CFOptionFlags options);

#if defined(__cplusplus)
}
#endif

#endif /* __IOKIT_IOCFSERIALIZE_H */
