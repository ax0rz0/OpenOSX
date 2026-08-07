/*
 * flsl.c (OpenOSX) -- find last set bit in a long.
 *
 * The open libplatform drop ships ffsll.c/flsll.c but not flsl(). dyld (via the
 * libc sort helpers) references _flsl, so provide it here alongside the rest of
 * the fls/ffs family. On LP64 a long is 64-bit, identical to flsll's argument.
 */
#include <TargetConditionals.h>
#include <sys/cdefs.h>
#include <strings.h>

int
flsl(long mask)
{
#if __has_builtin(__builtin_flsl)
	return __builtin_flsl(mask);
#elif __has_builtin(__builtin_clzl)
	if (mask == 0)
		return (0);
	return (sizeof(mask) << 3) - __builtin_clzl(mask);
#else
	int bit;
	if (mask == 0)
		return (0);
	for (bit = 1; mask != 1; bit++)
		mask = (unsigned long)mask >> 1;
	return (bit);
#endif
}
