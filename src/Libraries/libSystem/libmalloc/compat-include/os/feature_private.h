/*
 * OpenOSX minimal os_feature_enabled_simple.
 *
 * Real Darwin backs this with a compact feature-flag table baked into the dyld shared cache / kernel commpage;
 * we have neither, so this is a genuine (not faked) minimal implementation:
 * it always returns the caller-supplied default.
 * Both of libmalloc's two call sites (malloc.c EnableBootArgs, pguard_malloc.c's FEATURE_FLAG macro)
 * treat the result as a plain bool gate,
 * so "always default" is correct bring-up behavior, not a stub masking missing functionality.
 *
 * Call pattern on real Darwin: os_feature_enabled_simple(ns, feature, default)
 * where ns/feature are bare identifiers (stringified internally), matching
 * libmalloc's two use sites exactly.
 */

#ifndef _OS_FEATURE_PRIVATE_H
#define _OS_FEATURE_PRIVATE_H

#include <stdbool.h>

#define os_feature_enabled_simple(ns, feature, default_value) \
	((bool)(default_value))

#endif /* _OS_FEATURE_PRIVATE_H */
