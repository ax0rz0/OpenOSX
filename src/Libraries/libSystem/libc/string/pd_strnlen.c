/*
 * Real, standalone strnlen/strlcpy/strlcat.
 *
 * libplatform and libsystem_kernel's _libc_funcptr.c.o both carry private
 * (hidden/private_extern) copies of several string functions -- unlike their
 * sibling strlen/strncmp/memcpy/etc, which end up correctly resolving to a
 * public symbol at final link, these three consistently end up hidden in
 * the linked libSystem.B.dylib no matter what's listed in libSystem.exports
 * (an export list can only keep a symbol visible, never un-hide one that's
 * private_extern at the object level). Plain public definitions here avoid
 * depending on which duplicate the linker happens to keep. (index and
 * memccpy hit the same bug; index's fix lives in stub/pd_libSystem_compat.c,
 * and memccpy just needed its hidden _libc_funcptr.c dup removed since
 * libplatform already has a working public one.)
 */
#include <stddef.h>

size_t
strnlen(const char *s, size_t maxlen)
{
	size_t len;

	for (len = 0; len < maxlen; len++, s++) {
		if (!*s) {
			break;
		}
	}
	return len;
}

size_t
strlcpy(char *dst, const char *src, size_t dstsize)
{
	size_t srclen = 0;

	while (src[srclen] != '\0') {
		srclen++;
	}

	if (dstsize != 0) {
		size_t copylen = srclen < dstsize - 1 ? srclen : dstsize - 1;
		size_t i;

		for (i = 0; i < copylen; i++) {
			dst[i] = src[i];
		}
		dst[copylen] = '\0';
	}

	return srclen;
}

size_t
strlcat(char *dst, const char *src, size_t dstsize)
{
	size_t dstlen = 0;
	size_t srclen = 0;
	size_t room;
	size_t copylen;
	size_t i;

	while (dstlen < dstsize && dst[dstlen] != '\0') {
		dstlen++;
	}
	if (dstlen == dstsize) {
		return dstsize + __builtin_strlen(src);
	}

	while (src[srclen] != '\0') {
		srclen++;
	}

	room = dstsize - dstlen - 1;
	copylen = srclen < room ? srclen : room;

	for (i = 0; i < copylen; i++) {
		dst[dstlen + i] = src[i];
	}
	dst[dstlen + copylen] = '\0';

	return dstlen + srclen;
}
