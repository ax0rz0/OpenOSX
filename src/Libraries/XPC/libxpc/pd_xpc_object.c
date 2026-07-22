/*
 * PureDarwin: real classes.m (from github.com/PureDarwin/XPC) defines the two
 * XPC object classes' -dealloc via real Objective-C (@interface/@implementation),
 * needing a real ObjC runtime this tree doesn't have. PureDarwin's own
 * libdispatch already hit the same problem for its dispatch objects and routes
 * around it via USE_OBJC=0 (see libdispatch/CMakeLists.txt) - but that flag
 * guards the *entire* libdispatch/src/object.m file (`#if USE_OBJC ... #endif`,
 * spanning the whole file), including os_retain()/os_release() themselves, so
 * with it off nothing in this tree provides those two generic entry points at
 * all. xpc_retain()/xpc_release() (xpc_misc.c) call exactly those.
 *
 * struct xpc_object_header already embeds real atomic refcount fields via the
 * same _OS_OBJECT_HEADER(isa, ref_cnt, xref_cnt) macro real dispatch objects
 * use (see os/object_private.h) - independent of classes.m or any ObjC
 * machinery. This provides just enough of os_retain/os_release, scoped to
 * XPC's own object header layout, to do real atomic refcounting and call the
 * real xpc_object_destroy() at zero - not a general os_object reimplementation,
 * just what XPC itself needs, without classes.m/real ObjC.
 */
#include <sys/types.h>
#include <mach/mach.h>
#include <xpc/launchd.h>
#include <stdatomic.h>
#include "xpc_internal.h"

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
