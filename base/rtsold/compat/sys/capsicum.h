// SPDX-License-Identifier: BSD-2-Clause
// NeoDarwin-Language: expressibility: a compat header for FreeBSD's C rtsold, which includes it.
//
// FreeBSD's <sys/capsicum.h>, for rtsold (base/rtsold/build.sh). xnu has no
// Capsicum: a descriptor's rights can't be limited, so the rights calls do
// nothing and cap_rights_init() yields its argument, which rtsold passes
// straight on to caph_rights_limit() (compat/capsicum_helpers.h).
#ifndef ND_RTSOLD_SYS_CAPSICUM_H
#define ND_RTSOLD_SYS_CAPSICUM_H
#include <sys/types.h>

typedef struct { int nd_unused; } cap_rights_t;

#define CAP_CONNECT	0
#define CAP_EVENT	0
#define CAP_IOCTL	0
#define CAP_READ	0
#define CAP_SEND	0
#define CAP_WRITE	0

#define cap_rights_init(rights, ...)	(rights)
#endif
