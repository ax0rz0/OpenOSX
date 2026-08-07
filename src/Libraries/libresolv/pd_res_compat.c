/*
 * resolv.h unconditionally #defines the modern plain resolver API names
 * (res_init, res_query, res_search, res_send, res_mkquery,
 * res_querydomain, res_close/res_nclose, res_ninit/res_nsend/res_nquery/
 * res_nsearch/res_nmkquery) away to their BIND-8-compat "res_9_*" spelling
 * whenever __BIND_NOSTATIC isn't defined - which every other .c file in
 * this archive relies on being active, since res_data.c's non-USE__RES_9
 * branch needs the `_res` global that only exists when __BIND_NOSTATIC is
 * NOT defined. So the archive only ever *defines* res_9_* symbols. Forward
 * the plain names callers actually link against (getaddrinfo, third-party
 * ports) to the res_9_* entry points here, in a TU that never includes
 * resolv.h at all, so none of this renaming applies to it.
 */
#include <sys/types.h>

extern int res_9_init(void);
extern int res_9_query(const char *, int, int, unsigned char *, int);
extern int res_9_search(const char *, int, int, unsigned char *, int);
extern int res_9_querydomain(const char *, const char *, int, int, unsigned char *, int);
extern int res_9_mkquery(int, const char *, int, int, const unsigned char *, int, const unsigned char *, unsigned char *, int);
extern int res_9_send(const unsigned char *, int, unsigned char *, int);
extern void res_9_close(void);

int res_init(void) { return res_9_init(); }
int res_query(const char *n, int c, int t, unsigned char *a, int al) { return res_9_query(n, c, t, a, al); }
int res_search(const char *n, int c, int t, unsigned char *a, int al) { return res_9_search(n, c, t, a, al); }
int res_querydomain(const char *n, const char *d, int c, int t, unsigned char *a, int al) { return res_9_querydomain(n, d, c, t, a, al); }
int res_mkquery(int op, const char *dn, int c, int t, const unsigned char *d, int dl, const unsigned char *nr, unsigned char *buf, int bl) {
	return res_9_mkquery(op, dn, c, t, d, dl, nr, buf, bl);
}
int res_send(const unsigned char *buf, int bl, unsigned char *ans, int al) { return res_9_send(buf, bl, ans, al); }
void res_close(void) { res_9_close(); }
