/* OpenOSX compat redirect for split userspace builds. */
#ifndef PRIVATE
#define OPENOSX_DEFINED_PRIVATE_FOR_CPU_CAPABILITIES 1
#define PRIVATE 1
#endif

#include <i386/cpu_capabilities.h>

#ifdef OPENOSX_DEFINED_PRIVATE_FOR_CPU_CAPABILITIES
#undef PRIVATE
#undef OPENOSX_DEFINED_PRIVATE_FOR_CPU_CAPABILITIES
#endif
