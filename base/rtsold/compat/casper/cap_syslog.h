// SPDX-License-Identifier: BSD-2-Clause
// NeoDarwin-Language: expressibility: a compat header for FreeBSD's C rtsold, which includes it.
//
// FreeBSD's <casper/cap_syslog.h>, for rtsold: syslog(3) itself. In the
// foreground (-f, as netconfigd runs it) rtsold prints to standard error
// instead and never calls these.
#ifndef ND_RTSOLD_CAP_SYSLOG_H
#define ND_RTSOLD_CAP_SYSLOG_H
#include <syslog.h>
#include <libcasper.h>

#define cap_openlog(chan, ident, opt, fac)	((void)(chan), openlog((ident), (opt), (fac)))
#define cap_setlogmask(chan, mask)		((void)(chan), setlogmask(mask))
#define cap_vsyslog(chan, pri, fmt, ap)		((void)(chan), vsyslog((pri), (fmt), (ap)))
#endif
