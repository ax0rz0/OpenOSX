#ifndef OPENOSX_DYLD_COMPAT_SYS_MMAN_H
#define OPENOSX_DYLD_COMPAT_SYS_MMAN_H

#include_next <sys/mman.h>

/* XNU exposes this syscall to dyld, while the SDK's public header omits it. */
#ifndef OPENOSX_MREMAP_ENCRYPTED_DECL
#define OPENOSX_MREMAP_ENCRYPTED_DECL
#include <stdint.h>
int mremap_encrypted(void *, size_t, uint32_t, uint32_t, uint32_t);
#endif

#endif /* OPENOSX_DYLD_COMPAT_SYS_MMAN_H */
