/*
 * string/FreeBSD/strtok.c implements __strtok_r and publishes strtok_r as a weak
 * alias via __weak_reference(__strtok_r, strtok_r). That macro does not emit a
 * usable Mach-O alias under this (osxcross) toolchain -- __strtok_r is present
 * but _strtok_r is not -- so provide the public strtok_r as a thin forwarder.
 */
#include <string.h>

extern char *__strtok_r(char *, const char *, char **);

char *
strtok_r(char *s, const char *sep, char **lasts)
{
	return __strtok_r(s, sep, lasts);
}
