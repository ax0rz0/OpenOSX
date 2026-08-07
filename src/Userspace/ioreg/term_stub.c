/*
 * See compat/term.h: this project has no termcap/terminfo database, so
 * "entry not found" (tgetent returning -1, per its real documented
 * contract) is the correct real answer, not a stand-in stub.
 */
#include "compat/term.h"

int
tgetent(char *bp, const char *name)
{
	(void)bp;
	(void)name;
	return -1;
}

char *
tgetstr(const char *id, char **area)
{
	(void)id;
	(void)area;
	return (char *)0;
}

int
tputs(const char *str, int affcnt, int (*putc)(int))
{
	(void)affcnt;
	if (str == (char *)0) {
		return 0;
	}
	while (*str) {
		putc((unsigned char)*str++);
	}
	return 0;
}
