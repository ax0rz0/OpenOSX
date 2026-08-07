# host_ld (PD's own in-tree ld64, tools/cctools/ld64) is an add_executable()
# target, so osxcross's toolchain.cmake, which forces CMAKE_SYSTEM_NAME
# Darwin globally, builds it as an unusable Mach-O binary instead of a
# native ELF one (the same Canadian-cross trap migcom's dead in-tree target
# has). osxcross ships its own native, TAPI-capable ld (built for the host,
# targeting Darwin) as "<triple>-ld"; prefer that, matching migcom's
# seeded-via-package-manager pattern, and only fall back to host_ld if it
# can't be found.
if(OPENOSX_NIX_TOOLCHAIN AND DEFINED ENV{NIX_NATIVE_LD_PATH})
    set(NATIVE_LD64_EXECUTABLE "$ENV{NIX_NATIVE_LD_PATH}" CACHE FILEPATH "Native host ld64 for Darwin links" FORCE)
endif()

set(OPENOSX_USE_LD64_LLD FALSE)
if(OPENOSX_NIX_TOOLCHAIN AND NOT NATIVE_LD64_EXECUTABLE)
    set(OPENOSX_USE_LD64_LLD TRUE)
endif()

if(NOT CMAKE_HOST_APPLE AND NOT NATIVE_LD64_EXECUTABLE)
    get_filename_component(_ld64_ar_name "${CMAKE_AR}" NAME)
    if(_ld64_ar_name MATCHES "^(.+)-ar$")
        find_program(NATIVE_LD64_EXECUTABLE NAMES "${CMAKE_MATCH_1}-ld")
    endif()
endif()

function(add_darwin_executable name)
    cmake_parse_arguments(SL "NO_STANDARD_LIBRARIES;USE_HOST_SDK" "MACOSX_VERSION_MIN" "" ${ARGN})

    add_executable(${name})
    target_compile_definitions(${name} PRIVATE __OPENOSX__)
    if(CMAKE_HOST_APPLE)
        # PD own ld64 (host_ld) is built without TAPI support, so it cannot
        # resolve the SDK .tbd text-stub files (e.g. libSystem.tbd), fall
        # back to the real Apple linker via xcrun, which handles them fine.
        execute_process(COMMAND xcrun --find ld OUTPUT_VARIABLE XCRUN_LD OUTPUT_STRIP_TRAILING_WHITESPACE)
        target_link_options(${name} PRIVATE -fuse-ld=${XCRUN_LD})
    elseif(NATIVE_LD64_EXECUTABLE)
        target_link_options(${name} PRIVATE -fuse-ld=${NATIVE_LD64_EXECUTABLE})
    elseif(OPENOSX_USE_LD64_LLD)
        target_link_options(${name} PRIVATE -fuse-ld=lld)
    else()
        add_dependencies(${name} host_ld)
        target_link_options(${name} PRIVATE -fuse-ld=$<TARGET_FILE:host_ld>)
    endif()

    if(NOT SL_USE_HOST_SDK)
        target_compile_options(${name} PRIVATE -nostdlib -nostdinc)
        target_link_options(${name} PRIVATE -nostdlib)
        set_property(TARGET ${name} PROPERTY OSX_ARCHITECTURES "${OPENOSX_ARCH}")
    endif()

    # TODO: Handle SL_NO_STANDARD_LIBRARIES here, once the libraries have been added to the build.

    if(SL_MACOSX_VERSION_MIN)
        set_property(TARGET ${name} PROPERTY CMAKE_OSX_DEPLOYMENT_TARGET ${SL_MACOSX_VERSION_MIN})
    elseif(CMAKE_MACOSX_MIN_VERSION)
        set_property(TARGET ${name} PROPERTY CMAKE_OSX_DEPLOYMENT_TARGET ${CMAKE_MACOSX_MIN_VERSION})
    else()
        message(AUTHOR_WARNING "Could not determine -mmacosx-version-min flag for target ${name}")
    endif()
endfunction()

