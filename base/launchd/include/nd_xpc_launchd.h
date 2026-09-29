/*
 * SPDX-License-Identifier: BSD-2-Clause
 * NeoDarwin-Language: expressibility: a C header for launchd-842's C sources, standing in for an unpublished one.
 *
 * Stands in for libxpc's <xpc/launchd.h>, the interface between libxpc and
 * launchd, which Apple doesn't publish. NeoDarwin builds launchd-842 without
 * it (config.h's HAVE_XPC_LAUNCHD is 0, patches/0002): XPC pipes, domains,
 * events and process calls are compiled out, since their only clients
 * (xpcproxy, xpcd, UserEventAgent) are closed. core.c still names a few of
 * the interface's types and calls outside those paths: in the job model's
 * fields, the job defaults cache and LaunchEvents. This header declares
 * them. The values are NeoDarwin's own; nothing outside launchd reads them.
 *
 * Every call fails as it would with no XPC peer, so the paths that reach
 * them do nothing:
 *  - ld2xpc() converts nothing: a LaunchEvents entry is logged as not
 *    imported (there is no event monitor to deliver it), and there is no job
 *    defaults cache (TARGET_OS_EMBEDDED only), so xpc2ld() is never reached;
 *  - xpc_copy_entitlements_for_pid() finds no entitlements, and is only
 *    called for events registered through the XPC event calls;
 *  - xpc_call_wakeup() answers a requestor that only an XPC domain has.
 */
#ifndef ND_XPC_LAUNCHD_H
#define ND_XPC_LAUNCHD_H

#include <launch.h>
#include <mach/mach.h>
#include <stdint.h>
#include <sys/types.h>
#include <xpc/xpc.h>

/* The kind of XPC service a process attaches to (waiting4attach). */
typedef int64_t xpc_service_type_t;

/* Environment variables xpcproxy reads. */
#define XPC_SERVICE_ENV_ATTACHED "XPC_SERVICE_ATTACHED"
#define XPC_SERVICE_RENDEZVOUS_TOKEN "XPC_SERVICE_RENDEZVOUS_TOKEN"

/* The flag for an event that carries its registrant's entitlements. */
#define XPC_EVENT_FLAG_ENTITLEMENTS (1ull << 0)

/* The jetsam bands of the XPC process calls, in the order of core.c's
 * _launchd_priority_map (only embedded launchd applies them).
 */
typedef enum {
	XPC_JETSAM_BAND_SUSPENDED = 1,
	XPC_JETSAM_BAND_BACKGROUND_OPPORTUNISTIC,
	XPC_JETSAM_BAND_BACKGROUND,
	XPC_JETSAM_BAND_MAIL,
	XPC_JETSAM_BAND_PHONE,
	XPC_JETSAM_BAND_UI_SUPPORT,
	XPC_JETSAM_BAND_FOREGROUND_SUPPORT,
	XPC_JETSAM_BAND_FOREGROUND,
	XPC_JETSAM_BAND_AUDIO,
	XPC_JETSAM_BAND_ACCESSORY,
	XPC_JETSAM_BAND_CRITICAL,
	XPC_JETSAM_BAND_TELEPHONY,
	XPC_JETSAM_BAND_LAST,
} xpc_jetsam_band_t;

static inline xpc_object_t
ld2xpc(launch_data_t ld)
{
	(void)ld;
	return NULL;
}

static inline launch_data_t
xpc2ld(xpc_object_t xo)
{
	(void)xo;
	return NULL;
}

static inline xpc_object_t
xpc_copy_entitlements_for_pid(pid_t pid)
{
	(void)pid;
	return NULL;
}

static inline kern_return_t
xpc_call_wakeup(mach_port_t rport, int error)
{
	(void)rport;
	(void)error;
	return MACH_SEND_INVALID_DEST;
}

#endif /* ND_XPC_LAUNCHD_H */
