// SPDX-License-Identifier: BSD-2-Clause
// NeoDarwin-Language: expressibility: a compat header for FreeBSD's C dhclient, which includes it.
//
// FreeBSD's <sys/endian.h>, for dhclient: the big-endian decoders it uses.
// Darwin has <libkern/OSByteOrder.h> instead.
#ifndef ND_DHCLIENT_SYS_ENDIAN_H
#define ND_DHCLIENT_SYS_ENDIAN_H
#include <stdint.h>

static inline uint16_t
be16dec(const void *pp)
{
	const uint8_t *p = pp;
	return (uint16_t)((p[0] << 8) | p[1]);
}

static inline uint32_t
be32dec(const void *pp)
{
	const uint8_t *p = pp;
	return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | p[3];
}
#endif
