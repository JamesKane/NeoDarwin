// SPDX-License-Identifier: BSD-2-Clause
// NeoDarwin-Language: portability: FreeBSD's <sys/systm.h>, reduced to what FreeBSD's crypto sources use, so they compile unmodified in XNU and on the host.
#ifndef ND_COMPAT_SYS_SYSTM_H
#define ND_COMPAT_SYS_SYSTM_H
#include <string.h>
#ifndef KASSERT
#define KASSERT(exp, msg) do { } while (0)
#endif
#endif
