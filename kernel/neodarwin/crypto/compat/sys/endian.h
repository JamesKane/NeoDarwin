// SPDX-License-Identifier: BSD-2-Clause
// NeoDarwin-Language: portability: FreeBSD's <sys/endian.h> interface, which neither macOS nor XNU provides, for FreeBSD crypto compiled unmodified.
#ifndef ND_COMPAT_SYS_ENDIAN_H
#define ND_COMPAT_SYS_ENDIAN_H
#include <sys/types.h>
#include <stdint.h>

static inline uint16_t be16dec(const void *p) { const uint8_t *b = p; return (uint16_t)((b[0] << 8) | b[1]); }
static inline uint32_t be32dec(const void *p) { const uint8_t *b = p; return ((uint32_t)b[0] << 24) | ((uint32_t)b[1] << 16) | ((uint32_t)b[2] << 8) | b[3]; }
static inline uint64_t be64dec(const void *p) { const uint8_t *b = p; return ((uint64_t)be32dec(b) << 32) | be32dec(b + 4); }
static inline uint32_t le32dec(const void *p) { const uint8_t *b = p; return ((uint32_t)b[3] << 24) | ((uint32_t)b[2] << 16) | ((uint32_t)b[1] << 8) | b[0]; }
static inline uint64_t le64dec(const void *p) { const uint8_t *b = p; return ((uint64_t)le32dec(b + 4) << 32) | le32dec(b); }
static inline void be32enc(void *p, uint32_t v) { uint8_t *b = p; b[0] = (uint8_t)(v >> 24); b[1] = (uint8_t)(v >> 16); b[2] = (uint8_t)(v >> 8); b[3] = (uint8_t)v; }
static inline void be64enc(void *p, uint64_t v) { be32enc(p, (uint32_t)(v >> 32)); be32enc((uint8_t *)p + 4, (uint32_t)v); }
static inline void le32enc(void *p, uint32_t v) { uint8_t *b = p; b[0] = (uint8_t)v; b[1] = (uint8_t)(v >> 8); b[2] = (uint8_t)(v >> 16); b[3] = (uint8_t)(v >> 24); }
static inline void le64enc(void *p, uint64_t v) { le32enc(p, (uint32_t)v); le32enc((uint8_t *)p + 4, (uint32_t)(v >> 32)); }
#ifndef bswap32
#define bswap32(x) __builtin_bswap32(x)
#define bswap64(x) __builtin_bswap64(x)
#endif
#endif
