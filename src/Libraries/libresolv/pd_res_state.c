/*
 * Storage for the legacy global `_res` resolver-state struct that
 * resolv.h declares `extern` (see res_9_init()'s "_res_static = &_res"
 * path in res_data.c). Needs the real macro-renamed __res_9_state layout,
 * so this (unlike pd_res_compat.c) does include resolv.h - it doesn't
 * define any of the function names resolv.h's macros rename, so there's
 * no naming collision here.
 *
 * The zero-initializer matters: an uninitialized "struct __res_state
 * _res;" is a tentative/common definition, and ld64's archive-member
 * selection doesn't reliably treat a common symbol as satisfying an
 * undefined reference from another archive the way a real definition
 * does - res_data.c.o's "_res_static = &_res" reference then goes
 * unresolved even though nm shows the symbol present in this object.
 * An explicit initializer forces a real BSS definition instead.
 */
#include <resolv.h>

struct __res_state _res = {0};