function(add_darwin_static_library name)
    cmake_parse_arguments(SL "USE_HOST_SDK" "MACOSX_VERSION_MIN" "" ${ARGN})

    add_library(${name} STATIC)
    if(TARGET host_libtool)
        add_dependencies(${name} host_libtool)
    endif()
    target_compile_definitions(${name} PRIVATE __OPENOSX__)

    if(SL_MACOSX_VERSION_MIN)
        set_property(TARGET ${name} PROPERTY CMAKE_OSX_DEPLOYMENT_TARGET ${SL_MACOSX_VERSION_MIN})
    elseif(CMAKE_MACOSX_MIN_VERSION)
        set_property(TARGET ${name} PROPERTY CMAKE_OSX_DEPLOYMENT_TARGET ${CMAKE_MACOSX_MIN_VERSION})
    else()
        message(AUTHOR_WARNING "Could not determine -mmacosx-version-min flag for target ${name}")
    endif()

    if(NOT SL_USE_HOST_SDK)
        target_compile_options(${name} PRIVATE -nostdlib -nostdinc)
        set_property(TARGET ${name} PROPERTY OSX_ARCHITECTURES "${OPENOSX_ARCH}")
    endif()
endfunction()

function(add_darwin_shared_library name)
    cmake_parse_arguments(SL "MODULE;USE_HOST_SDK" "MACOSX_VERSION_MIN;INSTALL_NAME_DIR;INSTALL_NAME" "RPATHS" ${ARGN})

    if(SL_MODULE)
        add_library(${name} MODULE)
    else()
        add_library(${name} SHARED)
    endif()

    if(CMAKE_HOST_APPLE)
        # PD own ld64 (host_ld) is built without TAPI support, so it cannot
        # resolve the SDK .tbd text-stub files (e.g. libSystem.tbd), fall
        # back to the real Apple linker via xcrun, which handles them fine.
        execute_process(COMMAND xcrun --find ld OUTPUT_VARIABLE XCRUN_LD OUTPUT_STRIP_TRAILING_WHITESPACE)
        target_link_options(${name} PRIVATE -fuse-ld=${XCRUN_LD})
    elseif(NATIVE_LD64_EXECUTABLE)
        target_link_options(${name} PRIVATE -fuse-ld=${NATIVE_LD64_EXECUTABLE})
    elseif(OPENOSX_USE_LD64_LLD)
        target_link_options(${name} PRIVATE -fuse-ld=lld)
    else()
        add_dependencies(${name} host_ld)
        target_link_options(${name} PRIVATE -fuse-ld=$<TARGET_FILE:host_ld>)
    endif()
    target_compile_definitions(${name} PRIVATE __OPENOSX__)

    string(SUBSTRING ${name} 0 3 name_prefix)
    if(name_prefix STREQUAL "lib")
        string(SUBSTRING ${name} 3 -1 base_name)
        set_property(TARGET ${name} PROPERTY OUTPUT_NAME ${base_name})
        set_property(TARGET ${name} PROPERTY PREFIX lib)
    endif()
    set_property(TARGET ${name} PROPERTY SUFFIX .dylib)

    if(NOT SL_USE_HOST_SDK)
        target_compile_options(${name} PRIVATE -nostdlib -nostdinc)
        target_link_options(${name} PRIVATE -nostdlib)

        set_property(TARGET ${name} PROPERTY OSX_ARCHITECTURES "${OPENOSX_ARCH}")
    endif()

    if(SL_MACOSX_VERSION_MIN)
        set_property(TARGET ${name} PROPERTY CMAKE_OSX_DEPLOYMENT_TARGET ${SL_MACOSX_VERSION_MIN})
    elseif(CMAKE_MACOSX_MIN_VERSION)
        set_property(TARGET ${name} PROPERTY CMAKE_OSX_DEPLOYMENT_TARGET ${CMAKE_MACOSX_MIN_VERSION})
    else()
        message(AUTHOR_WARNING "Could not determine -mmacosx-version-min flag for target ${name}")
    endif()

    # NB: our link-time `-install_name` flag (above/below) is NOT the whole
    # story. CMake's OWN install(TARGETS) rule for SHARED_LIBRARY independently
    # re-runs `install_name_tool -id` at install time, using the target's real
    # CMake INSTALL_NAME_DIR property (a built-in property, unrelated to our
    # SL_INSTALL_NAME_DIR macro argument above despite the name collision) --
    # and if that property is unset, CMake's install rule passes just the bare
    # filename ("libfoo.dylib"), silently clobbering whatever full path the
    # link step embedded. <rdar://n/a, PD-local>: every dylib built through
    # this helper had its correct /usr/lib/... install name overwritten with a
    # bare filename at `cmake --install` time, e.g. libdyld.dylib installed
    # with id "libdyld.dylib" instead of "/usr/lib/system/libdyld.dylib",
    # which made real dyld's libDyldPath() identity check
    # (ImageLoaderMachO.cpp's setupLazyPointerHandler) fail and throw
    # "__dyld section not supported in ...". Setting the real INSTALL_NAME_DIR
    # property here keeps CMake's own install-time rewrite consistent with our
    # link-time flag instead of undoing it.
    if(SL_INSTALL_NAME_DIR)
        target_link_options(${name} PRIVATE -install_name "${SL_INSTALL_NAME_DIR}/$<TARGET_FILE_NAME:${name}>")
        set_property(TARGET ${name} PROPERTY INSTALL_NAME_DIR "${SL_INSTALL_NAME_DIR}")
        set_property(TARGET ${name} PROPERTY BUILD_WITH_INSTALL_NAME_DIR FALSE)
        set_property(TARGET ${name} PROPERTY NO_SONAME TRUE)
    elseif(SL_INSTALL_NAME)
        target_link_options(${name} PRIVATE -install_name ${SL_INSTALL_NAME})
        get_filename_component(_sl_install_name_dir "${SL_INSTALL_NAME}" DIRECTORY)
        set_property(TARGET ${name} PROPERTY INSTALL_NAME_DIR "${_sl_install_name_dir}")
        set_property(TARGET ${name} PROPERTY BUILD_WITH_INSTALL_NAME_DIR FALSE)
        set_property(TARGET ${name} PROPERTY NO_SONAME TRUE)
    elseif(NOT SL_MODULE)
        message(WARNING "Shared library target ${name} should have INSTALL_NAME_DIR defined")
    endif()

    foreach(rpath IN LISTS SL_RPATHS)
        target_link_options(${name} PRIVATE "SHELL:-rpath ${rpath}")
    endforeach()
