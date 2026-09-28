// SPDX-License-Identifier: BSD-2-Clause
// NeoDarwin-Language: portability: corecrypto's RSA interface is C, exported by XNU to kexts.
//
// RSA PKCS#1 v1.5 signature verification on BearSSL (contrib/bearssl, MIT),
// the verifier FreeBSD's libsecureboot uses. Public-key operations only:
// the kernel never holds private keys.
//
// ccrsa_make_pub stores the modulus and exponent as big-endian bytes in the
// caller's ccrsa_pub_ctx, after the cczp header (whose unit count the caller
// sets; libkern's rsa_make_pub sizes it for 4096-bit keys).

#include "ndcrypto.h"
#include <corecrypto/cc_error.h>
#include <corecrypto/cc_priv.h>
#include <string.h>

#include "bearssl_rsa.h"

#define ND_RSA_MAX_BYTES 512  // 4096 bits, libkern's RSA_MAX_KEY_BITSIZE
#define ND_RSA_MAGIC 0x6e645253u  // "ndRS"

struct nd_rsa_pub {
	uint32_t magic;
	uint16_t nlen, elen;
	uint8_t n[ND_RSA_MAX_BYTES];
	uint8_t e[ND_RSA_MAX_BYTES];
};

static struct nd_rsa_pub *
pub_of(ccrsa_pub_ctx_t k)
{
	return (struct nd_rsa_pub *)(void *)ccrsa_ctx_m(k);
}

// Bytes the caller's context provides after the cczp header.
static size_t
pub_room(ccrsa_pub_ctx_t k)
{
	return ccrsa_pub_ctx_size(ccn_sizeof_n(ccrsa_ctx_n(k))) - (size_t)((uint8_t *)ccrsa_ctx_m(k) - (uint8_t *)k);
}

static size_t
strip_zeros(const uint8_t **p, size_t n)
{
	while (n > 0 && **p == 0) {
		(*p)++;
		n--;
	}
	return n;
}

int
nd_rsa_make_pub(ccrsa_pub_ctx_t pubk, size_t exp_nbytes, const uint8_t *exp, size_t mod_nbytes, const uint8_t *mod)
{
	exp_nbytes = strip_zeros(&exp, exp_nbytes);
	mod_nbytes = strip_zeros(&mod, mod_nbytes);
	if (mod_nbytes == 0 || exp_nbytes == 0 || mod_nbytes > ND_RSA_MAX_BYTES || exp_nbytes > ND_RSA_MAX_BYTES ||
	    pub_room(pubk) < sizeof(struct nd_rsa_pub)) {
		return CCERR_PARAMETER;
	}
	struct nd_rsa_pub *p = pub_of(pubk);
	p->magic = ND_RSA_MAGIC;
	p->nlen = (uint16_t)mod_nbytes;
	p->elen = (uint16_t)exp_nbytes;
	memcpy(p->n, mod, mod_nbytes);
	memcpy(p->e, exp, exp_nbytes);
	return CCERR_OK;
}

// `oid` is corecrypto's DER form (tag 0x06, length, value) or NULL for a raw
// digest; BearSSL wants the length byte and value.
int
nd_rsa_verify_pkcs1v15(ccrsa_pub_ctx_t key, const uint8_t *oid, size_t digest_len, const uint8_t *digest,
    size_t sig_len, const uint8_t *sig, bool *valid)
{
	*valid = false;
	struct nd_rsa_pub *p = pub_of(key);
	if (p->magic != ND_RSA_MAGIC || digest_len > 64 || sig_len != p->nlen || (oid != NULL && oid[0] != 0x06)) {
		return CCERR_PARAMETER;
	}
	br_rsa_public_key pk = { .n = p->n, .nlen = p->nlen, .e = p->e, .elen = p->elen };
	uint8_t hash[64];
	if (br_rsa_i31_pkcs1_vrfy(sig, sig_len, oid ? oid + 1 : NULL, digest_len, &pk, hash)) {
		*valid = cc_cmp_safe(digest_len, hash, digest) == 0;
	}
	cc_clear(sizeof(hash), hash);
	return CCERR_OK;
}
