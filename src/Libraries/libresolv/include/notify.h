/*
 * Minimal stand-in for Apple's notifyd client API (usually shipped by
 * Libnotify, backed by the notifyd daemon over Mach IPC). OpenOSX has
 * neither Libnotify nor a notifyd daemon yet - libresolv only uses this API
 * to opportunistically notice "network config changed, re-read
 * resolv.conf" and "please cancel this in-flight query, thread is exiting"
 * events; both are optimizations, not correctness requirements. Every call
 * here fails/no-ops so callers fall back to their non-notified path (they
 * already handle notify_register_*() failing - see res_query.c/res_send.c/
 * dns.c token == -1 checks). Replace when a real notifyd exists.
 */
#ifndef _PD_NOTIFY_H_
#define _PD_NOTIFY_H_

#include <stdint.h>

#define NOTIFY_STATUS_OK 0
#define NOTIFY_STATUS_FAILED 1

#ifdef __cplusplus
extern "C" {
#endif

uint32_t notify_register_plain(const char *name, int *out_token);
uint32_t notify_register_check(const char *name, int *out_token);
uint32_t notify_check(int token, int *check);
uint32_t notify_get_state(int token, int *state);
uint32_t notify_cancel(int token);
uint32_t notify_post(const char *name);
uint32_t notify_monitor_file(int token, const char *name, int flags);

#ifdef __cplusplus
}
#endif

#endif /* _PD_NOTIFY_H_ */
