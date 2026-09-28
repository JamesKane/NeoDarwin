// SPDX-License-Identifier: BSD-2-Clause
// NeoDarwin-Language: portability: XNU's crypto_functions table and Apple's corecrypto interface are C, and this provider registers before any other kernel runtime exists.
//
// ndcrypto: NeoDarwin's kernel crypto provider (docs/kernel/crypto-provider.md).
// It implements the corecrypto interface Apple publishes as APSL
// (EXTERNAL_HEADERS/corecrypto) from xnu's own corecrypto subset and FreeBSD's
// kernel crypto, and fills XNU's crypto_functions table.

#ifndef NDCRYPTO_H
#define NDCRYPTO_H

#include <corecrypto/ccdigest.h>
#include <corecrypto/ccmode.h>
#include <corecrypto/ccmode_impl.h>
#include <corecrypto/ccchacha20poly1305.h>
#include <corecrypto/ccrsa.h>
#include <corecrypto/ccrng.h>
#include <libkern/crypto/register_crypto.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

// Digests (nd_digest.c).
extern const struct ccdigest_info nd_md5_di;
extern const struct ccdigest_info nd_sha1_di;
extern const struct ccdigest_info nd_sha384_di;
extern const struct ccdigest_info nd_sha512_di;

// Block ciphers as corecrypto ECB modes (nd_aes.c, nd_des.c).
extern const struct ccmode_ecb nd_aes_ecb_encrypt, nd_aes_ecb_decrypt;
extern const struct ccmode_ecb nd_des_ecb_encrypt, nd_des_ecb_decrypt;
extern const struct ccmode_ecb nd_tdes_ecb_encrypt, nd_tdes_ecb_decrypt;
int nd_des_key_is_weak(void *key, unsigned long length);
void nd_des_key_set_odd_parity(void *key, unsigned long length);

// Modes over any ECB (nd_modes.c).
extern const struct ccmode_cbc nd_aes_cbc_encrypt, nd_aes_cbc_decrypt;
extern const struct ccmode_cbc nd_des_cbc_encrypt, nd_des_cbc_decrypt;
extern const struct ccmode_cbc nd_tdes_cbc_encrypt, nd_tdes_cbc_decrypt;
extern const struct ccmode_ctr nd_aes_ctr_crypt;
extern const struct ccmode_xts nd_aes_xts_encrypt, nd_aes_xts_decrypt;
extern const struct ccmode_gcm nd_aes_gcm_encrypt, nd_aes_gcm_decrypt;
size_t nd_cts3_encrypt(const struct ccmode_cbc *cbc, cccbc_ctx *ctx, cccbc_iv *iv, size_t nbytes, const void *in, void *out);
size_t nd_cts3_decrypt(const struct ccmode_cbc *cbc, cccbc_ctx *ctx, cccbc_iv *iv, size_t nbytes, const void *in, void *out);

// ChaCha20-Poly1305 (nd_chachapoly.c).
extern const struct ccchacha20poly1305_fns nd_chacha20poly1305_fns;

// RSA public-key verification (nd_rsa.c).
int nd_rsa_make_pub(ccrsa_pub_ctx_t pubk, size_t exp_nbytes, const uint8_t *exp, size_t mod_nbytes, const uint8_t *mod);
int nd_rsa_verify_pkcs1v15(ccrsa_pub_ctx_t key, const uint8_t *oid, size_t digest_len, const uint8_t *digest,
    size_t sig_len, const uint8_t *sig, bool *valid);

// Randomness (nd_random.c): ChaCha20 generators (FreeBSD) with fast key
// erasure for the kmem contexts and the per-CPU kernel PRNG, fed by xnu's
// HMAC-DRBG as the central pool.
size_t nd_random_kmem_ctx_size(void);
void nd_random_kmem_init(void *ctx);
void nd_random_generate(void *ctx, void *out, size_t nbytes);
void nd_random_uniform(void *ctx, uint64_t bound, uint64_t *out);
struct ccrng_state *nd_rng(int *error);
struct cckprng_funcs;
extern const struct cckprng_funcs nd_kprng_funcs;
struct cckprng_ctx *nd_kprng_ctx(void);

// Supplied by the environment (nd_kernel.c in XNU, the test harness on the
// host): fresh seed material before the kernel PRNG exists, and output of
// the registered kernel PRNG afterwards.
void nd_platform_seed(void *out, size_t nbytes);
void nd_platform_random(void *out, size_t nbytes);

// crypto_digest_alg_t to descriptor, shared by the digest and HMAC multiplexers (nd_mux.c).
const struct ccdigest_info *nd_digest_info(unsigned int alg);

// The filled table (nd_register.c), for the kernel and for tests.
struct crypto_functions;
const struct crypto_functions *nd_crypto_functions(void);

#endif
