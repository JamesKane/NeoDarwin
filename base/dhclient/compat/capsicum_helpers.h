// SPDX-License-Identifier: BSD-2-Clause
// NeoDarwin-Language: expressibility: a compat header for FreeBSD's C dhclient, which includes it.
//
// FreeBSD's <capsicum_helpers.h>, for dhclient: without Capsicum
// (compat/sys/capsicum.h) there is nothing to limit or enter, and each
// helper succeeds, as FreeBSD's do on a kernel without Capsicum.
#ifndef ND_DHCLIENT_CAPSICUM_HELPERS_H
#define ND_DHCLIENT_CAPSICUM_HELPERS_H
#include <sys/capsicum.h>

#define caph_rights_limit(fd, rights)	0
#define caph_ioctls_limit(fd, cmds, n)	0
#define caph_fcntls_limit(fd, fcntls)	0
#define caph_enter_casper()		0
#endif
