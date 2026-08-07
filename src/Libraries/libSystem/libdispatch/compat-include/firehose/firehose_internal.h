/*
 * OpenOSX: minimal stand-in for firehose/firehose_internal.h, which lives
 * in Apple's separate (not vendored here) firehose project. internal.h
 * includes this unconditionally, but with VOUCHER_USE_MACH_VOUCHER=0
 * (see libdispatch/CMakeLists.txt) voucher.c's real firehose_buffer_*
 * calls are compiled out, routed instead to upstream's own firehose-free
 * fallback voucher implementation. Only two identifiers from this header
 * are still referenced unconditionally (init.c's _firehose_task_buffer
 * global and _firehose_spi_version constant) - an opaque pointer type and
 * a version constant, neither of which anything here actually dereferences
 * or calls into.
 */
#ifndef __PD_FIREHOSE_INTERNAL_H__
#define __PD_FIREHOSE_INTERNAL_H__

#include <stdint.h>

typedef struct firehose_buffer_s *firehose_buffer_t;

#define OS_FIREHOSE_SPI_VERSION 20170411

#endif /* __PD_FIREHOSE_INTERNAL_H__ */
