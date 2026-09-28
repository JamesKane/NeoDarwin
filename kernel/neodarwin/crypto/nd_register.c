// SPDX-License-Identifier: BSD-2-Clause
// NeoDarwin-Language: portability: XNU's crypto_functions table is a C struct of function pointers.
//
// The crypto_functions table ndcrypto fills (libkern/crypto/register_crypto.h).
// Every entry is set; docs/kernel/crypto-provider.md §2 maps each to its source.

#include "ndcrypto.h"
#include <corecrypto/cchmac.h>
#include <libkern/crypto/register_crypto.h>

// Function forms and multiplexers from nd_mux.c.
void nd_ccdigest_final(const struct ccdigest_info *, ccdigest_ctx_t, void *);
void nd_ccdigest(const struct ccdigest_info *, unsigned long, const void *, void *);
size_t nd_digest_ctx_size(crypto_digest_alg_t);
void nd_digest_init(crypto_digest_alg_t, void *, size_t);
void nd_digest_update(crypto_digest_alg_t, void *, size_t, const void *, size_t);
void nd_digest_final(crypto_digest_alg_t, void *, size_t, void *, size_t);
void nd_digest(crypto_digest_alg_t, const void *, size_t, void *, size_t);
size_t nd_hmac_ctx_size(crypto_digest_alg_t);
void nd_hmac_init(crypto_digest_alg_t, void *, size_t, const void *, size_t);
void nd_hmac_update(crypto_digest_alg_t, void *, size_t, const void *, size_t);
void nd_hmac_final_generate(crypto_digest_alg_t, void *, size_t, void *, size_t);
bool nd_hmac_final_verify(crypto_digest_alg_t, void *, size_t, const void *, size_t);
void nd_hmac_generate(crypto_digest_alg_t, const void *, size_t, const void *, size_t, void *, size_t);
bool nd_hmac_verify(crypto_digest_alg_t, const void *, size_t, const void *, size_t, const void *, size_t);

// xnu's ccgcm front ends (osfmk/corecrypto/ccgcm.c), which work on the
// generic GCM key layout nd_modes.c uses.
int ccgcm_init_with_iv(const struct ccmode_gcm *mode, ccgcm_ctx *ctx, size_t key_nbytes, const void *key, const void *iv);
int ccgcm_inc_iv(const struct ccmode_gcm *mode, ccgcm_ctx *ctx, void *iv);

static void
nd_digest_update_ul(const struct ccdigest_info *di, ccdigest_ctx_t ctx, unsigned long len, const void *data)
{
	ccdigest_update(di, ctx, len, data);
}

static void
nd_digest_final_uc(const struct ccdigest_info *di, ccdigest_ctx_t ctx, void *digest)
{
	nd_ccdigest_final(di, ctx, digest);
}

static void
nd_hmac_init_ul(const struct ccdigest_info *di, cchmac_ctx_t ctx, unsigned long key_len, const void *key)
{
	cchmac_init(di, ctx, key_len, key);
}

static void
nd_hmac_update_ul(const struct ccdigest_info *di, cchmac_ctx_t ctx, unsigned long len, const void *data)
{
	cchmac_update(di, ctx, len, data);
}

static void
nd_hmac_final_uc(const struct ccdigest_info *di, cchmac_ctx_t ctx, unsigned char *mac)
{
	cchmac_final(di, ctx, mac);
}

static void
nd_hmac_ul(const struct ccdigest_info *di, unsigned long key_len, const void *key, unsigned long data_len,
    const void *data, unsigned char *mac)
{
	cchmac(di, key_len, key, data_len, data, mac);
}


