// SPDX-License-Identifier: BSD-2-Clause
// NeoDarwin-Language: expressibility: a compat header for FreeBSD's C dhclient, which includes it.
//
// FreeBSD's <casper/cap_syslog.h>, for dhclient: syslog(3) itself. When
// dhclient also prints its messages on standard error (log_perror: -d, or
// before it daemonizes), they don't go to syslog too: NeoDarwin's syslog
// writes to standard error until there is a log store (the libsystem_trace
// stand-in), so each line would come out twice. Once dhclient is a daemon,
// they go to syslog alone, as on FreeBSD.
#ifndef ND_DHCLIENT_CAP_SYSLOG_H
#define ND_DHCLIENT_CAP_SYSLOG_H
#include <syslog.h>
#include <libcasper.h>

extern int log_perror;

#define cap_openlog(chan, ident, opt, fac)	openlog((ident), (opt), (fac))
#define cap_setlogmask(chan, mask)		setlogmask(mask)
#define cap_syslog(chan, pri, ...) \
	do { if (!log_perror) syslog((pri), __VA_ARGS__); } while (0)
#endif
