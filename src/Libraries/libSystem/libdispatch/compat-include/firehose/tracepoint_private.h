/*
 * OpenOSX: minimal stand-in for firehose/tracepoint_private.h, which
 * lives in Apple's separate (not vendored here) firehose project. Nothing
 * in this build actually implements os_log/firehose activity tracing - this
 * only provides the handful of plain integer typedefs
 * os/voucher_activity_private.h references in function declarations, so
 * libdispatch's voucher/tracing API surface compiles. None of these
 * declarations are called by anything this build actually exercises.
 */
#ifndef __PD_FIREHOSE_TRACEPOINT_PRIVATE_H__
#define __PD_FIREHOSE_TRACEPOINT_PRIVATE_H__

#include <stdint.h>

typedef uint64_t firehose_tracepoint_id_t;
typedef uint64_t firehose_activity_id_t;
typedef uint32_t firehose_activity_flags_t;
typedef uint32_t firehose_stream_t;

#endif /* __PD_FIREHOSE_TRACEPOINT_PRIVATE_H__ */
