// SPDX-License-Identifier: BSD-2-Clause
// NeoDarwin-Language: portability: corecrypto's ChaCha20-Poly1305 interface is a C function table consumed by XNU.
//
// ChaCha20-Poly1305 (RFC 8439) behind corecrypto's streaming interface.
// ChaCha20 is FreeBSD's (sys/crypto/chacha20, D. J. Bernstein, public
// domain); Poly1305 is libsodium's poly1305-donna (sys/contrib/libsodium,
// ISC), compiled into this file unmodified, as FreeBSD's kernel builds it.
// The 64-bit donna state fits corecrypto's ccpoly1305_ctx; the 32-bit one,
// with unsigned long limbs, does not on LP64.

#include "ndcrypto.h"
#include <corecrypto/cc_error.h>
#include <corecrypto/cc_priv.h>
#include <string.h>

#include <sys/types.h>
#include <crypto/chacha20/chacha.h>

#define HAVE_TI_MODE 1
#include "crypto_onetimeauth/poly1305/donna/poly1305_donna.c"

// libsodium support routines poly1305-donna refers to, mapped onto
// corecrypto's constant-time equivalents.
void
sodium_memzero(void *const pnt, const size_t len)
{
	cc_clear(len, pnt);
}
int
crypto_verify_16(const unsigned char *x, const unsigned char *y)
{
	return cc_cmp_safe(16, x, y) == 0 ? 0 : -1;
}

_Static_assert(sizeof(poly1305_state_internal_t) <= sizeof(ccpoly1305_ctx), "poly1305-donna64 state exceeds ccpoly1305_ctx");
_Static_assert(sizeof(struct chacha_ctx) <= sizeof(((ccchacha20_ctx *)0)->state), "chacha_ctx exceeds ccchacha20_ctx.state");

#define CHACHA(ctx) ((struct chacha_ctx *)(ctx)->chacha20_ctx.state)
#define POLY(ctx) ((poly1305_state_internal_t *)(void *)&(ctx)->poly1305_ctx)
#define STATE_NEED_NONCE 0

static const struct ccchacha20poly1305_info nd_info;

static const struct ccchacha20poly1305_info *
cp_info(void)
{
	return &nd_info;
}

static int
cp_reset(const struct ccchacha20poly1305_info *info, ccchacha20poly1305_ctx *ctx)
{
	(void)info;
	cc_clear(sizeof(ctx->poly1305_ctx), &ctx->poly1305_ctx);
	cc_clear(sizeof(ctx->chacha20_ctx.buffer), ctx->chacha20_ctx.buffer);
	ctx->chacha20_ctx.leftover = 0;
	ctx->aad_nbytes = 0;
	ctx->text_nbytes = 0;
	ctx->state = STATE_NEED_NONCE;
	return CCERR_OK;
}

static int
cp_init(const struct ccchacha20poly1305_info *info, ccchacha20poly1305_ctx *ctx, const uint8_t *key)
{
	chacha_keysetup(CHACHA(ctx), key, 256);
	return cp_reset(info, ctx);
}

// Load counter and the nonce words 13..15 as RFC 8439 lays them out.
static void
set_counter_nonce(ccchacha20poly1305_ctx *ctx, uint32_t counter, const uint8_t nonce[12])
{
	uint8_t ctr[8];
	cc_store32_le(counter, ctr);
	memcpy(ctr + 4, nonce, 4);
	chacha_ivsetup(CHACHA(ctx), nonce + 4, ctr);
}

static int
cp_setnonce(const struct ccchacha20poly1305_info *info, ccchacha20poly1305_ctx *ctx, const uint8_t *nonce)
{
	(void)info;
	if (ctx->state != STATE_NEED_NONCE) {
		return CCMODE_INVALID_CALL_SEQUENCE;
	}
	// Block 0 keys Poly1305; the message starts at block 1.
	uint8_t block[64] = { 0 };
	set_counter_nonce(ctx, 0, nonce);
	chacha_encrypt_bytes(CHACHA(ctx), block, block, 64);
	poly1305_init(POLY(ctx), block);
	cc_clear(sizeof(block), block);
	set_counter_nonce(ctx, 1, nonce);
	ctx->chacha20_ctx.leftover = 0;
	ctx->state = CCCHACHA20POLY1305_STATE_AAD;
	return CCERR_OK;
}

// ndcrypto's definition (corecrypto documents none): the nonce is a 96-bit
// little-endian integer in state words 13..15, incremented, re-keyed, and
// written to `nonce`. Valid after reset.
static int
cp_incnonce(const struct ccchacha20poly1305_info *info, ccchacha20poly1305_ctx *ctx, uint8_t *nonce)
{
	uint32_t *w = CHACHA(ctx)->input;
	for (int i = 13; i <= 15; i++) {
		if (++w[i] != 0) {
			break;
		}
	}
	for (int i = 0; i < 3; i++) {
		cc_store32_le(w[13 + i], nonce + 4 * i);
	}
	return cp_setnonce(info, ctx, nonce);
}

