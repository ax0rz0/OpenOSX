/*
 * sandbox/private.h  (OpenOSX stub)
 *
 * Apple's libsandbox private header is source-available, not open source, so it
 * is absent from OpenOSX and the SDK. dyld2.cpp uses only sandbox_check() with
 * the SANDBOX_FILTER_PATH / SANDBOX_CHECK_NO_REPORT flags; declare just those. The
 * sandbox_check symbol is furnished at link time by a permissive OpenOSX stub.
 */
#ifndef OPENOSX_SANDBOX_PRIVATE_STUB_H
#define OPENOSX_SANDBOX_PRIVATE_STUB_H

#include <sys/types.h>
#include <sandbox.h>

typedef unsigned int sandbox_filter_type;

enum {
	SANDBOX_FILTER_NONE      = 0,
	SANDBOX_FILTER_PATH      = 1,
	SANDBOX_CHECK_NO_REPORT  = (1 << 31),
};

#ifdef __cplusplus
extern "C"
#endif
int sandbox_check(pid_t pid, const char *operation, sandbox_filter_type type, ...);

#endif /* OPENOSX_SANDBOX_PRIVATE_STUB_H */
