// SPDX-License-Identifier: BSD-2-Clause
// NeoDarwin-Language: portability: corecrypto mode descriptors are C structs consumed by XNU.
//
// AES on FreeBSD's rijndael (sys/crypto/rijndael, public domain), and every
// AES mode XNU's table names. The ECB context keeps one key schedule, the
// direction it is used in: libkern stores CBC contexts in 280 bytes
// (AES_CBC_CTX_MAX_SIZE), so FreeBSD's two-schedule rijndael_ctx is too big.

#include "nd_modes.h"
#include <corecrypto/cc_error.h>
#include <sys/types.h>
#include <crypto/rijndael/rijndael.h>

struct aes_ecb_ctx {
	int nr;
	uint32_t rk[4 * (RIJNDAEL_MAXNR + 1)];
};

_Static_assert(ND_CBC_SIZE(sizeof(struct aes_ecb_ctx)) <= 280, "AES-CBC context exceeds libkern's AES_CBC_CTX_MAX_SIZE");
_Static_assert(ND_XTS_SIZE(sizeof(struct aes_ecb_ctx)) <= 1064, "AES-XTS context exceeds libkern's AES_XTS_CTX_MAX_SIZE");

static bool
aes_key_ok(size_t n)
{
	return n == 16 || n == 24 || n == 32;
}

static int
aes_init_enc(const struct ccmode_ecb *ecb, ccecb_ctx *ctx, size_t key_nbytes, const void *key)
{
	(void)ecb;
	if (!aes_key_ok(key_nbytes)) {
		return CCERR_PARAMETER;
	}
	struct aes_ecb_ctx *c = (struct aes_ecb_ctx *)ctx;
	c->nr = rijndaelKeySetupEnc(c->rk, key, (int)key_nbytes * 8);
	return CCERR_OK;
}

static int
aes_init_dec(const struct ccmode_ecb *ecb, ccecb_ctx *ctx, size_t key_nbytes, const void *key)
{
	(void)ecb;
	if (!aes_key_ok(key_nbytes)) {
		return CCERR_PARAMETER;
	}
	struct aes_ecb_ctx *c = (struct aes_ecb_ctx *)ctx;
	c->nr = rijndaelKeySetupDec(c->rk, key, (int)key_nbytes * 8);
	return CCERR_OK;
}

static int
aes_encrypt(const ccecb_ctx *ctx, size_t nblocks, const void *in, void *out)
{
	const struct aes_ecb_ctx *c = (const struct aes_ecb_ctx *)ctx;
	const uint8_t *i = in;
	uint8_t *o = out;
	for (size_t b = 0; b < nblocks; b++, i += 16, o += 16) {
		rijndaelEncrypt(c->rk, c->nr, i, o);
	}
	return CCERR_OK;
}

static int
aes_decrypt(const ccecb_ctx *ctx, size_t nblocks, const void *in, void *out)
{
	const struct aes_ecb_ctx *c = (const struct aes_ecb_ctx *)ctx;
	const uint8_t *i = in;
	uint8_t *o = out;
	for (size_t b = 0; b < nblocks; b++, i += 16, o += 16) {
		rijndaelDecrypt(c->rk, c->nr, i, o);
	}
	return CCERR_OK;
}

#define AES_CTX sizeof(struct aes_ecb_ctx)

const struct ccmode_ecb nd_aes_ecb_encrypt = { .size = AES_CTX, .block_size = 16, .init = aes_init_enc, .ecb = aes_encrypt };
const struct ccmode_ecb nd_aes_ecb_decrypt = { .size = AES_CTX, .block_size = 16, .init = aes_init_dec, .ecb = aes_decrypt };

const struct ccmode_cbc nd_aes_cbc_encrypt = ND_CBC_MODE(nd_aes_ecb_encrypt, AES_CTX, 16, nd_cbc_encrypt);
const struct ccmode_cbc nd_aes_cbc_decrypt = ND_CBC_MODE(nd_aes_ecb_decrypt, AES_CTX, 16, nd_cbc_decrypt);
const struct ccmode_ctr nd_aes_ctr_crypt = ND_CTR_MODE(nd_aes_ecb_encrypt, AES_CTX);
const struct ccmode_xts nd_aes_xts_encrypt = ND_XTS_MODE(nd_aes_ecb_encrypt, nd_aes_ecb_encrypt, AES_CTX);
const struct ccmode_xts nd_aes_xts_decrypt = ND_XTS_MODE(nd_aes_ecb_decrypt, nd_aes_ecb_encrypt, AES_CTX);
const struct ccmode_gcm nd_aes_gcm_encrypt = ND_GCM_MODE(nd_aes_ecb_encrypt, AES_CTX, CCMODE_GCM_ENCRYPTOR);
const struct ccmode_gcm nd_aes_gcm_decrypt = ND_GCM_MODE(nd_aes_ecb_encrypt, AES_CTX, CCMODE_GCM_DECRYPTOR);
