extern void start(void);
void *pd_libdyld_getStartGlueToCallExit(void) { return (void *)&start; }

extern void tlv_initializer(void);
void pd_libdyld_tlv_initializer(void) { tlv_initializer(); }

#include <stdint.h>
#include <stdbool.h>
bool
__isPlatformVersionAtLeast(uint32_t platform, uint32_t major, uint32_t minor,
		uint32_t subminor)
{
	(void)platform;
	(void)major;
	(void)minor;
	(void)subminor;
	return true;
}

/*
 * _availability_version_check: the other half of clang's @available lowering.
 * Xcode 12 and later emit a call to this instead of __isPlatformVersionAtLeast
 * when a check names several platforms, so a binary can import either.
 *
 * Answers yes, matching the stub above. Note what that means: every @available
 * check in a foreign binary succeeds regardless of what we actually implement,
 * so an app that guards a newer API behind one will take the newer path and
 * find the symbol missing. That is the right trade for now - answering no
 * would send apps down deprecated paths we implement no better - but it is a
 * reason a binary can fail somewhere far from the check that caused it.
 */
typedef uint32_t pd_dyld_platform_t;
typedef struct {
	pd_dyld_platform_t platform;
	uint32_t version;
} pd_dyld_build_version_t;

bool
_availability_version_check(uint32_t count, pd_dyld_build_version_t versions[])
{
	(void)count;
	(void)versions;
	return true;
}
