/*
 * PureDarwin: real CFMessagePort.c's CFMessagePortSetName() needs
 * bootstrap_check_in(), which only currently exists inside
 * XPC_libxpc_static (force-loaded into launchd_real) - pulling it into
 * libSystem_B_stub too would mean also force-loading launchd's whole MIG
 * client-stub archive (XPC_launchd_mig_static) into libSystem itself, a
 * much bigger structural change than this stub is worth. CFMachPort.c
 * (real, see CMakeLists.txt) is what SystemStarter's real job-exit
 * detection actually needs; CFMessagePort itself isn't used anywhere in
 * this tree yet. CFRuntime.c's central class table still unconditionally
 * references __CFMessagePortClass, so provide a minimal, functionless
 * registration for just that - not a general CFMessagePort reimplementation.
 */
#include "CFRuntime.h"

const CFRuntimeClass __CFMessagePortClass = {
    0,
    "CFMessagePort",
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
};
