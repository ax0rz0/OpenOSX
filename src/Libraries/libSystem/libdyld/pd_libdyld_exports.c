/*
 * pd_libdyld_exports.c -- bridges libdyld.dylib's PRIVATE (hidden-visibility,
 * by deliberate design -- see start_glue.s/dyldLibSystemGlue.c) ABI surface to
 * libSystem.B.dylib, which now carries the dyld<->libSystem handshake
 * constructor (pd_libSystem_init.c, moved there because dyld only permits an
 * image's initializer to run before libSystemInitialized if that image's
 * install path is libSystem.B.dylib's own -- see that file's own comment).
 *
 * `start` (start_glue.s) is `.private_extern` -- do NOT change that visibility
 * directly: it is shared with the `dyld` executable target too, and the last
 * time this project renamed/touched `_start` there it broke dyld's own build.
 * Instead expose its address via an ordinary (default-visibility) data symbol
 * defined HERE, inside libdyld.dylib, where referencing the hidden `start`
 * symbol directly is legal (same image).
 */
extern void start(void);
void *pd_libdyld_getStartGlueToCallExit(void) { return (void *)&start; }

/*
 * __isPlatformVersionAtLeast is emitted by clang for @available()/
 * __builtin_available()/API_AVAILABLE checks; on real Darwin it lives in
 * dyld (dyld3::APIs) and is resolved from the flat namespace at runtime.
 * PureDarwin's vendored dyld3 sources don't implement it, so binaries that
 * use @available (e.g. fastfetch) fail to launch with
 * "Symbol not found: ___isPlatformVersionAtLeast". PureDarwin targets a
 * single platform/version (macOS 11.0, fixed at link time via
 * -platform_version), so every @available() check compiled against our SDK
 * is trivially satisfied -> always return true. (This is the same stub that
 * used to live in the orphaned, never-compiled pd_dladdr.c.)
 */
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
