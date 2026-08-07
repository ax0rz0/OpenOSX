add_library(host_corecrypto_static STATIC)
target_include_directories(host_corecrypto_static PUBLIC include)
target_include_directories(host_corecrypto_static PRIVATE private)
# host_corecrypto_static is only ever a host build tool dependency (of
# host_ld) but still wants genuine Darwin SDK headers (Mach/libkern types).
# Our vendored SDK's own <stdint.h>/<sys/_types.h> chain #include_next's
# expecting to layer on a COMPATIBLE libc underneath - layered on glibc
# instead (our native host libc) it hard-conflicts on __int64_t et al
# (Darwin: "long long", glibc/x86_64: "long", same width, different type).
# -nostdlibinc keeps clang's own resource-dir builtins (stdarg.h etc, added
# regardless) but drops glibc's /usr/include, so the SDK's own headers are
# the only libc in play - exactly what a real -target ...-apple-macosx
# compile gets, just without changing the actual (native ELF) codegen target.
# NIX_NATIVE_DARWIN_HEADER_FLAGS (-isysroot/-D__APPLE__/etc, set by
# build.nix) is scoped to just this target and host_commoncrypto_static,
# not applied project-wide - see build.nix for why.
if(DEFINED ENV{NIX_NATIVE_DARWIN_HEADER_FLAGS})
    separate_arguments(_nix_darwin_hdr_flags UNIX_COMMAND "$ENV{NIX_NATIVE_DARWIN_HEADER_FLAGS}")
    target_compile_options(host_corecrypto_static PRIVATE -nostdlibinc ${_nix_darwin_hdr_flags})
    target_include_directories(host_corecrypto_static PRIVATE $ENV{NIX_NATIVE_DARWIN_HEADER_DIRS})
endif()

# cc_user_stub.c (below) is deliberately built as its own plain-native
# object, NOT part of host_corecrypto_static's Darwin-spoofed compile: it
# only wants write(2) to report a stub call, but under -D__APPLE__ the
# SDK's <unistd.h> asm-aliases write() to the symbol "_write" (Darwin's
# leading-underscore libc convention) - a symbol that doesn't exist when
# actually linked against real glibc.
add_library(host_cc_user_stub OBJECT cc_user_stub.c)

add_library(host_corecrypto_headers INTERFACE)
target_include_directories(host_corecrypto_headers INTERFACE include)

add_library(host_corecrypto_private_headers INTERFACE)
target_include_directories(host_corecrypto_private_headers INTERFACE include)

target_sources(host_corecrypto_static PRIVATE
    src/cc.c
    src/ccdigest.c
    src/ccmd2.c
    src/ccmd4.c
    src/ccmd5.c
    src/ccder.c
    src/ccec.c
    src/ccdh.c
    src/ccdh_gp.c
    src/ccaes.c
    src/ccsha2xx.c
    src/ccsha3xx.c
    src/cczp.c
    src/ccsha1.c
    src/ccrsa.c
    src/ccrng.c
    src/ccrng_system.c
    src/ccrc4.c
    src/ccn.c
    src/ccmode.c
    src/ccdes.c
    src/ccrsa_priv.c
    src/cccast.c
    src/ccrc2.c
    src/ccblowfish.c
    src/ccnistkdf.c
    src/ccz.c
    src/cccmac.c
    src/ccripemd.c
    src/cchkdf.c
    src/cchmac.c
    src/ccpad.c
    src/ccpbkdf2.c
    src/ccrc4.c
    src/ccansikdf.c
    src/ccecies.c
    src/ccrng_pbkdf2.c
    src/ccec_projective_point.c
    src/ccec_points.c
    src/ccn_extra.c
    src/cczp_extra.c
    src/ccgcm.c
    src/cch2c.c
    src/ccsrp.c
    src/ccwrap_priv.c
    src/cc_priv.c
    src/ccec25519.c
    src/cccbc.c
    src/ccccm.c
    src/cccfb.c
    src/cccfb8.c
    src/ccchacha20poly1305.c
    src/ccctr.c
    src/ccofb.c
    src/ccxts.c
    src/ccckg.c
    $<TARGET_OBJECTS:host_cc_user_stub>
)
