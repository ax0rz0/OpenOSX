add_library(host_libmacho STATIC)
target_sources(host_libmacho PRIVATE
    arch.c
)
set_property(TARGET host_libmacho PROPERTY OUTPUT_NAME macho)
target_include_directories(host_libmacho PRIVATE ../include ../include/foreign)

# Real x86_64-apple-darwin build of the same source, for self-hosted cctools
# tools that run on OpenOSX itself (as opposed to host_libmacho above,
# which is a Linux-native library used only to drive our own cross-builds).
if(OPENOSX_ENABLE_SELFHOST_CCTOOLS)
    add_darwin_static_library(libmacho_static)
    target_sources(libmacho_static PRIVATE arch.c)
    set_property(TARGET libmacho_static PROPERTY OUTPUT_NAME macho_selfhost)
    target_include_directories(libmacho_static PRIVATE ../include ../include/foreign)
    target_compile_options(libmacho_static PRIVATE -isysroot ${OPENOSX_MACOSX_SDK})
    get_target_property(_selfhost_libmacho_opts libmacho_static COMPILE_OPTIONS)
    if(_selfhost_libmacho_opts)
        list(REMOVE_ITEM _selfhost_libmacho_opts -nostdinc)
        set_target_properties(libmacho_static PROPERTIES COMPILE_OPTIONS "${_selfhost_libmacho_opts}")
    endif()
endif()
