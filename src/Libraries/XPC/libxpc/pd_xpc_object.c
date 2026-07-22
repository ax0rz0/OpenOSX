#include <sys/types.h>
#include <mach/mach.h>
#include <xpc/launchd.h>
#include <stdatomic.h>
#include <stdlib.h>
#include <string.h>
#include "xpc_internal.h"

/*
 * Real storage for the OS_xpc_object_class/OS_xpc_connection_class opaque
 * isa tags (see the OS_OBJECT_OBJC_CLASS_DECL redefinition in
 * xpc_internal.h) - plain data objects, not ObjC class metadata, since
 * nothing in this tree dispatches on isa method tables.
 */
void *OS_xpc_object_class;
void *OS_xpc_connection_class;

/*
 * Real _os_object_alloc(): callers pass the class tag and the payload size
 * beyond struct xpc_object_header (see xpc_connection.c/xpc_type.c), so
 * this allocates header+payload, zeroes it, and sets up the same
 * ref_cnt/xref_cnt fields os_retain()/os_release() below operate on -
 * mirrors real libdispatch object.c's _os_object_alloc_realized() minus
 * the ObjC isa-swizzling this tree doesn't have.
 */
_os_object_t
_os_object_alloc(const void *cls, size_t size)
{
    struct xpc_object_header *hdr = calloc(1, sizeof(struct xpc_object_header) + size);

    if (hdr == NULL) {
        return NULL;
    }
    hdr->isa = cls;
    hdr->ref_cnt = 1;
    hdr->xref_cnt = 1;
    return (_os_object_t)hdr;
}

void *
os_retain(void *obj)
{
    struct xpc_object_header *hdr = obj;

    if (hdr != NULL) {
        atomic_fetch_add_explicit((_Atomic int *)&hdr->ref_cnt, 1, memory_order_relaxed);
    }
    return obj;
}

void
os_release(void *obj)
{
    struct xpc_object_header *hdr = obj;

    if (hdr == NULL) {
        return;
    }
    if (atomic_fetch_sub_explicit((_Atomic int *)&hdr->ref_cnt, 1, memory_order_release) == 1) {
        atomic_thread_fence(memory_order_acquire);
        xpc_object_destroy((struct xpc_object *)obj);
    }
}