endfunction()

function(add_darwin_object_library name)
    cmake_parse_arguments(SL "USE_HOST_SDK" "MACOSX_VERSION_MIN" "" ${ARGN})

    add_library(${name} OBJECT)
    set_property(TARGET ${name} PROPERTY LINKER_LANGUAGE C)
    target_compile_definitions(${name} PRIVATE __OPENOSX__)

    if(SL_MACOSX_VERSION_MIN)
        set_property(TARGET ${name} PROPERTY CMAKE_OSX_DEPLOYMENT_TARGET ${SL_MACOSX_VERSION_MIN})
    elseif(CMAKE_MACOSX_MIN_VERSION)
        set_property(TARGET ${name} PROPERTY CMAKE_OSX_DEPLOYMENT_TARGET ${CMAKE_MACOSX_MIN_VERSION})
    else()
        message(AUTHOR_WARNING "Could not determine -mmacosx-version-min flag for target ${name}")
    endif()

    if(NOT SL_USE_HOST_SDK)
        target_compile_options(${name} PRIVATE -nostdlib -nostdinc)
        set_property(TARGET ${name} PROPERTY OSX_ARCHITECTURES "${OPENOSX_ARCH}")
    endif()
endfunction()

set(CMAKE_SKIP_RPATH TRUE)
set(CMAKE_SKIP_BUILD_RPATH TRUE)
set(CMAKE_SKIP_INSTALL_RPATH TRUE)
set(CMAKE_BUILD_WITH_INSTALL_RPATH FALSE)
set(CMAKE_MACOSX_RPATH FALSE)
