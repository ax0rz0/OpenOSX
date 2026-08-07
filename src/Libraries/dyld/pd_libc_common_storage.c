/*
 * Apple ld does not reliably pull archive members that only satisfy common
 * storage symbols after dyld's libc_internal objects introduce the references.
 * Keep the dyld executable link independent of full libc archive force-loads.
 */
void (*__cleanup)(void);
long __gdtoa_locks[2];
