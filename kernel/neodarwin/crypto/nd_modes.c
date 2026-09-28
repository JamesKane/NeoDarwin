// SPDX-License-Identifier: BSD-2-Clause
// NeoDarwin-Language: portability: corecrypto mode descriptors are C structs consumed by XNU.
//
// CBC, CTR, XTS, GCM and CBC-CS3 ciphertext stealing over any corecrypto ECB
// mode. Constructions follow NIST SP 800-38A/-38D, IEEE 1619 and the SP
// 800-38A addendum; FreeBSD's opencrypto xforms were the reference.

#include "nd_modes.h"
#include <corecrypto/cc_error.h>
#include <corecrypto/cc_priv.h>
#include <string.h>

#include "ccmode_gcm_internal.h"  // xnu osfmk/corecrypto: the generic GCM key layout and helpers

_Static_assert(sizeof(struct _ccmode_gcm_key) <= ND_GCM_KEY_HEADER, "GCM key header too small");

// -- CBC ---------------------------------------------------------------------

struct cbc_hdr {
	const struct ccmode_ecb *ecb;
};
#define CBC_ECB(ctx) ((ccecb_ctx *)((uint8_t *)(uintptr_t)(ctx) + ND_CBC_HEADER))

int
nd_cbc_init(const struct ccmode_cbc *cbc, cccbc_ctx *ctx, size_t key_len, const void *key)
{
	const struct ccmode_ecb *ecb = cbc->custom;
	((struct cbc_hdr *)ctx)->ecb = ecb;
	return ecb->init(ecb, CBC_ECB(ctx), key_len, key);
}

int
nd_cbc_encrypt(const cccbc_ctx *ctx, cccbc_iv *iv, size_t nblocks, const void *in, void *out)
{
	const struct ccmode_ecb *ecb = ((const struct cbc_hdr *)ctx)->ecb;
	size_t bs = ecb->block_size;
	uint8_t *v = (uint8_t *)iv;
	const uint8_t *i = in;
	uint8_t *o = out;
	uint8_t x[16];
	for (size_t b = 0; b < nblocks; b++, i += bs, o += bs) {
		for (size_t k = 0; k < bs; k++) {
			x[k] = i[k] ^ v[k];
		}
		ecb->ecb(CBC_ECB(ctx), 1, x, o);
		memcpy(v, o, bs);
	}
	cc_clear(sizeof(x), x);
	return CCERR_OK;
}

int
nd_cbc_decrypt(const cccbc_ctx *ctx, cccbc_iv *iv, size_t nblocks, const void *in, void *out)
{
	const struct ccmode_ecb *ecb = ((const struct cbc_hdr *)ctx)->ecb;
	size_t bs = ecb->block_size;
	uint8_t *v = (uint8_t *)iv;
	const uint8_t *i = in;
	uint8_t *o = out;
	uint8_t x[16], c[16];
	for (size_t b = 0; b < nblocks; b++, i += bs, o += bs) {
		memcpy(c, i, bs);  // in may equal out
		ecb->ecb(CBC_ECB(ctx), 1, c, x);
		for (size_t k = 0; k < bs; k++) {
			o[k] = x[k] ^ v[k];
		}
		memcpy(v, c, bs);
	}
	cc_clear(sizeof(x), x);
	return CCERR_OK;
}

// -- CTR ---------------------------------------------------------------------

struct ctr_hdr {
	const struct ccmode_ecb *ecb;
	size_t used;  // bytes of pad consumed
	uint8_t ctr[16];
	uint8_t pad[16];
};
_Static_assert(sizeof(struct ctr_hdr) <= ND_CTR_HEADER, "CTR header too small");
#define CTR_ECB(ctx) ((ccecb_ctx *)((uint8_t *)(ctx) + ND_CTR_HEADER))

int
nd_ctr_setctr(const struct ccmode_ctr *mode, ccctr_ctx *ctx, const void *ctr)
{
	(void)mode;
	struct ctr_hdr *h = (struct ctr_hdr *)ctx;
	memcpy(h->ctr, ctr, 16);
	h->used = 16;
	return CCERR_OK;
}

int
nd_ctr_init(const struct ccmode_ctr *mode, ccctr_ctx *ctx, size_t key_len, const void *key, const void *iv)
{
	struct ctr_hdr *h = (struct ctr_hdr *)ctx;
	h->ecb = mode->custom;
	int rc = h->ecb->init(h->ecb, CTR_ECB(ctx), key_len, key);
	if (rc == CCERR_OK) {
		rc = nd_ctr_setctr(mode, ctx, iv);
	}
	return rc;
}

