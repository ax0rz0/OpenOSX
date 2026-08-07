#ifndef __ROOTLESS_H__
#define __ROOTLESS_H__

#include <TargetConditionals.h>

#if TARGET_OS_IPHONE && !TARGET_OS_SIMULATOR

#include <libroot/libroot.h>

#ifdef XINA_SUPPORT
_Pragma("message(\"'XINA_SUPPORT' is deprecated. libroot will now handle this for you.\")")
#endif

#define ROOT_PATH(cPath) JBROOT_PATH_CSTRING(cPath)
#define ROOT_PATH_VAR(cPath) JBROOT_PATH_CSTRING(cPath)

#define ROOT_PATH_NS(nsPath) JBROOT_PATH_NSSTRING(nsPath)
#define ROOT_PATH_NS_VAR(nsPath) JBROOT_PATH_NSSTRING(nsPath)

#else

// no libroot support

#include <sys/syslimits.h>
#include <sys/stat.h> 
#include <string.h>

int rootless_protected_volume(const char *path);
/* param renamed from 'class' (a C++ keyword) so this header is usable from C++. */
int rootless_check_trusted_class(const char *path, const char *rootless_class);
int rootless_mkdir_restricted(const char *path, mode_t mode, const char *rootless_class);

#define ROOT_PATH(cPath) THEOS_PACKAGE_INSTALL_PREFIX cPath
#define ROOT_PATH_NS(path) @THEOS_PACKAGE_INSTALL_PREFIX path

#define ROOT_PATH_NS_VAR(path) [@THEOS_PACKAGE_INSTALL_PREFIX stringByAppendingPathComponent:path]
#define ROOT_PATH_VAR(path) sizeof(THEOS_PACKAGE_INSTALL_PREFIX) > 1 ? ({ \
    char outPath[PATH_MAX]; \
    strlcpy(outPath, THEOS_PACKAGE_INSTALL_PREFIX, PATH_MAX); \
    strlcat(outPath, path, PATH_MAX); \
    outPath; \
}) : path

#endif
#endif // __ROOTLESS_H__
