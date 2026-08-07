/*
 * pd_dyld_internal_stubs.cpp
 *
 * dyld references a handful of its own symbols that live in translation units we
 * can't compile in the OpenOSX bring-up:
 *
 *  - The dyld-monitor notification hooks (setNotifyMonitoringDyld{,Main} and
 *    AllImages::notifyMonitor{Main,Loads,Unloads}) live in
 *    dyld_process_info_notify.cpp, which is built on libdispatch
 *    (dispatch_source/queue/once). They only exist so external tools
 *    (Instruments, the debugger's dyld-monitor SPI) can observe image loads;
 *    a booting dyld does not need them. No-op them.
 *
 *  - A few libdyld API globals (dyld_get_active_platform, _NSGetMachExecuteHeader,
 *    _dyld_shared_cache_contains_path, _dyld_fast_stub_entry) normally come from
 *    dyldAPIsInLibSystem.cpp (the libdyld side, not the dyld binary). Provide
 *    minimal bring-up implementations: platform is macOS and there is no shared
 *    cache. _dyld_fast_stub_entry forwards into dyld2's real lazy binder so
 *    classic lazy stubs do not require -fixup_chains in every user binary.
 *
 * This file is compiled into the dyld target so it inherits its include env.
 */

#include <cstdint>
#include <mach-o/loader.h>
#include <mach-o/dyld_images.h>
#include <mach-o/dyld_priv.h>
#include "dyld3/AllImages.h"

void setNotifyMonitoringDyldMain(void (*)()) { }
void setNotifyMonitoringDyld(void (*)(bool unloading, unsigned imageCount,
                                      const struct mach_header* loadAddresses[],
                                      const char* imagePaths[])) { }

namespace dyld3 {
void AllImages::notifyMonitorMain() { }
void AllImages::notifyMonitorLoads(const Array<LoadedImage>&) { }
void AllImages::notifyMonitorUnloads(const Array<LoadedImage>&) { }
}

dyld_platform_t dyld_get_active_platform(void) { return PLATFORM_MACOS; }

extern "C" void* _NSGetMachExecuteHeader(void) { return nullptr; }

bool _dyld_shared_cache_contains_path(const char*) { return false; }

class ImageLoader;
namespace dyld { uintptr_t fastBindLazySymbol(ImageLoader** imageLoaderCache, uintptr_t lazyBindingInfoOffset); }

void* _dyld_fast_stub_entry(void* loaderCache, long lazyBindingInfoOffset)
{
    return reinterpret_cast<void*>(
        dyld::fastBindLazySymbol(reinterpret_cast<ImageLoader**>(loaderCache),
                                 static_cast<uintptr_t>(lazyBindingInfoOffset)));
}

// The C dlopen entry normally lives in dyldAPIsInLibSystem.cpp (the libdyld side,
// not the dyld binary). dyld3/APIs.cpp puts &dlopen in its API function-pointer
// table, so the dyld binary needs the symbol. Provide it here, forwarding to the
// real implementation dyld3::dlopen_internal (declared in dyld3/APIs.h). NOTE:
// dlopen_internal walks __builtin_return_address to identify the calling image,
// so this thin entry must itself be on the frame chain -- exactly as the
// canonical glue does it.
namespace dyld3 { void* dlopen_internal(const char* path, int mode, void* callerAddress); }

extern "C" void* dlopen(const char* path, int mode)
{
    return dyld3::dlopen_internal(path, mode, __builtin_return_address(0));
}
