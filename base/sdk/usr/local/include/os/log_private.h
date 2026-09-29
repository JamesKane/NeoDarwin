/*
 * SPDX-License-Identifier: BSD-2-Clause
 * NeoDarwin-Language: expressibility: a C header standing in for one from Apple's internal SDK.
 *
 * Stands in for libsystem_trace's os/log_private.h; libsystem_trace is not
 * published. Libc uses two things from it:
 *  - the log "pack", a captured log call whose format string os/assumes.c
 *    reads (os_log_pack_t, olp_format) before handing it to libsystem_trace's
 *    os_log_pack_send_and_compose(), which it declares itself;
 *  - os_log_send_and_compose(), which the os_crash() macro uses in files that
 *    define OS_CRASH_ENABLE_EXPERIMENTAL_LIBTRACE (arc4random.c, vfprintf.c):
 *    it logs a message and also formats it into the caller's buffer.
 * Both sides of each exchange are built by NeoDarwin (Libc and the
 * libsystem_trace stand-in, docs/base/libsystem.md), so the layout and
 * calling convention below only have to agree between them. As in Apple's
 * header, the macro calls a libsystem_trace function named with an _impl
 * suffix.
 * libsystem_asl uses the syslog shim: syslog(3) and asl_log(3) ask
 * os_log_shim_enabled() whether messages from the caller (addr, its return
 * address) go to os_log, and if so send them with os_log_with_args_4syslog().
 * These are libsystem_trace exports under Apple's names and signatures.
 */
#ifndef __OS_LOG_PRIVATE_H__
#define __OS_LOG_PRIVATE_H__

#include <os/log.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h>
#include <time.h>

typedef struct os_log_pack_s {
	uint64_t olp_continuous_time;
	struct timespec olp_wall_time;
	const void *olp_mh;          /* the calling image's Mach-O header */
	const void *olp_pc;          /* the call site */
	const char *olp_format;
	uint8_t olp_data[];          /* the encoded arguments */
} os_log_pack_s, *os_log_pack_t;

/* os_log_send_and_compose() flags: log the message, return it formatted. */
#define OS_LOG_F_SEND     0x1u
#define OS_LOG_F_COMPOSE  0x2u

__BEGIN_DECLS
/*
 * Formats fmt into buf (bufsz bytes) when OS_LOG_F_COMPOSE is set, logs it
 * when OS_LOG_F_SEND is set, stores fmt in *fmt_out, and returns buf.
 */
extern char *_os_log_send_and_compose_impl(uint32_t flags, const char **fmt_out, char *buf, size_t bufsz,
    os_log_t log, os_log_type_t type, const char *fmt, ...) __attribute__((format(os_log, 7, 8)));

/* The syslog shim: whether the image containing addr logs through os_log. */
extern bool os_log_shim_enabled(void *addr);
/* Logs fmt with args as if called from ret_addr. */
extern void os_log_with_args(os_log_t log, os_log_type_t type, const char *fmt, va_list args, void *ret_addr);
/* os_log_with_args() for messages that came in through syslog(3) and asl(3). */
extern void os_log_with_args_4syslog(os_log_t log, os_log_type_t type, const char *fmt, va_list args,
    void *ret_addr);
__END_DECLS

#define os_log_send_and_compose(flags, fmt_out, buf, bufsz, log, type, fmt, ...) \
	_os_log_send_and_compose_impl((flags), (fmt_out), (buf), (bufsz), (log), (type), (fmt), ##__VA_ARGS__)

#endif /* __OS_LOG_PRIVATE_H__ */
