// SPDX-License-Identifier: BSD-2-Clause
// NeoDarwin-Language: expressibility: a compat header for FreeBSD's C rtsold, which includes it.
//
// FreeBSD's <capsicum_helpers.h>, for rtsold: without Capsicum
// (compat/sys/capsicum.h) there is nothing to limit or enter, and each
// helper succeeds, as FreeBSD's do on a kernel without Capsicum. rtsold then
// runs with root's privileges, as FreeBSD's did before Capsicum.
#ifndef ND_RTSOLD_CAPSICUM_HELPERS_H
#define ND_RTSOLD_CAPSICUM_HELPERS_H
#include <sys/capsicum.h>

#define caph_rights_limit(fd, rights)	((void)(fd), (void)(rights), 0)
#define caph_cache_catpages()		((void)0)
#define caph_enter_casper()		0
#endif