int
nd_ctr_crypt(ccctr_ctx *ctx, size_t nbytes, const void *in, void *out)
{
	struct ctr_hdr *h = (struct ctr_hdr *)ctx;
	const uint8_t *i = in;
	uint8_t *o = out;
	for (size_t n = 0; n < nbytes; n++) {
		if (h->used == 16) {
			h->ecb->ecb(CTR_ECB(ctx), 1, h->ctr, h->pad);
			inc_uint(h->ctr, 16);
			h->used = 0;
		}
		o[n] = i[n] ^ h->pad[h->used++];
	}
	return CCERR_OK;
}

// -- XTS ---------------------------------------------------------------------

struct xts_hdr {
	const struct ccmode_ecb *data;
	const struct ccmode_ecb *tweak;
};
static ccecb_ctx *
xts_data(const ccxts_ctx *ctx)
{
	return (ccecb_ctx *)((uint8_t *)(uintptr_t)ctx + ND_XTS_HEADER);
}
static ccecb_ctx *
xts_tweak(const ccxts_ctx *ctx)
{
	const struct xts_hdr *h = (const struct xts_hdr *)ctx;
	return (ccecb_ctx *)((uint8_t *)(uintptr_t)ctx + ND_XTS_HEADER + ND_ALIGN16(h->data->size));
}

int
nd_xts_init(const struct ccmode_xts *xts, ccxts_ctx *ctx, size_t key_nbytes, const void *data_key, const void *tweak_key)
{
	struct xts_hdr *h = (struct xts_hdr *)ctx;
	h->data = xts->custom;
	h->tweak = xts->custom1;
	int rc = h->data->init(h->data, xts_data(ctx), key_nbytes, data_key);
	if (rc == CCERR_OK) {
		rc = h->tweak->init(h->tweak, xts_tweak(ctx), key_nbytes, tweak_key);
	}
	return rc;
}

void
nd_xts_key_sched(const struct ccmode_xts *xts, ccxts_ctx *ctx, size_t key_nbytes, const void *data_key, const void *tweak_key)
{
	(void)nd_xts_init(xts, ctx, key_nbytes, data_key, tweak_key);
}

int
nd_xts_set_tweak(const ccxts_ctx *ctx, ccxts_tweak *tweak, const void *iv)
{
	const struct xts_hdr *h = (const struct xts_hdr *)ctx;
	return h->tweak->ecb(xts_tweak(ctx), 1, iv, tweak);
}

// Multiply the tweak by x in GF(2^128), little-endian byte order (IEEE 1619).
static void
xts_mul_x(uint8_t t[16])
{
	uint8_t carry = 0;
	for (int k = 0; k < 16; k++) {
		uint8_t next = t[k] >> 7;
		t[k] = (uint8_t)((t[k] << 1) | carry);
		carry = next;
	}
	if (carry) {
		t[0] ^= 0x87;
	}
}

void *
nd_xts_crypt(const ccxts_ctx *ctx, ccxts_tweak *tweak, size_t nblocks, const void *in, void *out)
{
	const struct xts_hdr *h = (const struct xts_hdr *)ctx;
	uint8_t *t = (uint8_t *)tweak;
	const uint8_t *i = in;
	uint8_t *o = out;
	uint8_t x[16];
	for (size_t b = 0; b < nblocks; b++, i += 16, o += 16) {
		for (int k = 0; k < 16; k++) {
			x[k] = i[k] ^ t[k];
		}
		h->data->ecb(xts_data(ctx), 1, x, x);
		for (int k = 0; k < 16; k++) {
			o[k] = x[k] ^ t[k];
		}
		xts_mul_x(t);
	}
	cc_clear(sizeof(x), x);
	return tweak;
}

// -- GCM ---------------------------------------------------------------------

#define GCM(ctx) _CCMODE_GCM_KEY(ctx)

int
nd_gcm_reset(ccgcm_ctx *ctx)
{
	cc_clear(16, CCMODE_GCM_KEY_X(ctx));
	cc_clear(16, CCMODE_GCM_KEY_Y(ctx));
	if (!(GCM(ctx)->flags & CCGCM_FLAGS_INIT_WITH_IV)) {
		cc_clear(16, CCMODE_GCM_KEY_Y_0(ctx));  // ccgcm_inc_iv steps the saved IV
	}
	cc_clear(16, CCMODE_GCM_KEY_PAD(ctx));
	GCM(ctx)->buf_nbytes = 0;
	GCM(ctx)->aad_nbytes = 0;
	GCM(ctx)->text_nbytes = 0;
	GCM(ctx)->state = CCMODE_GCM_STATE_IV;
	return CCERR_OK;
}

