/*
 * IOPowerSources - power-source enumeration.
 *
 * Apple implements this in IOKitUser on top of IOPMLib and the powerd
 * notification machinery, neither of which exists here yet.
 */

#include <CoreFoundation/CoreFoundation.h>

CFTypeRef IOPSCopyPowerSourcesInfo(void);
CFArrayRef IOPSCopyPowerSourcesList(CFTypeRef blob);
CFDictionaryRef IOPSGetPowerSourceDescription(CFTypeRef blob, CFTypeRef ps);

/*
 * The blob is an opaque token that only IOPSCopyPowerSourcesList and
 * IOPSGetPowerSourceDescription interpret. An empty dictionary is a valid
 * CFTypeRef for callers to CFRelease, which is all they do with it.
 */
CFTypeRef
IOPSCopyPowerSourcesInfo(void)
{
	return CFDictionaryCreate(kCFAllocatorDefault, NULL, NULL, 0,
	    &kCFTypeDictionaryKeyCallBacks, &kCFTypeDictionaryValueCallBacks);
}

CFArrayRef
IOPSCopyPowerSourcesList(CFTypeRef blob)
{
	(void)blob;
	return CFArrayCreate(kCFAllocatorDefault, NULL, 0,
	    &kCFTypeArrayCallBacks);
}

CFDictionaryRef
IOPSGetPowerSourceDescription(CFTypeRef blob, CFTypeRef ps)
{
	(void)blob;
	(void)ps;
	/* Not a Copy/Create function: the returned dictionary would be owned by
	 * the blob, so there is nothing to hand back for an empty list. */
	return NULL;
}
