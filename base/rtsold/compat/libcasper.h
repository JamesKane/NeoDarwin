// SPDX-License-Identifier: BSD-2-Clause
// NeoDarwin-Language: expressibility: a compat header for FreeBSD's C rtsold, which includes it.
//
// FreeBSD's <libcasper.h>, for rtsold: built without WITH_CASPER (as
// FreeBSD's rescue rtsol is), rtsold's own services (cap_llflags.c,
// cap_script.c, cap_sendmsg.c) call their functions directly and the
// channels are never opened; only the type is needed.
#ifndef ND_RTSOLD_LIBCASPER_H
#define ND_RTSOLD_LIBCASPER_H

typedef struct cap_channel cap_channel_t;	// rtsold.h names the struct
#endif
