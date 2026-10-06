/*
 * SPDX-License-Identifier: BSD-2-Clause
 * NeoDarwin-Language: portability: a C header standing in for one from Apple's internal SDK, included by ld64's C and C++ sources.
 *
 * corecrypto's digest interface, as ld64 uses it (libcodedirectory.c and
 * OutputFile.cpp: ccsha1_di/ccsha256_di, ccdigest_di_decl, init, update,
 * final), over CommonCrypto's public CC_SHA1 and CC_SHA256. corecrypto's
 * headers ship only in Apple's internal SDK (toolchains/ld64/README.md).
 */
#ifndef ND_LD64_CCDIGEST_H
#define ND_LD64_CCDIGEST_H

#include <stddef.h>
#include <string.h>
#include <CommonCrypto/CommonDigest.h>

#define CCSHA1_OUTPUT_SIZE 20
#define CCSHA256_OUTPUT_SIZE 32

struct ccdigest_info {
	size_t output_size;
	int nd_kind; /* 1 = SHA-1, 256 = SHA-256 */
};

union nd_ccdigest_ctx {
	CC_SHA1_CTX sha1;
	CC_SHA256_CTX sha256;
};

static inline const struct ccdigest_info *ccsha1_di(void)
{
	static const struct ccdigest_info di = { CC_SHA1_DIGEST_LENGTH, 1 };
	return &di;
}

static inline const struct ccdigest_info *ccsha256_di(void)
{
	static const struct ccdigest_info di = { CC_SHA256_DIGEST_LENGTH, 256 };
	return &di;
}

#define ccdigest_di_decl(_di_, _name_) union nd_ccdigest_ctx _name_[1]

static inline void ccdigest_init(const struct ccdigest_info *di, union nd_ccdigest_ctx *ctx)
{
	if (di->nd_kind == 1)
		CC_SHA1_Init(&ctx->sha1);
	else
		CC_SHA256_Init(&ctx->sha256);
}

static inline void ccdigest_update(const struct ccdigest_info *di, union nd_ccdigest_ctx *ctx, size_t len, const void *data)
{
	const unsigned char *p = (const unsigned char *)data;
	while (len > 0) {
		CC_LONG n = len > 0x40000000u ? 0x40000000u : (CC_LONG)len;
		if (di->nd_kind == 1)
			CC_SHA1_Update(&ctx->sha1, p, n);
		else
			CC_SHA256_Update(&ctx->sha256, p, n);
		p += n;
		len -= n;
	}
}

static inline void ccdigest_final(const struct ccdigest_info *di, union nd_ccdigest_ctx *ctx, unsigned char *digest)
{
	if (di->nd_kind == 1)
		CC_SHA1_Final(digest, &ctx->sha1);
	else
		CC_SHA256_Final(digest, &ctx->sha256);
}

#endif
