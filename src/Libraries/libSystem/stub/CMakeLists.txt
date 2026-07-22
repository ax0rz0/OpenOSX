add_darwin_shared_library(libSystem_B_stub
    MACOSX_VERSION_MIN 11.0
    INSTALL_NAME /usr/lib/libSystem.B.dylib)
set_property(TARGET libSystem_B_stub PROPERTY OUTPUT_NAME "System.B")
add_custom_command(TARGET libSystem_B_stub POST_BUILD
    COMMAND "${CMAKE_COMMAND}" -E create_symlink
        "$<TARGET_FILE_NAME:libSystem_B_stub>"
        "$<TARGET_FILE_DIR:libSystem_B_stub>/libSystem.dylib"
    VERBATIM)

target_sources(libSystem_B_stub PRIVATE
    empty.c
    pd_libSystem_init.c
    pd_libSystem_compat.c
    pd_chkstk_darwin.s
    ${CMAKE_SOURCE_DIR}/src/Libraries/dyld/pd_libc_common_storage.c)

target_link_options(libSystem_B_stub PRIVATE
    -compatibility_version 1.0.0
    -current_version 1292.100.5)

get_target_property(_libsystem_b_copts libSystem_B_stub COMPILE_OPTIONS)
if(_libsystem_b_copts)
    list(REMOVE_ITEM _libsystem_b_copts -nostdinc)
    set_target_properties(libSystem_B_stub PROPERTIES COMPILE_OPTIONS "${_libsystem_b_copts}")
endif()
target_compile_options(libSystem_B_stub PRIVATE -isysroot ${PUREDARWIN_MACOSX_SDK})
target_link_options(libSystem_B_stub PRIVATE -isysroot ${PUREDARWIN_MACOSX_SDK})

set(LIBSYSTEM_EXPORTS ${CMAKE_CURRENT_SOURCE_DIR}/libSystem.exports)
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS ${LIBSYSTEM_EXPORTS})
file(STRINGS ${LIBSYSTEM_EXPORTS} _libsystem_export_lines)
set(_libsystem_u_flags)
foreach(_line ${_libsystem_export_lines})
    string(STRIP "${_line}" _line)
    if(_line AND NOT _line MATCHES "^#")
        list(APPEND _libsystem_u_flags "-Wl,-u,${_line}")
    endif()
endforeach()
add_dependencies(libSystem_B_stub
    libsystem_kernel_static libplatform_static pthread_full_static
    libc_static libc_internal_static libmalloc_static
    corecrypto_static CrashReporterClient libresolv_static
    libdispatch_static openlibm_static XPC_notify_client_static XPC_notifyd_mig_static
    XPC_libxpc_static XPC_libnv_static XPC_launchd_mig_static)
target_link_options(libSystem_B_stub PRIVATE
    -Wl,-force_load,$<TARGET_FILE:XPC_notify_client_static>
    -Wl,-force_load,$<TARGET_FILE:XPC_libnv_static>
    -Wl,-force_load,$<TARGET_FILE:XPC_libxpc_static>
    $<TARGET_FILE:XPC_notifyd_mig_static>
    $<TARGET_FILE:XPC_launchd_mig_static>)
if(PUREDARWIN_USE_LD64_LLD)
    target_link_libraries(libSystem_B_stub PRIVATE
        $<TARGET_FILE:libplatform_static>
        $<TARGET_FILE:pthread_full_static>
        $<TARGET_FILE:libc_static>
        $<TARGET_FILE:libc_internal_static>
        $<TARGET_FILE:openlibm_static>
        $<TARGET_FILE:libmalloc_static>
        $<TARGET_FILE:libdispatch_static>
        $<TARGET_FILE:libsystem_kernel_static>
        ${CMAKE_BINARY_DIR}/src/Libraries/libSystem/libsystem_kernel/syscalls.a
        $<TARGET_FILE:corecrypto_static>
        $<TARGET_FILE:CrashReporterClient>
        $<TARGET_FILE:libresolv_static>)
else()
    target_link_libraries(libSystem_B_stub PRIVATE
        $<TARGET_FILE:libsystem_kernel_static>
        $<TARGET_FILE:libplatform_static>
        $<TARGET_FILE:pthread_full_static>
        $<TARGET_FILE:libc_static>
        $<TARGET_FILE:libc_internal_static>
        $<TARGET_FILE:openlibm_static>
        $<TARGET_FILE:libmalloc_static>
        $<TARGET_FILE:libdispatch_static>
        ${CMAKE_BINARY_DIR}/src/Libraries/libSystem/libsystem_kernel/syscalls.a
        $<TARGET_FILE:corecrypto_static>
        $<TARGET_FILE:CrashReporterClient>
        $<TARGET_FILE:libresolv_static>)
endif()
if(TARGET libcxxabi_static)
    add_dependencies(libSystem_B_stub libcxxabi_static_build)
    target_link_libraries(libSystem_B_stub PRIVATE $<TARGET_FILE:libcxxabi_static>)
endif()
if(TARGET libunwind_static)
    add_dependencies(libSystem_B_stub libunwind_static_build)
    target_link_libraries(libSystem_B_stub PRIVATE $<TARGET_FILE:libunwind_static>)
endif()
if(TARGET libcxx_static)
    add_dependencies(libSystem_B_stub libcxx_static_build)
    target_link_libraries(libSystem_B_stub PRIVATE $<TARGET_FILE:libcxx_static>)
endif()
if(TARGET commoncrypto_static)
    target_link_libraries(libSystem_B_stub PRIVATE $<TARGET_FILE:commoncrypto_static>)
endif()
target_link_options(libSystem_B_stub PRIVATE
    ${_libsystem_u_flags}
    # -dead_strip (as dyld's own link uses): resolve the unavoidable duplicate
    # definitions that overlapping static archives carry (mem/str primitives in
    # both libplatform and libc/libsystem_kernel; pthread funcptr wrappers in both
    # libsystem_kernel and pthread_static) by keeping only atoms reachable from
    # the exported roots and stripping the rest, instead of erroring on the dup.
    -Wl,-dead_strip
    -Wl,-fixup_chains
    -Wl,-exported_symbols_list,${LIBSYSTEM_EXPORTS})
if(PUREDARWIN_USE_LD64_LLD)
    # ld64.lld diagnoses duplicate archive members before ordinary dead_strip
    # can discard them; Apple's ld64 accepts this link. Keep the compatibility
    # switch scoped to the Nix/LLVM linker path.
    target_link_options(libSystem_B_stub PRIVATE -Wl,--dead-strip-duplicates)
endif()
set_property(TARGET libSystem_B_stub APPEND PROPERTY LINK_DEPENDS ${LIBSYSTEM_EXPORTS})

add_dependencies(libSystem_B_stub libdyld)
target_link_libraries(libSystem_B_stub PRIVATE $<TARGET_FILE:libdyld>)
target_link_options(libSystem_B_stub PRIVATE
    -Wl,-reexport_library,$<TARGET_FILE:libdyld>)

install(TARGETS libSystem_B_stub LIBRARY DESTINATION usr/lib COMPONENT BaseSystem)
install_symlink(usr/lib/libSystem.dylib libSystem.B.dylib COMPONENT BaseSystem)
install_symlink(usr/lib/libc.dylib libSystem.B.dylib COMPONENT BaseSystem)
install_symlink(usr/lib/libm.dylib libSystem.B.dylib COMPONENT BaseSystem)