int
nd_gcm_init(const struct ccmode_gcm *gcm, ccgcm_ctx *ctx, size_t key_nbytes, const void *key)
{
	struct _ccmode_gcm_key *k = GCM(ctx);
	k->ecb = gcm->custom;  // always the encrypting ECB
	k->ecb_key = (uint8_t *)ctx + ND_GCM_KEY_HEADER;
	k->encdec = gcm->encdec;
	k->flags = 0;
	int rc = k->ecb->init(k->ecb, CCMODE_GCM_KEY_ECB_KEY(ctx), key_nbytes, key);
	if (rc != CCERR_OK) {
		return rc;
	}
	cc_clear(16, CCMODE_GCM_KEY_H(ctx));
	k->ecb->ecb(CCMODE_GCM_KEY_ECB_KEY(ctx), 1, CCMODE_GCM_KEY_H(ctx), CCMODE_GCM_KEY_H(ctx));
	return nd_gcm_reset(ctx);
}

// X ^= data, multiplying by H at each 16-byte boundary of the running count.
static void
ghash_bytes(ccgcm_ctx *ctx, uint64_t *count, const uint8_t *data, size_t nbytes)
{
	uint8_t *x = CCMODE_GCM_KEY_X(ctx);
	for (size_t n = 0; n < nbytes; n++) {
		x[*count % 16] ^= data[n];
		(*count)++;
		if (*count % 16 == 0) {
			ccmode_gcm_mult_h(ctx, x);
		}
	}
}

int
nd_gcm_set_iv(ccgcm_ctx *ctx, size_t iv_nbytes, const void *iv)
{
	struct _ccmode_gcm_key *k = GCM(ctx);
	if (k->state != CCMODE_GCM_STATE_IV || iv_nbytes == 0) {
		return CCMODE_INVALID_CALL_SEQUENCE;
	}
	uint8_t *y0 = CCMODE_GCM_KEY_Y_0(ctx);
	if (iv_nbytes == CCGCM_IV_NBYTES) {
		memcpy(y0, iv, 12);
		y0[12] = 0; y0[13] = 0; y0[14] = 0; y0[15] = 1;
	} else {
		// Y0 = GHASH(IV || pad || 0^64 || [len(IV)]64), computed in X.
		uint64_t n = 0;
		ghash_bytes(ctx, &n, iv, iv_nbytes);
		if (n % 16) {
			ccmode_gcm_mult_h(ctx, CCMODE_GCM_KEY_X(ctx));
		}
		uint8_t lens[16] = { 0 };
		cc_store64_be((uint64_t)iv_nbytes * 8, lens + 8);
		n = 0;
		ghash_bytes(ctx, &n, lens, 16);
		memcpy(y0, CCMODE_GCM_KEY_X(ctx), 16);
		cc_clear(16, CCMODE_GCM_KEY_X(ctx));
	}
	memcpy(CCMODE_GCM_KEY_Y(ctx), y0, 16);
	ccmode_gcm_update_pad(ctx);  // Y = inc32(Y0), pad = E(Y)
	k->buf_nbytes = 0;
	k->state = CCMODE_GCM_STATE_AAD;
	return CCERR_OK;
}

int
nd_gcm_aad(ccgcm_ctx *ctx, size_t nbytes, const void *in)
{
	struct _ccmode_gcm_key *k = GCM(ctx);
	if (k->state != CCMODE_GCM_STATE_AAD) {
		return CCMODE_INVALID_CALL_SEQUENCE;
	}
	ghash_bytes(ctx, &k->aad_nbytes, in, nbytes);
	return CCERR_OK;
}

int
nd_gcm_crypt(ccgcm_ctx *ctx, size_t nbytes, const void *in, void *out)
{
	struct _ccmode_gcm_key *k = GCM(ctx);
	if (k->state == CCMODE_GCM_STATE_AAD) {
		ccmode_gcm_aad_finalize(ctx);  // xnu: finish the partial AAD block, state TEXT
	}
	if (k->state != CCMODE_GCM_STATE_TEXT) {
		return CCMODE_INVALID_CALL_SEQUENCE;
	}
	const uint8_t *i = in;
	uint8_t *o = out;
	uint8_t *pad = CCMODE_GCM_KEY_PAD(ctx);
	uint8_t *x = CCMODE_GCM_KEY_X(ctx);
	bool enc = k->encdec == CCMODE_GCM_ENCRYPTOR;
	for (size_t n = 0; n < nbytes; n++) {
		if (k->buf_nbytes == 16) {
			ccmode_gcm_update_pad(ctx);
			k->buf_nbytes = 0;
		}
		uint8_t c = enc ? (uint8_t)(i[n] ^ pad[k->buf_nbytes]) : i[n];
		o[n] = (uint8_t)(i[n] ^ pad[k->buf_nbytes]);
		k->buf_nbytes++;
		x[k->text_nbytes % 16] ^= c;
		k->text_nbytes++;
		if (k->text_nbytes % 16 == 0) {
			ccmode_gcm_mult_h(ctx, x);
		}
	}
	return CCERR_OK;
}

