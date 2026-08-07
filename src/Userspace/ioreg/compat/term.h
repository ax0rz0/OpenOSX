/*
 * ioreg.c uses tgetent()/tgetstr()/tputs() only to look up the terminal's
 * bold-on/bold-off escape sequences, and already treats a failed tgetent()
 * as "no bold available" (falls back to empty strings). This project has
 * no termcap/terminfo database at all, so returning "entry not found" is
 * the real, correct answer for every TERM value - not a placeholder for
 * something unimplemented.
 */
#ifndef _PD_TERM_H
#define _PD_TERM_H

#if defined(__cplusplus)
extern "C" {
#endif

int tgetent(char *bp, const char *name);
char *tgetstr(const char *id, char **area);
int tputs(const char *str, int affcnt, int (*putc)(int));

#if defined(__cplusplus)
}
#endif

#endif /* _PD_TERM_H */
