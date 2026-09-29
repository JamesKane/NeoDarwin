// SPDX-License-Identifier: BSD-2-Clause
// NeoDarwin-Language: portability: FreeBSD's <sys/endian.h> byte-order names, which Darwin spells in <machine/endian.h>.
#ifndef ND_MSUN_SYS_ENDIAN_H
#define ND_MSUN_SYS_ENDIAN_H
#include <machine/endian.h>
#define _LITTLE_ENDIAN __DARWIN_LITTLE_ENDIAN
#define _BIG_ENDIAN __DARWIN_BIG_ENDIAN
#define _PDP_ENDIAN __DARWIN_PDP_ENDIAN
#define _BYTE_ORDER __DARWIN_BYTE_ORDER
#endif
