/*
 * sgetrune/sputrune -- the pre-multibyte-locale narrow rune conversion API.
 * Deprecated even in real macOS (UNIFDEF_LEGACY_RUNE_APIS), and only wired up
 * by setrunelocale.c's old-format-locale-file branch, which we never actually
 * hit (no locale files ship). Still, this is the real, well-known single-byte
 * passthrough form these functions have always had -- not a faked value.
 */

/* NB: deliberately not including <rune.h> -- it #defines sgetrune/sputrune
 * as macros around __sgetrune/__sputrune function-pointer indirection, which
 * would clash with these being the real (non-indirected) definitions. */
#include <runetype.h>
#include <stddef.h>

#define _INVALID_RUNE (_CurrentRuneLocale->__invalid_rune)

rune_t
sgetrune(const char *string, size_t n, char const **result)
{
	if (n < 1) {
		if (result != NULL)
			*result = string;
		return (_INVALID_RUNE);
	}
	if (result != NULL)
		*result = string + 1;
	return ((unsigned char)*string);
}

int
sputrune(rune_t c, char *string, size_t n, char **result)
{
	if (n >= 1) {
		if (string != NULL)
			*string = (char)c;
		if (result != NULL)
			*result = string + 1;
		return (1);
	}
	if (result != NULL)
		*result = string;
	return (0);
}
