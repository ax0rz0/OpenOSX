# TAPI_SUPPORT changes the layout of the Options class (Options.h grows
# fTAPIFiles/fPreferTAPIFile members), so it must be defined for EVERY TU
# that includes Options.h - directory-scoped here so parsers/ and passes/
# (added below) inherit it too, never per-target.
if(OPENOSX_LIBTAPI_INCLUDE_DIR AND OPENOSX_LIBTAPI_LIBRARY)
    add_compile_definitions(TAPI_SUPPORT=1)
    include_directories(${OPENOSX_LIBTAPI_INCLUDE_DIR})
endif()

add_subdirectory(parsers)
add_subdirectory(passes)

find_package(OpenSSL REQUIRED)

add_executable(host_ld)
target_sources(host_ld PRIVATE
    debugline.c
    InputFiles.cpp
    ld.cpp
    libcodedirectory.c
    Options.cpp
    OutputFile.cpp
    Resolver.cpp
    ResponseFiles.cpp
    Snapshot.cpp
    SymbolTable.cpp
    PlatformSupport.cpp
    code-sign-blobs/blob.cpp
)

target_compile_definitions(host_ld PRIVATE __DARWIN_UNIX03)
# -fblocks: libcodedirectory.c's signature_ctx union has a
# libcd_signature_generator_block member gated on __has_extension(blocks) -
# real Apple/Darwin-target clang enables the Blocks extension by default,
# but a plain native-Linux-target clang (see build.nix's native-ld
# configure) doesn't, so that member silently disappears without this.
target_compile_options(host_ld PRIVATE "-Wno-#warnings" -Wno-deprecated -fblocks)
target_link_libraries(host_ld PRIVATE
    host_libxar host_ld_helper host_ld_BlocksRuntime host_ld_libparsers host_ld_libpasses host_libstuff
    OpenSSL::SSL OpenSSL::Crypto
    host_commoncrypto_private_headers
    host_corecrypto_static
    host_corecrypto_headers
)
if(OPENOSX_LIBTAPI_INCLUDE_DIR AND OPENOSX_LIBTAPI_LIBRARY)
    target_link_libraries(host_ld PRIVATE ${OPENOSX_LIBTAPI_LIBRARY})
endif()
set_property(TARGET host_ld PROPERTY OUTPUT_NAME ld)
set_property(TARGET host_ld PROPERTY CXX_STANDARD 17)
target_include_directories(host_ld PRIVATE
    ../../../include
    ../../../include/foreign
    ..
    ../..
    ../abstraction
    ../3rd
    ../3rd/include
    ../3rd/BlocksRuntime
    ${CMAKE_CURRENT_SOURCE_DIR}
    parsers passes
    ${OPENSSL_INCLUDE_DIR}
)
