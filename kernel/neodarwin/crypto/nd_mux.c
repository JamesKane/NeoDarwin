// SPDX-License-Identifier: BSD-2-Clause
// NeoDarwin-Language: portability: XNU's crypto_digest and crypto_hmac interfaces are C function tables.
//
// The algorithm-indexed digest and HMAC entry points (crypto_digest_*,
// crypto_hmac_*) over xnu's ccdigest and cchmac, and function forms of the
// ccdigest helpers corecrypto's header declares inline.

#include "ndcrypto.h"
#include <corecrypto/cc_priv.h>
#include <corecrypto/cchmac.h>
#include <corecrypto/ccsha2.h>
#include <libkern/crypto/register_crypto.h>
#include <string.h>

const struct ccdigest_info *
nd_digest_info(unsigned int alg)
{
	switch (alg) {
	case CRYPTO_DIGEST_ALG_MD5: return &nd_md5_di;
	case CRYPTO_DIGEST_ALG_SHA1: return &nd_sha1_di;
	case CRYPTO_DIGEST_ALG_SHA256: return ccsha256_di();
	case CRYPTO_DIGEST_ALG_SHA384: return &nd_sha384_di;
	case CRYPTO_DIGEST_ALG_SHA512: return &nd_sha512_di;
	default: cc_abort("ndcrypto: unknown digest algorithm");
	}
	return NULL;
}

static void
copy_out(void *dst, size_t dst_size, const uint8_t *src, size_t src_size)
{
	memcpy(dst, src, dst_size < src_size ? dst_size : src_size);
}

// -- ccdigest function forms -----------------------------------------------------

void
nd_ccdigest_final(const struct ccdigest_info *di, ccdigest_ctx_t ctx, void *digest)
{
	di->final(di, ctx, digest);
}

void
nd_ccdigest(const struct ccdigest_info *di, unsigned long len, const void *data, void *digest)
{
	ccdigest_di_decl(di, ctx);
	ccdigest_init(di, ctx);
	ccdigest_update(di, ctx, len, data);
	di->final(di, ctx, digest);
	ccdigest_di_clear(di, ctx);
}

// -- crypto_digest_* -------------------------------------------------------------

size_t
nd_digest_ctx_size(crypto_digest_alg_t alg)
{
	return ccdigest_di_size(nd_digest_info(alg));
}

void
nd_digest_init(crypto_digest_alg_t alg, void *ctx, size_t ctx_size)
{
	const struct ccdigest_info *di = nd_digest_info(alg);
	if (ctx_size < ccdigest_di_size(di)) {
		cc_abort("ndcrypto: digest context too small");
	}
	ccdigest_init(di, ctx);
}

void
nd_digest_update(crypto_digest_alg_t alg, void *ctx, size_t ctx_size, const void *data, size_t data_size)
{
	(void)ctx_size;
	ccdigest_update(nd_digest_info(alg), ctx, data_size, data);
}

void
nd_digest_final(crypto_digest_alg_t alg, void *ctx, size_t ctx_size, void *digest, size_t digest_size)
{
	(void)ctx_size;
	const struct ccdigest_info *di = nd_digest_info(alg);
	uint8_t out[64];
	di->final(di, ctx, out);
	copy_out(digest, digest_size, out, di->output_size);
	cc_clear(sizeof(out), out);
}

void
nd_digest(crypto_digest_alg_t alg, const void *data, size_t data_size, void *digest, size_t digest_size)
{
	const struct ccdigest_info *di = nd_digest_info(alg);
	uint8_t out[64];
	nd_ccdigest(di, data_size, data, out);
	copy_out(digest, digest_size, out, di->output_size);
	cc_clear(sizeof(out), out);
}

// -- crypto_hmac_* ---------------------------------------------------------------

size_t
nd_hmac_ctx_size(crypto_digest_alg_t alg)
{
	return cchmac_di_size(nd_digest_info(alg));
}

void
nd_hmac_init(crypto_digest_alg_t alg, void *ctx, size_t ctx_size, const void *key, size_t key_size)
{
	const struct ccdigest_info *di = nd_digest_info(alg);
	if (ctx_size < cchmac_di_size(di)) {
		cc_abort("ndcrypto: HMAC context too small");
	}
	cchmac_init(di, ctx, key_size, key);
}

void
nd_hmac_update(crypto_digest_alg_t alg, void *ctx, size_t ctx_size, const void *data, size_t data_size)
{
	(void)ctx_size;
	cchmac_update(nd_digest_info(alg), ctx, data_size, data);
}

void
nd_hmac_final_generate(crypto_digest_alg_t alg, void *ctx, size_t ctx_size, void *tag, size_t tag_size)
{
	(void)ctx_size;
	const struct ccdigest_info *di = nd_digest_info(alg);
	uint8_t out[64];
	cchmac_final(di, ctx, out);
	copy_out(tag, tag_size, out, di->output_size);
	cc_clear(sizeof(out), out);
}

bool
nd_hmac_final_verify(crypto_digest_alg_t alg, void *ctx, size_t ctx_size, const void *tag, size_t tag_size)
{
	(void)ctx_size;
	const struct ccdigest_info *di = nd_digest_info(alg);
	uint8_t out[64];
	cchmac_final(di, ctx, out);
	bool ok = tag_size > 0 && tag_size <= di->output_size && cc_cmp_safe(tag_size, out, tag) == 0;
	cc_clear(sizeof(out), out);
	return ok;
}

void
nd_hmac_generate(crypto_digest_alg_t alg, const void *key, size_t key_size, const void *data, size_t data_size,
    void *tag, size_t tag_size)
{
	const struct ccdigest_info *di = nd_digest_info(alg);
	uint8_t out[64];
	cchmac(di, key_size, key, data_size, data, out);
	copy_out(tag, tag_size, out, di->output_size);
	cc_clear(sizeof(out), out);
}

bool
nd_hmac_verify(crypto_digest_alg_t alg, const void *key, size_t key_size, const void *data, size_t data_size,
    const void *tag, size_t tag_size)
{
	const struct ccdigest_info *di = nd_digest_info(alg);
	uint8_t out[64];
	cchmac(di, key_size, key, data_size, data, out);
	bool ok = tag_size > 0 && tag_size <= di->output_size && cc_cmp_safe(tag_size, out, tag) == 0;
	cc_clear(sizeof(out), out);
	return ok;
}
