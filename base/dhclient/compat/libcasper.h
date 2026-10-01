// SPDX-License-Identifier: BSD-2-Clause
// NeoDarwin-Language: expressibility: a compat header for FreeBSD's C dhclient, which includes it.
//
// FreeBSD's <libcasper.h>, for dhclient: Casper's services exist so that a
// process in capability mode can still reach syslogd. Without Capsicum,
// dhclient logs directly (compat/casper/cap_syslog.h); the channel is a
// token that is never dereferenced.
#ifndef ND_DHCLIENT_LIBCASPER_H
#define ND_DHCLIENT_LIBCASPER_H

typedef struct nd_cap_channel cap_channel_t;

#define cap_init()			((cap_channel_t *)1)
#define cap_service_open(chan, name)	((void)(name), (chan))
#define cap_close(chan)			((void)(chan))
#endif
