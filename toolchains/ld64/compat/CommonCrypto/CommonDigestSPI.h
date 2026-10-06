/*
 * SPDX-License-Identifier: BSD-2-Clause
 * NeoDarwin-Language: portability: a C header standing in for one from Apple's internal SDK, included by ld64's LinkEdit.hpp.
 * The one-shot CCDigest ld64 calls (SHA-256 only, for the output UUID),
 * over CommonCrypto's public CC_SHA256.
 */
#ifndef ND_LD64_COMMONDIGESTSPI_H
#define ND_LD64_COMMONDIGESTSPI_H
#include <stdint.h>
#include <stdlib.h>
#include <CommonCrypto/CommonDigest.h>

enum { kCCDigestSHA256 = 10 };

static inline int CCDigest(int alg, const void *data, size_t len, uint8_t *out)
{
	CC_SHA256_CTX ctx;
	const unsigned char *p = (const unsigned char *)data;
	if (alg != kCCDigestSHA256)
		abort();
	CC_SHA256_Init(&ctx);
	while (len > 0) {
		CC_LONG n = len > 0x40000000u ? 0x40000000u : (CC_LONG)len;
		CC_SHA256_Update(&ctx, p, n);
		p += n;
		len -= n;
	}
	CC_SHA256_Final(out, &ctx);
	return 0;
}
#endif
