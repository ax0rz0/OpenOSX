/*
 * Minimal compat stub for a non-Apple build (see tools/cctools's other
 * include/foreign/* shims): real os/availability.h pulls in Apple's whole
 * AvailabilityInternal.h macro system to annotate API version history.
 * host_ld only needs these macros to exist and expand to nothing - it
 * doesn't care about deployment-target availability checking.
 */
#ifndef __OS_AVAILABILITY__
#define __OS_AVAILABILITY__

#ifndef API_AVAILABLE
#define API_AVAILABLE(...)
#endif
#ifndef API_DEPRECATED
#define API_DEPRECATED(...)
#endif
#ifndef API_DEPRECATED_WITH_REPLACEMENT
#define API_DEPRECATED_WITH_REPLACEMENT(...)
#endif
#ifndef API_UNAVAILABLE
#define API_UNAVAILABLE(...)
#endif

#endif /* __OS_AVAILABILITY__ */
