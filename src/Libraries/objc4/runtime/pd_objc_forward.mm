/*
 * pd_objc_forward.mm - OpenOSX addition to objc4 (not Apple source).
 *
 * A diagnostic forward handler, for finding out what a real macOS application
 * wants from frameworks we have not finished writing.
 *
 * objc4's default handler calls _objc_fatal on the first unrecognized
 * selector, which is correct behaviour for a shipping system and useless for
 * bring-up: each run tells you exactly one missing method, so discovering N of
 * them costs N build-and-boot cycles. Under this handler the send returns nil
 * instead, the process keeps going, and one run enumerates the whole set.
 *
 * That is deliberately unsound - a method that was supposed to return a double
 * or have a side effect now does neither - so it is opt-in and off by default:
 *
 *     OPENOSX_OBJC_FORWARD=log      log each missing selector once, return nil
 *     OPENOSX_OBJC_FORWARD=fatal    Apple's behaviour (the default)
 *
 * Lines are tagged so they are greppable out of a serial log:
 *
 *     openosx-objc: unimplemented -[NSWindow setTitle:]
 *
 * Each (class, selector) pair is reported once. Real Cocoa applications send
 * missing selectors from inside draw and event loops, so without that the
 * console fills with the same line thousands of times and the interesting ones
 * scroll away.
 */
#include <objc/runtime.h>
#include <objc/message.h>

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// Open-addressed set of seen (class, selector) pairs. Fixed size and lock-free
// so the handler adds no allocation and no lock to a path that is already
// failing; a lost race only costs a duplicate log line.
//
// Uses clang's __atomic builtins rather than <stdatomic.h>: this file is
// compiled as Objective-C++, where stdatomic.h and <atomic> disagree about
// what _Atomic and atomic_compare_exchange_* mean. The builtins are spelled
// the same in both languages.
#define PD_FWD_SLOTS 2048
static uint64_t pd_fwd_seen[PD_FWD_SLOTS];

static uint64_t pd_fwd_hash(const char *cls, const char *sel)
{
    uint64_t h = 1469598103934665603ULL;            // FNV-1a
    for (const char *p = cls; p && *p; p++) {
        h ^= (unsigned char)*p;
        h *= 1099511628211ULL;
    }
    h ^= '|';
    h *= 1099511628211ULL;
    for (const char *p = sel; p && *p; p++) {
        h ^= (unsigned char)*p;
        h *= 1099511628211ULL;
    }
    return h ? h : 1;                               // 0 marks an empty slot
}

static bool pd_fwd_first_time(const char *cls, const char *sel)
{
    uint64_t key = pd_fwd_hash(cls, sel);
    size_t i = (size_t)(key % PD_FWD_SLOTS);
    for (size_t probe = 0; probe < 64; probe++) {
        size_t slot = (i + probe) % PD_FWD_SLOTS;
        uint64_t cur = __atomic_load_n(&pd_fwd_seen[slot], __ATOMIC_RELAXED);
        if (cur == key)
            return false;                           // already reported
        if (cur == 0) {
            uint64_t expected = 0;
            if (__atomic_compare_exchange_n(&pd_fwd_seen[slot], &expected, key,
                                            false, __ATOMIC_RELAXED,
                                            __ATOMIC_RELAXED))
                return true;
            if (expected == key)
                return false;                       // lost the race to an equal key
        }
    }
    return true;                                    // table full: keep reporting
}

static void pd_fwd_report(id self, SEL sel)
{
    Class cls = object_getClass(self);
    const char *name = object_getClassName(self);
    const char *selname = sel ? sel_getName(sel) : "<null>";
    if (!pd_fwd_first_time(name ? name : "<nil>", selname))
        return;
    fprintf(stderr, "openosx-objc: unimplemented %c[%s %s]\n",
            class_isMetaClass(cls) ? '+' : '-', name ? name : "<nil>", selname);
    fflush(stderr);
}

static id pd_forward_logging(id self, SEL sel)
{
    pd_fwd_report(self, sel);
    return nil;
}

// Matches objc4's own oversized stret placeholder: the caller supplies the
// return buffer, and returning a zeroed struct is the least-surprising answer.
struct pd_fwd_stret { int i[100]; };

static struct pd_fwd_stret pd_forward_logging_stret(id self, SEL sel)
{
    pd_fwd_report(self, sel);
    struct pd_fwd_stret zero;
    memset(&zero, 0, sizeof(zero));
    return zero;
}

__attribute__((constructor))
static void pd_objc_forward_init(void)
{
    const char *mode = getenv("OPENOSX_OBJC_FORWARD");
    if (!mode || strcmp(mode, "log") != 0)
        return;                                     // default: Apple's fatal handler
    objc_setForwardHandler((void *)pd_forward_logging,
                           (void *)pd_forward_logging_stret);
    fprintf(stderr, "openosx-objc: forwarding unimplemented selectors "
                    "(OPENOSX_OBJC_FORWARD=log); sends will return nil\n");
    fflush(stderr);
}
