/*
 * dispatch/dispatch.h  (OpenOSX minimal stub)
 *
 * dyld pulls the full <dispatch/dispatch.h> transitively only through
 * <mach-o/dyld_process_info.h>, and only for the single dispatch_queue_t
 * parameter of _dyld_process_info_notify() -- a host-side API the runtime
 * linker does not implement. The real SDK dispatch.h drags in os/workgroup.h,
 * which does not compile under this toolchain. Declare just the opaque type.
 */
#ifndef OPENOSX_DYLD_DISPATCH_STUB_H
#define OPENOSX_DYLD_DISPATCH_STUB_H

typedef struct dispatch_queue_s *dispatch_queue_t;

#endif /* OPENOSX_DYLD_DISPATCH_STUB_H */
