/*
 * libamfi.h  (OpenOSX stub)
 *
 * dyld2.cpp includes <libamfi.h> on non-simulator builds for the AMFI
 * (AppleMobileFileIntegrity) dyld-policy interface. OpenOSX has no AMFI, so
 * we provide the minimal declarations dyld needs -- identical to the ones dyld
 * declares inline on its TARGET_OS_SIMULATOR path. The amfi_check_dyld_policy_self
 * symbol must be furnished at link time (a permissive OpenOSX stub).
 */
#ifndef OPENOSX_LIBAMFI_STUB_H
#define OPENOSX_LIBAMFI_STUB_H

#include <stdint.h>

enum amfi_dyld_policy_input_flag_set {
	AMFI_DYLD_INPUT_PROC_IN_SIMULATOR       = (1 << 0),
	AMFI_DYLD_INPUT_PROC_IS_ENCRYPTED       = (1 << 1),
	AMFI_DYLD_INPUT_PROC_HAS_RESTRICT_SEG   = (1 << 2),
};

enum amfi_dyld_policy_output_flag_set {
	AMFI_DYLD_OUTPUT_ALLOW_AT_PATH                    = (1 << 0),
	AMFI_DYLD_OUTPUT_ALLOW_PATH_VARS                  = (1 << 1),
	AMFI_DYLD_OUTPUT_ALLOW_CUSTOM_SHARED_CACHE        = (1 << 2),
	AMFI_DYLD_OUTPUT_ALLOW_FALLBACK_PATHS             = (1 << 3),
	AMFI_DYLD_OUTPUT_ALLOW_PRINT_VARS                 = (1 << 4),
	AMFI_DYLD_OUTPUT_ALLOW_FAILED_LIBRARY_INSERTION   = (1 << 5),
	AMFI_DYLD_OUTPUT_ALLOW_LIBRARY_INTERPOSING        = (1 << 6),
};

#ifdef __cplusplus
extern "C"
#endif
int amfi_check_dyld_policy_self(uint64_t input_flags, uint64_t* output_flags);

#endif /* OPENOSX_LIBAMFI_STUB_H */
