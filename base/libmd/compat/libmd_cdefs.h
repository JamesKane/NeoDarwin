// SPDX-License-Identifier: BSD-2-Clause
// NeoDarwin-Language: portability: FreeBSD <sys/cdefs.h> names that libmd's headers use and Darwin's lack.
//
// libmd's headers are FreeBSD's (base/libmd), and their prototypes use
// FreeBSD's __min_size and __nonstring. Apple's own libmd headers, which
// aren't published, don't. A program that includes them (text_cmds' md5)
// force-includes this first.
#ifndef ND_LIBMD_CDEFS_H
#define ND_LIBMD_CDEFS_H
#include <sys/cdefs.h>
#ifndef __nonstring
#define __nonstring __attribute__((__nonstring__))
#endif
#ifndef __min_size
#define __min_size(x) static (x)
#endif
#endif
