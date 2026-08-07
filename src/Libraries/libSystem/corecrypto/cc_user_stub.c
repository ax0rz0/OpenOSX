#include <string.h>
#include <unistd.h>

void darling_corecrypro_stub(const char *function) {
    // Not fprintf(stderr, ...): this file is built as part of
    // host_corecrypto_static (see host_corecrypto_static.cmake) with real
    // Darwin SDK headers (__APPLE__ defined) so its own struct/type layout
    // stays self-consistent, but it links against the real native glibc at
    // the end - Darwin's <stdio.h> stderr macro expands to a reference to
    // __stderrp, a Darwin-libc-internal symbol that doesn't exist in
    // glibc, so it fails at link time. write() to fd 2 needs no such
    // Darwin-specific stdio symbol.
    static const char prefix[] = "STUB FUNCTION IN CORECRYPTO ";
    static const char suffix[] = " CALLED!\n";
    write(2, prefix, sizeof(prefix) - 1);
    write(2, function, strlen(function));
    write(2, suffix, sizeof(suffix) - 1);
}
