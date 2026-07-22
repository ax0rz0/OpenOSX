mig(job.defs
    INCLUDES_FROM_TARGET XPC_libxpc_static
    USER_HEADER ${CMAKE_CURRENT_BINARY_DIR}/job.h
    USER_SOURCE ${CMAKE_CURRENT_BINARY_DIR}/job_user.c
    NOVOUCHERS)
mig(job_forward.defs
    INCLUDES_FROM_TARGET XPC_libxpc_static
    USER_HEADER ${CMAKE_CURRENT_BINARY_DIR}/job_forward.h
    USER_SOURCE ${CMAKE_CURRENT_BINARY_DIR}/job_forward_user.c
    NOVOUCHERS)
mig(job_reply.defs
    INCLUDES_FROM_TARGET XPC_libxpc_static
    USER_HEADER ${CMAKE_CURRENT_BINARY_DIR}/job_reply.h
    USER_SOURCE ${CMAKE_CURRENT_BINARY_DIR}/job_replyUser.c
    SERVER_HEADER ${CMAKE_CURRENT_BINARY_DIR}/job_reply_server.h
    SERVER_SOURCE ${CMAKE_CURRENT_BINARY_DIR}/job_replyServer.c
    NOVOUCHERS)
mig(internal.defs
    INCLUDES_FROM_TARGET XPC_libxpc_static
    USER_HEADER ${CMAKE_CURRENT_BINARY_DIR}/internal.h
    USER_SOURCE ${CMAKE_CURRENT_BINARY_DIR}/internal_user.c
    NOVOUCHERS)

add_library(XPC_launchd_mig_static STATIC
    ${CMAKE_CURRENT_BINARY_DIR}/job_user.c
    ${CMAKE_CURRENT_BINARY_DIR}/job_forward_user.c
    ${CMAKE_CURRENT_BINARY_DIR}/job_replyUser.c
    ${CMAKE_CURRENT_BINARY_DIR}/job_replyServer.c
    ${CMAKE_CURRENT_BINARY_DIR}/internal_user.c
)
target_compile_definitions(XPC_launchd_mig_static PRIVATE __DARWIN_ONLY_UNIX_CONFORMANCE=1)
target_compile_options(XPC_launchd_mig_static PRIVATE
    $<$<COMPILE_LANGUAGE:C>:-include${CMAKE_CURRENT_SOURCE_DIR}/../../libSystem/libplatform/pd_bridgeos_compat.h>
    -isysroot ${PUREDARWIN_MACOSX_SDK}
    -I${PUREDARWIN_MACOSX_SDK}/usr/include)
target_include_directories(XPC_launchd_mig_static PRIVATE
    ${CMAKE_CURRENT_BINARY_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/../libxpc
    ${CMAKE_CURRENT_SOURCE_DIR}/../libxpc/include
    ${CMAKE_BINARY_DIR}/src/Libraries/libSystem/libc/include/${PUREDARWIN_ARCH}
    ${CMAKE_BINARY_DIR}/src/Libraries/libSystem/libsystem_kernel/public_include
)
add_dependencies(XPC_launchd_mig_static libsystem_kernel_static)
target_link_libraries(XPC_launchd_mig_static PRIVATE
    AvailabilityHeaders
    clang_bootstrap_headers
    libc_public_headers
    libc_features
    pthread_common_headers
    xnu_headers
    xnu_private_headers
)
add_dependencies(XPC_launchd_mig_static libc_features)

# Expose the generated headers to consumers (libxpc) without re-running MIG.
add_library(XPC_launchd_mig_headers INTERFACE)
target_include_directories(XPC_launchd_mig_headers INTERFACE ${CMAKE_CURRENT_BINARY_DIR})