int
nd_gcm_finalize(ccgcm_ctx *ctx, size_t tag_nbytes, void *tag)
{
	struct _ccmode_gcm_key *k = GCM(ctx);
	if (k->state == CCMODE_GCM_STATE_AAD) {
		ccmode_gcm_aad_finalize(ctx);
	}
	if (k->state != CCMODE_GCM_STATE_TEXT || tag_nbytes > 16) {
		return CCMODE_INVALID_CALL_SEQUENCE;
	}
	uint8_t *x = CCMODE_GCM_KEY_X(ctx);
	if (k->text_nbytes % 16) {
		ccmode_gcm_mult_h(ctx, x);
	}
	uint8_t lens[16];
	cc_store64_be(k->aad_nbytes * 8, lens);
	cc_store64_be(k->text_nbytes * 8, lens + 8);
	for (int n = 0; n < 16; n++) {
		x[n] ^= lens[n];
	}
	ccmode_gcm_mult_h(ctx, x);
	uint8_t t[16];
	k->ecb->ecb(CCMODE_GCM_KEY_ECB_KEY(ctx), 1, CCMODE_GCM_KEY_Y_0(ctx), t);
	for (int n = 0; n < 16; n++) {
		t[n] ^= x[n];
	}
	int rc = CCERR_OK;
	if (k->encdec == CCMODE_GCM_DECRYPTOR && cc_cmp_safe(tag_nbytes, tag, t) != 0) {
		rc = CCMODE_INTEGRITY_FAILURE;
	}
	memcpy(tag, t, tag_nbytes);  // corecrypto writes the computed tag either way
	cc_clear(sizeof(t), t);
	k->state = CCMODE_GCM_STATE_FINAL;
	return rc;
}

// -- CBC ciphertext stealing, CS3 (Kerberos) ------------------------------------
//
// nbytes >= one block. Whole blocks go through CBC; if the message is longer
// than one block, the last two blocks are swapped (CS3 always swaps).

size_t
nd_cts3_encrypt(const struct ccmode_cbc *cbc, cccbc_ctx *ctx, cccbc_iv *iv, size_t nbytes, const void *in, void *out)
{
	size_t bs = cbc->block_size;
	if (nbytes < bs) {
		return 0;
	}
	size_t tail = nbytes % bs ? nbytes % bs : bs;
	size_t head = nbytes - tail - bs;  // bytes before the last two (partial) blocks
	const uint8_t *i = in;
	uint8_t *o = out;
	if (head) {
		cbc->cbc(ctx, iv, head / bs, i, o);
	}
	if (nbytes == bs) {
		cbc->cbc(ctx, iv, 1, i + head, o + head);
		return nbytes;
	}
	uint8_t penult[16], last[16] = { 0 };
	cbc->cbc(ctx, iv, 1, i + head, penult);  // C(n-1)
	memcpy(last, i + head + bs, tail);        // P(n), zero padded
	uint8_t cn[16];
	cbc->cbc(ctx, iv, 1, last, cn);            // C(n) chains from C(n-1)
	memcpy(o + head, cn, bs);                  // CS3: C(n) first,
	memcpy(o + head + bs, penult, tail);       // then C(n-1) truncated
	cc_clear(16, penult);
	cc_clear(16, last);
	return nbytes;
}

size_t
nd_cts3_decrypt(const struct ccmode_cbc *cbc, cccbc_ctx *ctx, cccbc_iv *iv, size_t nbytes, const void *in, void *out)
{
	size_t bs = cbc->block_size;
	if (nbytes < bs) {
		return 0;
	}
	size_t tail = nbytes % bs ? nbytes % bs : bs;
	size_t head = nbytes - tail - bs;
	const uint8_t *i = in;
	uint8_t *o = out;
	if (head) {
		cbc->cbc(ctx, iv, head / bs, i, o);
	}
	if (nbytes == bs) {
		cbc->cbc(ctx, iv, 1, i + head, o + head);
		return nbytes;
	}
	// in: C(n) (full), C(n-1) truncated to `tail`. Decrypt C(n) with a zero
	// IV to get D = P(n) ^ C(n-1), recover C(n-1)'s missing bytes from D.
	uint8_t d[16], cn1[16], cn[16], z[16] = { 0 };
	memcpy(cn, i + head, bs);
	cbc->cbc(ctx, (cccbc_iv *)z, 1, cn, d);   // D(C(n)) with IV 0
	memcpy(cn1, i + head + bs, tail);
	memcpy(cn1 + tail, d + tail, bs - tail);
	for (size_t k = 0; k < tail; k++) {
		o[head + bs + k] = d[k] ^ cn1[k];       // P(n)
	}
	cbc->cbc(ctx, iv, 1, cn1, o + head);       // P(n-1) with the running IV
	memcpy(iv, cn, bs);                        // next IV is C(n), as after encryption
	cc_clear(16, d);
	return nbytes;
}
