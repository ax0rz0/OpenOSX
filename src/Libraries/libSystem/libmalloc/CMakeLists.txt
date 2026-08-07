add_library(libmalloc_headers INTERFACE)
target_include_directories(libmalloc_headers INTERFACE include)

add_library(libmalloc_private_headers INTERFACE)
target_include_directories(libmalloc_private_headers INTERFACE private)

add_library(libmalloc_static STATIC EXCLUDE_FROM_ALL
    src/malloc.c
    src/malloc_common.c
    src/malloc_printf.c
    src/magazine_malloc.c
    src/magazine_large.c
    src/magazine_medium.c
    src/magazine_small.c
    src/magazine_tiny.c
    src/magazine_rack.c
    src/nano_malloc.c
    src/nano_malloc_common.c
    src/nanov2_malloc.c
    src/pd_nanov2_fallback.c
    src/purgeable_malloc.c
    src/pguard_malloc.c
    src/legacy_malloc.c
    src/frozen_malloc.c
    src/bitarray.c
    src/vm.c
    src/msl_lite_support.c
)

set_property(TARGET libmalloc_static PROPERTY OSX_ARCHITECTURES "${OPENOSX_ARCH}")

target_compile_definitions(libmalloc_static PRIVATE
    MALLOC_TARGET_EXCLAVES=0
    PRIVATE=1)

target_include_directories(libmalloc_static PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/compat-include
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}/private
    ${CMAKE_CURRENT_SOURCE_DIR}/resolver
    ${CMAKE_SOURCE_DIR}/src/Libraries/dyld/upstream/include
    # pthread/private.h, tsd_private.h
    ${CMAKE_CURRENT_SOURCE_DIR}/../pthread/private
    ${CMAKE_CURRENT_SOURCE_DIR}/../pthread/include
    ${CMAKE_CURRENT_SOURCE_DIR}/../libc/gen
    ${CMAKE_CURRENT_SOURCE_DIR}/../libc/private
    ${CMAKE_CURRENT_SOURCE_DIR}/../libsystem_kernel/private_include
    ${CMAKE_CURRENT_SOURCE_DIR}/../libsystem_kernel/mach
    ${CMAKE_BINARY_DIR}/src/Libraries/libSystem/libsystem_kernel/public_include
    ${CMAKE_CURRENT_SOURCE_DIR}/../libplatform/private
    ${CMAKE_CURRENT_SOURCE_DIR}/../libplatform/include
    ${CMAKE_SOURCE_DIR}/src/Kernel/xnu/libkern
    ${OPENOSX_SOURCE_DIR}/src/Kernel/xnu/osfmk
    ${OPENOSX_SOURCE_DIR}/src/Kernel/xnu/bsd)

target_link_libraries(libmalloc_static PRIVATE
    AvailabilityHeaders
    architecture_headers
    clang_bootstrap_headers
    libc_public_headers
    libc_private_headers
    libplatform_headers
    libplatform_private_headers
    xnu_private_headers
    xnu_headers)

target_compile_options(libmalloc_static PRIVATE
    -mno-red-zone -fno-stack-protector
    -isysroot ${OPENOSX_MACOSX_SDK}
    -Wno-availability
    # bridgeos availability shim + PD SDK compat (reuse the existing ones).
    $<$<COMPILE_LANGUAGE:C>:-include${CMAKE_CURRENT_SOURCE_DIR}/../libplatform/pd_bridgeos_compat.h>
    $<$<COMPILE_LANGUAGE:C>:-include${CMAKE_SOURCE_DIR}/src/Libraries/dyld/dyld_sdk_compat.h>)

add_dependencies(libmalloc_static libsystem_kernel_static)
