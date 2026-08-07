/*
 * OpenOSX: top-level <pthread.h> shim. Our from-scratch pthread only ships
 * the real implementation at <pthread/pthread.h> (pthread/include/pthread/),
 * not the thin top-level wrapper real Darwin's SDK also provides at
 * usr/include/pthread.h - libdispatch's internal.h includes the bare form.
 */
#include <pthread/pthread.h>