static const struct crypto_functions nd_table = {
	.ccdigest_init_fn = ccdigest_init,
	.ccdigest_update_fn = nd_digest_update_ul,
	.ccdigest_final_fn = nd_digest_final_uc,
	.ccdigest_fn = nd_ccdigest,
	.ccmd5_di = &nd_md5_di,
	.ccsha1_di = &nd_sha1_di,
	.ccsha256_di = NULL,  // set at registration: ccsha256_di() is a function
	.ccsha384_di = &nd_sha384_di,
	.ccsha512_di = &nd_sha512_di,
	.cchmac_init_fn = nd_hmac_init_ul,
	.cchmac_update_fn = nd_hmac_update_ul,
	.cchmac_final_fn = nd_hmac_final_uc,
	.cchmac_fn = nd_hmac_ul,
	.ccaes_ecb_encrypt = &nd_aes_ecb_encrypt,
	.ccaes_ecb_decrypt = &nd_aes_ecb_decrypt,
	.ccaes_cbc_encrypt = &nd_aes_cbc_encrypt,
	.ccaes_cbc_decrypt = &nd_aes_cbc_decrypt,
	.ccaes_ctr_crypt = &nd_aes_ctr_crypt,
	.ccaes_xts_encrypt = &nd_aes_xts_encrypt,
	.ccaes_xts_decrypt = &nd_aes_xts_decrypt,
	.ccaes_gcm_encrypt = &nd_aes_gcm_encrypt,
	.ccaes_gcm_decrypt = &nd_aes_gcm_decrypt,
	.ccgcm_init_with_iv_fn = ccgcm_init_with_iv,
	.ccgcm_inc_iv_fn = ccgcm_inc_iv,
	.ccchacha20poly1305_fns = &nd_chacha20poly1305_fns,
	.ccdes_ecb_encrypt = &nd_des_ecb_encrypt,
	.ccdes_ecb_decrypt = &nd_des_ecb_decrypt,
	.ccdes_cbc_encrypt = &nd_des_cbc_encrypt,
	.ccdes_cbc_decrypt = &nd_des_cbc_decrypt,
	.cctdes_ecb_encrypt = &nd_tdes_ecb_encrypt,
	.cctdes_ecb_decrypt = &nd_tdes_ecb_decrypt,
	.cctdes_cbc_encrypt = &nd_tdes_cbc_encrypt,
	.cctdes_cbc_decrypt = &nd_tdes_cbc_decrypt,
	.ccdes_key_is_weak_fn = nd_des_key_is_weak,
	.ccdes_key_set_odd_parity_fn = nd_des_key_set_odd_parity,
	.ccpad_cts3_encrypt_fn = nd_cts3_encrypt,
	.ccpad_cts3_decrypt_fn = nd_cts3_decrypt,
	.ccrng_fn = nd_rng,
	.ccrsa_make_pub_fn = nd_rsa_make_pub,
	.ccrsa_verify_pkcs1v15_fn = nd_rsa_verify_pkcs1v15,
	.random_generate_fn = nd_random_generate,
	.random_uniform_fn = nd_random_uniform,
	.random_kmem_ctx_size_fn = nd_random_kmem_ctx_size,
	.random_kmem_init_fn = nd_random_kmem_init,
	.digest_ctx_size_fn = nd_digest_ctx_size,
	.digest_init_fn = nd_digest_init,
	.digest_update_fn = nd_digest_update,
	.digest_final_fn = nd_digest_final,
	.digest_fn = nd_digest,
	.hmac_ctx_size_fn = nd_hmac_ctx_size,
	.hmac_init_fn = nd_hmac_init,
	.hmac_update_fn = nd_hmac_update,
	.hmac_final_generate_fn = nd_hmac_final_generate,
	.hmac_final_verify_fn = nd_hmac_final_verify,
	.hmac_generate_fn = nd_hmac_generate,
	.hmac_verify_fn = nd_hmac_verify,
};

static struct crypto_functions nd_registered;

const struct crypto_functions *
nd_crypto_functions(void)
{
	nd_registered = nd_table;
	nd_registered.ccsha256_di = ccsha256_di();
	return &nd_registered;
}
