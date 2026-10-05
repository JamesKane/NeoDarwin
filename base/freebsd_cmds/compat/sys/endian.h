// SPDX-License-Identifier: BSD-2-Clause
// NeoDarwin-Language: portability: FreeBSD's <sys/endian.h> byte-order helpers, which Darwin lacks.
//
// The encode and decode helpers of FreeBSD's sys/sys/endian.h (unaligned
// big- and little-endian loads and stores) and its htobe/htole names, over
// clang's byte-swap builtins. arm64 Darwin is little-endian.
#ifndef ND_SYS_ENDIAN_H
#define ND_SYS_ENDIAN_H
#include <sys/types.h>
#include <stdint.h>
#include <machine/endian.h>

#ifndef bswap16
#define bswap16(x) __builtin_bswap16(x)
#define bswap32(x) __builtin_bswap32(x)
#define bswap64(x) __builtin_bswap64(x)
#endif
#ifndef htobe16
#define htobe16(x) bswap16((x))
#define htobe32(x) bswap32((x))
#define htobe64(x) bswap64((x))
#define htole16(x) ((uint16_t)(x))
#define htole32(x) ((uint32_t)(x))
#define htole64(x) ((uint64_t)(x))
#define be16toh(x) bswap16((x))
#define be32toh(x) bswap32((x))
#define be64toh(x) bswap64((x))
#define le16toh(x) ((uint16_t)(x))
#define le32toh(x) ((uint32_t)(x))
#define le64toh(x) ((uint64_t)(x))
#endif

static __inline uint16_t
be16dec(const void *pp)
{
	const uint8_t *p = (const uint8_t *)pp;
	return ((uint16_t)((p[0] << 8) | p[1]));
}
static __inline uint32_t
be32dec(const void *pp)
{
	const uint8_t *p = (const uint8_t *)pp;
	return (((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | p[3]);
}
static __inline uint64_t
be64dec(const void *pp)
{
	const uint8_t *p = (const uint8_t *)pp;
	return (((uint64_t)be32dec(p) << 32) | be32dec(p + 4));
}
static __inline uint16_t
le16dec(const void *pp)
{
	const uint8_t *p = (const uint8_t *)pp;
	return ((uint16_t)((p[1] << 8) | p[0]));
}
static __inline uint32_t
le32dec(const void *pp)
{
	const uint8_t *p = (const uint8_t *)pp;
	return (((uint32_t)p[3] << 24) | ((uint32_t)p[2] << 16) | ((uint32_t)p[1] << 8) | p[0]);
}
static __inline uint64_t
le64dec(const void *pp)
{
	const uint8_t *p = (const uint8_t *)pp;
	return (((uint64_t)le32dec(p + 4) << 32) | le32dec(p));
}
static __inline void
be16enc(void *pp, uint16_t u)
{
	uint8_t *p = (uint8_t *)pp;
	p[0] = (u >> 8) & 0xff; p[1] = u & 0xff;
}
static __inline void
be32enc(void *pp, uint32_t u)
{
	uint8_t *p = (uint8_t *)pp;
	p[0] = (u >> 24) & 0xff; p[1] = (u >> 16) & 0xff; p[2] = (u >> 8) & 0xff; p[3] = u & 0xff;
}
static __inline void
be64enc(void *pp, uint64_t u)
{
	uint8_t *p = (uint8_t *)pp;
	be32enc(p, (uint32_t)(u >> 32)); be32enc(p + 4, (uint32_t)(u & 0xffffffffU));
}
static __inline void
le16enc(void *pp, uint16_t u)
{
	uint8_t *p = (uint8_t *)pp;
	p[0] = u & 0xff; p[1] = (u >> 8) & 0xff;
}
static __inline void
le32enc(void *pp, uint32_t u)
{
	uint8_t *p = (uint8_t *)pp;
	p[0] = u & 0xff; p[1] = (u >> 8) & 0xff; p[2] = (u >> 16) & 0xff; p[3] = (u >> 24) & 0xff;
}
static __inline void
le64enc(void *pp, uint64_t u)
{
	uint8_t *p = (uint8_t *)pp;
	le32enc(p, (uint32_t)(u & 0xffffffffU)); le32enc(p + 4, (uint32_t)(u >> 32));
}
#endif
