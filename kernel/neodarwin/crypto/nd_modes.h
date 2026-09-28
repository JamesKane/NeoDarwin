// SPDX-License-Identifier: BSD-2-Clause
// NeoDarwin-Language: portability: corecrypto mode descriptors are C structs consumed by XNU.
//
// Generic corecrypto modes over any struct ccmode_ecb. corecrypto's own
// factories (ccmode_factory.h) are closed; these follow the same contract.
// Contexts are {header, ECB context(s)}; the header records the ECB mode, so
// one set of functions serves AES, DES and triple DES.

#ifndef ND_MODES_H
#define ND_MODES_H

#include "ndcrypto.h"

#define ND_ALIGN16(n) (((n) + 15) & ~(size_t)15)

// CBC: the ECB mode pointer, then its context.
#define ND_CBC_HEADER 16
#define ND_CBC_SIZE(ecb_ctx_size) (ND_CBC_HEADER + ND_ALIGN16(ecb_ctx_size))
int nd_cbc_init(const struct ccmode_cbc *cbc, cccbc_ctx *ctx, size_t key_len, const void *key);
int nd_cbc_encrypt(const cccbc_ctx *ctx, cccbc_iv *iv, size_t nblocks, const void *in, void *out);
int nd_cbc_decrypt(const cccbc_ctx *ctx, cccbc_iv *iv, size_t nblocks, const void *in, void *out);
#define ND_CBC_MODE(ecb_mode, ecb_ctx_size, block, fn) { \
	.size = ND_CBC_SIZE(ecb_ctx_size), .block_size = (block), \
	.init = nd_cbc_init, .cbc = (fn), .custom = &(ecb_mode) }

// CTR: 128-bit big-endian counter, the current keystream block and how much
// of it is used, then the ECB context.
#define ND_CTR_HEADER 64
#define ND_CTR_SIZE(ecb_ctx_size) (ND_CTR_HEADER + ND_ALIGN16(ecb_ctx_size))
int nd_ctr_init(const struct ccmode_ctr *mode, ccctr_ctx *ctx, size_t key_len, const void *key, const void *iv);
int nd_ctr_setctr(const struct ccmode_ctr *mode, ccctr_ctx *ctx, const void *ctr);
int nd_ctr_crypt(ccctr_ctx *ctx, size_t nbytes, const void *in, void *out);
#define ND_CTR_MODE(ecb_mode, ecb_ctx_size) { \
	.size = ND_CTR_SIZE(ecb_ctx_size), .block_size = 1, .ecb_block_size = 16, \
	.init = nd_ctr_init, .setctr = nd_ctr_setctr, .ctr = nd_ctr_crypt, .custom = &(ecb_mode) }

// XTS (IEEE 1619): data ECB (encrypt or decrypt) and tweak ECB (always
// encrypt), then both contexts.
#define ND_XTS_HEADER 16
#define ND_XTS_SIZE(ecb_ctx_size) (ND_XTS_HEADER + 2 * ND_ALIGN16(ecb_ctx_size))
#define ND_XTS_TWEAK_SIZE 16
int nd_xts_init(const struct ccmode_xts *xts, ccxts_ctx *ctx, size_t key_nbytes, const void *data_key, const void *tweak_key);
void nd_xts_key_sched(const struct ccmode_xts *xts, ccxts_ctx *ctx, size_t key_nbytes, const void *data_key, const void *tweak_key);
int nd_xts_set_tweak(const ccxts_ctx *ctx, ccxts_tweak *tweak, const void *iv);
void *nd_xts_crypt(const ccxts_ctx *ctx, ccxts_tweak *tweak, size_t nblocks, const void *in, void *out);
#define ND_XTS_MODE(data_ecb, tweak_ecb, ecb_ctx_size) { \
	.size = ND_XTS_SIZE(ecb_ctx_size), .tweak_size = ND_XTS_TWEAK_SIZE, .block_size = 16, \
	.init = nd_xts_init, .key_sched = nd_xts_key_sched, .set_tweak = nd_xts_set_tweak, \
	.xts = nd_xts_crypt, .custom = &(data_ecb), .custom1 = &(tweak_ecb) }

// GCM: corecrypto's generic key layout (struct _ccmode_gcm_key, published
// in xnu's osfmk/corecrypto/ccmode_gcm_internal.h), so xnu's own
// ccgcm_inc_iv, update_pad, aad_finalize and mult_h work on it. The ECB
// context lives in its trailing u[] array.
size_t nd_gcm_size(size_t ecb_ctx_size);
int nd_gcm_init(const struct ccmode_gcm *gcm, ccgcm_ctx *ctx, size_t key_nbytes, const void *key);
int nd_gcm_set_iv(ccgcm_ctx *ctx, size_t iv_nbytes, const void *iv);
int nd_gcm_aad(ccgcm_ctx *ctx, size_t nbytes, const void *in);
int nd_gcm_crypt(ccgcm_ctx *ctx, size_t nbytes, const void *in, void *out);
int nd_gcm_finalize(ccgcm_ctx *ctx, size_t tag_nbytes, void *tag);
int nd_gcm_reset(ccgcm_ctx *ctx);
#define ND_GCM_KEY_HEADER 128  // >= sizeof(struct _ccmode_gcm_key); checked in nd_modes.c
#define ND_GCM_MODE(ecb_mode, ecb_ctx_size, direction) { \
	.size = ND_GCM_KEY_HEADER + ND_ALIGN16(ecb_ctx_size), .encdec = (direction), .block_size = 1, \
	.init = nd_gcm_init, .set_iv = nd_gcm_set_iv, .gmac = nd_gcm_aad, .gcm = nd_gcm_crypt, \
	.finalize = nd_gcm_finalize, .reset = nd_gcm_reset, .custom = &(ecb_mode) }

#endif