static void
pad16(ccchacha20poly1305_ctx *ctx, uint64_t n)
{
	static const uint8_t zeros[16];
	if (n % 16) {
		poly1305_update(POLY(ctx), zeros, 16 - n % 16);
	}
}

static int
cp_aad(const struct ccchacha20poly1305_info *info, ccchacha20poly1305_ctx *ctx, size_t nbytes, const void *aad)
{
	(void)info;
	if (ctx->state != CCCHACHA20POLY1305_STATE_AAD) {
		return CCMODE_INVALID_CALL_SEQUENCE;
	}
	poly1305_update(POLY(ctx), aad, nbytes);
	ctx->aad_nbytes += nbytes;
	return CCERR_OK;
}

// XOR with the keystream, keeping the unused part of the last block.
static void
keystream(ccchacha20poly1305_ctx *ctx, size_t nbytes, const uint8_t *in, uint8_t *out)
{
	ccchacha20_ctx *c = &ctx->chacha20_ctx;
	for (size_t n = 0; n < nbytes; n++) {
		if (c->leftover == 0) {
			memset(c->buffer, 0, sizeof(c->buffer));
			chacha_encrypt_bytes(CHACHA(ctx), c->buffer, c->buffer, 64);
			c->leftover = 64;
		}
		out[n] = in[n] ^ c->buffer[64 - c->leftover--];
	}
}

static int
begin_text(ccchacha20poly1305_ctx *ctx, uint8_t state)
{
	if (ctx->state == CCCHACHA20POLY1305_STATE_AAD) {
		pad16(ctx, ctx->aad_nbytes);
		ctx->state = state;
	}
	return ctx->state == state ? CCERR_OK : CCMODE_INVALID_CALL_SEQUENCE;
}

static int
cp_encrypt(const struct ccchacha20poly1305_info *info, ccchacha20poly1305_ctx *ctx, size_t nbytes, const void *ptext, void *ctext)
{
	(void)info;
	int rc = begin_text(ctx, CCCHACHA20POLY1305_STATE_ENCRYPT);
	if (rc != CCERR_OK) {
		return rc;
	}
	keystream(ctx, nbytes, ptext, ctext);
	poly1305_update(POLY(ctx), ctext, nbytes);
	ctx->text_nbytes += nbytes;
	return CCERR_OK;
}

static int
cp_decrypt(const struct ccchacha20poly1305_info *info, ccchacha20poly1305_ctx *ctx, size_t nbytes, const void *ctext, void *ptext)
{
	(void)info;
	int rc = begin_text(ctx, CCCHACHA20POLY1305_STATE_DECRYPT);
	if (rc != CCERR_OK) {
		return rc;
	}
	poly1305_update(POLY(ctx), ctext, nbytes);  // before XOR: ctext may equal ptext
	keystream(ctx, nbytes, ctext, ptext);
	ctx->text_nbytes += nbytes;
	return CCERR_OK;
}

static int
compute_tag(ccchacha20poly1305_ctx *ctx, uint8_t tag[16])
{
	if (ctx->state == CCCHACHA20POLY1305_STATE_AAD) {
		pad16(ctx, ctx->aad_nbytes);
	} else if (ctx->state != CCCHACHA20POLY1305_STATE_ENCRYPT && ctx->state != CCCHACHA20POLY1305_STATE_DECRYPT) {
		return CCMODE_INVALID_CALL_SEQUENCE;
	}
	pad16(ctx, ctx->text_nbytes);
	uint8_t lens[16];
	cc_store64_le(ctx->aad_nbytes, lens);
	cc_store64_le(ctx->text_nbytes, lens + 8);
	poly1305_update(POLY(ctx), lens, 16);
	poly1305_finish(POLY(ctx), tag);
	ctx->state = CCCHACHA20POLY1305_STATE_FINAL;
	return CCERR_OK;
}

static int
cp_finalize(const struct ccchacha20poly1305_info *info, ccchacha20poly1305_ctx *ctx, uint8_t *tag)
{
	(void)info;
	return compute_tag(ctx, tag);
}

static int
cp_verify(const struct ccchacha20poly1305_info *info, ccchacha20poly1305_ctx *ctx, const uint8_t *tag)
{
	(void)info;
	uint8_t t[16];
	int rc = compute_tag(ctx, t);
	if (rc == CCERR_OK && cc_cmp_safe(16, t, tag) != 0) {
		rc = CCMODE_INTEGRITY_FAILURE;
	}
	cc_clear(sizeof(t), t);
	return rc;
}

const struct ccchacha20poly1305_fns nd_chacha20poly1305_fns = {
	.info = cp_info, .init = cp_init, .reset = cp_reset, .setnonce = cp_setnonce, .incnonce = cp_incnonce,
	.aad = cp_aad, .encrypt = cp_encrypt, .finalize = cp_finalize, .decrypt = cp_decrypt, .verify = cp_verify,
};
