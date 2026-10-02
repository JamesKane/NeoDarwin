// SPDX-License-Identifier: BSD-2-Clause
// NeoDarwin-Language: portability: exercises ndcrypto through XNU's C crypto_functions table, exactly as libkern calls it.
//
// Known-answer tests for every algorithm in ndcrypto's crypto_functions
// table, reached only through the table. Vectors: RFC 1321 (MD5), FIPS 180
// examples (SHA-1/2), RFC 2202/4231 (HMAC), FIPS 197 and SP 800-38A (AES,
// CBC, CTR), IEEE 1619 (XTS), the GCM specification's test cases (McGrew &
// Viega), SP 800-67 (triple DES), RFC 3962 (CBC-CS3), RFC 8439
// (ChaCha20-Poly1305), and an OpenSSL-generated RSA PKCS#1 v1.5 signature.

#include "../ndcrypto.h"
#include <corecrypto/cchmac.h>
#include <corecrypto/cckprng.h>
#include <libkern/crypto/register_crypto.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int failures, passes;
static const struct crypto_functions *T;

// Deterministic "entropy" so runs are reproducible.
static uint64_t xs = 0x9e3779b97f4a7c15ULL;
static void
fill(void *out, size_t n)
{
	uint8_t *o = out;
	while (n--) {
		xs ^= xs << 13; xs ^= xs >> 7; xs ^= xs << 17;
		*o++ = (uint8_t)xs;
	}
}
void nd_platform_seed(void *out, size_t n) { fill(out, n); }
void nd_platform_random(void *out, size_t n) { fill(out, n); }
uintptr_t nd_platform_cpu_enter(unsigned *cpu) { *cpu = 0; return 0; }
void nd_platform_cpu_exit(uintptr_t state) { (void)state; }

static size_t
unhex(const char *h, uint8_t *out)
{
	size_t n = 0;
	for (; h[0] && h[1]; h += 2) {
		unsigned v;
		sscanf(h, "%2x", &v);
		out[n++] = (uint8_t)v;
	}
	return n;
}

static void
check(const char *what, const void *got, const char *want_hex)
{
	uint8_t want[512];
	size_t n = unhex(want_hex, want);
	if (memcmp(got, want, n) == 0) {
		passes++;
		return;
	}
	failures++;
	printf("FAIL %s\n  got  ", what);
	for (size_t i = 0; i < n; i++) printf("%02x", ((const uint8_t *)got)[i]);
	printf("\n  want %s\n", want_hex);
}

static void
expect(const char *what, int ok)
{
	if (ok) { passes++; } else { failures++; printf("FAIL %s\n", what); }
}

// -- digests ---------------------------------------------------------------------

static void
digest(const char *name, crypto_digest_alg_t alg, const void *msg, size_t len, const char *want)
{
	uint8_t out[64];
	T->digest_fn(alg, msg, len, out, sizeof(out));
	check(name, out, want);
	// Streaming, in odd-sized pieces, through the same table.
	uint8_t ctx[512];
	T->digest_init_fn(alg, ctx, T->digest_ctx_size_fn(alg));
	const uint8_t *p = msg;
	for (size_t off = 0; off < len; off += 7) {
		T->digest_update_fn(alg, ctx, sizeof(ctx), p + off, len - off < 7 ? len - off : 7);
	}
	T->digest_final_fn(alg, ctx, sizeof(ctx), out, sizeof(out));
	check(name, out, want);
}

static void
test_digests(void)
{
	static char million[1000000];
	memset(million, 'a', sizeof(million));
	const char *abc = "abc", *two = "abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq";
	const char *four = "abcdefghbcdefghicdefghijdefghijkefghijklfghijklmghijklmnhijklmnoijklmnopjklmnopqklmnopqrlmnopqrsmnopqrstnopqrstu";
	digest("MD5 empty", CRYPTO_DIGEST_ALG_MD5, "", 0, "d41d8cd98f00b204e9800998ecf8427e");
	digest("MD5 abc", CRYPTO_DIGEST_ALG_MD5, abc, 3, "900150983cd24fb0d6963f7d28e17f72");
	digest("MD5 80 digits", CRYPTO_DIGEST_ALG_MD5, "12345678901234567890123456789012345678901234567890123456789012345678901234567890", 80, "57edf4a22be3c955ac49da2e2107b67a");
	digest("SHA-1 empty", CRYPTO_DIGEST_ALG_SHA1, "", 0, "da39a3ee5e6b4b0d3255bfef95601890afd80709");
	digest("SHA-1 abc", CRYPTO_DIGEST_ALG_SHA1, abc, 3, "a9993e364706816aba3e25717850c26c9cd0d89d");
	digest("SHA-1 448-bit", CRYPTO_DIGEST_ALG_SHA1, two, strlen(two), "84983e441c3bd26ebaae4aa1f95129e5e54670f1");
	digest("SHA-1 million a", CRYPTO_DIGEST_ALG_SHA1, million, sizeof(million), "34aa973cd4c4daa4f61eeb2bdbad27316534016f");
	digest("SHA-256 abc", CRYPTO_DIGEST_ALG_SHA256, abc, 3, "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
	digest("SHA-384 empty", CRYPTO_DIGEST_ALG_SHA384, "", 0, "38b060a751ac96384cd9327eb1b1e36a21fdb71114be07434c0cc7bf63f6e1da274edebfe76f65fbd51ad2f14898b95b");
	digest("SHA-384 abc", CRYPTO_DIGEST_ALG_SHA384, abc, 3, "cb00753f45a35e8bb5a03d699ac65007272c32ab0eded1631a8b605a43ff5bed8086072ba1e7cc2358baeca134c825a7");
	digest("SHA-512 abc", CRYPTO_DIGEST_ALG_SHA512, abc, 3, "ddaf35a193617abacc417349ae20413112e6fa4e89a97ea20a9eeee64b55d39a2192992a274fc1a836ba3c23a3feebbd454d4423643ce80e2a9ac94fa54ca49f");
	digest("SHA-512 896-bit", CRYPTO_DIGEST_ALG_SHA512, four, strlen(four), "8e959b75dae313da8cf4f72814fc143f8f7779c6eb9f7fa17299aeadb6889018501d289e4900f7e4331b99dec4b5433ac7d329eeb6dd26545e96e55b874be909");
	digest("SHA-512 million a", CRYPTO_DIGEST_ALG_SHA512, million, sizeof(million), "e718483d0ce769644e2e42c7bc15b4638e1f98b13b2044285632a803afa973ebde0ff244877ea60a4cb0432ce577c31beb009c5c2c49aa2e4eadb217ad8cc09b");

	// The raw corecrypto descriptors, as the ccdigest_* entries expose them.
	uint8_t out[64];
	T->ccdigest_fn(T->ccsha1_di, 3, abc, out);
	check("ccdigest SHA-1 abc", out, "a9993e364706816aba3e25717850c26c9cd0d89d");
}

static void
test_hmac(void)
{
	const char *key = "Jefe", *msg = "what do ya want for nothing?";
	uint8_t tag[64];
	struct { crypto_digest_alg_t alg; const char *name, *want; } v[] = {
		{ CRYPTO_DIGEST_ALG_MD5, "HMAC-MD5 RFC 2202 #2", "750c783e6ab0b503eaa86e310a5db738" },
		{ CRYPTO_DIGEST_ALG_SHA1, "HMAC-SHA1 RFC 2202 #2", "effcdf6ae5eb2fa2d27416d5f184df9c259a7c79" },
		{ CRYPTO_DIGEST_ALG_SHA256, "HMAC-SHA256 RFC 4231 #2", "5bdcc146bf60754e6a042426089575c75a003f089d2739839dec58b964ec3843" },
		{ CRYPTO_DIGEST_ALG_SHA384, "HMAC-SHA384 RFC 4231 #2", "af45d2e376484031617f78d2b58a6b1b9c7ef464f5a01b47e42ec3736322445e8e2240ca5e69e2c78b3239ecfab21649" },
		{ CRYPTO_DIGEST_ALG_SHA512, "HMAC-SHA512 RFC 4231 #2", "164b7a7bfcf819e2e395fbe73b56e0a387bd64222e831fd610270cd7ea2505549758bf75c05a994a6d034f65f8f0e6fdcaeab1a34d4a6b4b636e070a38bce737" },
	};
	for (size_t i = 0; i < sizeof(v) / sizeof(v[0]); i++) {
		T->hmac_generate_fn(v[i].alg, key, 4, msg, strlen(msg), tag, sizeof(tag));
		check(v[i].name, tag, v[i].want);
		uint8_t ctx[1024];
		T->hmac_init_fn(v[i].alg, ctx, T->hmac_ctx_size_fn(v[i].alg), key, 4);
		T->hmac_update_fn(v[i].alg, ctx, sizeof(ctx), msg, 10);
		T->hmac_update_fn(v[i].alg, ctx, sizeof(ctx), msg + 10, strlen(msg) - 10);
		expect(v[i].name, T->hmac_final_verify_fn(v[i].alg, ctx, sizeof(ctx), tag, 16));
		tag[0] ^= 1;
		expect("HMAC verify rejects a changed tag", !T->hmac_verify_fn(v[i].alg, key, 4, msg, strlen(msg), tag, 16));
	}
}

// -- block ciphers and modes ---------------------------------------------------------

static void
ecb(const char *name, const struct ccmode_ecb *enc, const struct ccmode_ecb *dec, const char *key, const char *pt, const char *ct)
{
	uint8_t k[32], p[16], c[16], ctx[1024];
	size_t kn = unhex(key, k), pn = unhex(pt, p);
	expect(name, enc->size <= sizeof(ctx) && enc->init(enc, (ccecb_ctx *)ctx, kn, k) == 0);
	enc->ecb((ccecb_ctx *)ctx, 1, p, c);
	check(name, c, ct);
	dec->init(dec, (ccecb_ctx *)ctx, kn, k);
	dec->ecb((ccecb_ctx *)ctx, 1, c, c);
	expect(name, memcmp(c, p, pn) == 0);
}

static void
test_block_ciphers(void)
{
	ecb("AES-128 FIPS 197 C.1", T->ccaes_ecb_encrypt, T->ccaes_ecb_decrypt, "000102030405060708090a0b0c0d0e0f", "00112233445566778899aabbccddeeff", "69c4e0d86a7b0430d8cdb78070b4c55a");
	ecb("AES-192 FIPS 197 C.2", T->ccaes_ecb_encrypt, T->ccaes_ecb_decrypt, "000102030405060708090a0b0c0d0e0f1011121314151617", "00112233445566778899aabbccddeeff", "dda97ca4864cdfe06eaf70a0ec0d7191");
	ecb("AES-256 FIPS 197 C.3", T->ccaes_ecb_encrypt, T->ccaes_ecb_decrypt, "000102030405060708090a0b0c0d0e0f101112131415161718191a1b1c1d1e1f", "00112233445566778899aabbccddeeff", "8ea2b7ca516745bfeafc49904b496089");
	ecb("DES", T->ccdes_ecb_encrypt, T->ccdes_ecb_decrypt, "133457799bbcdff1", "0123456789abcdef", "85e813540f0ab405");
	ecb("3DES SP 800-67", T->cctdes_ecb_encrypt, T->cctdes_ecb_decrypt, "0123456789abcdef23456789abcdef01456789abcdef0123", "5468652071756663", "a826fd8ce53b855f");

	uint8_t k[8];
	unhex("0101010101010101", k);
	expect("DES weak key detected", T->ccdes_key_is_weak_fn(k, 8));
	unhex("0000000000000000", k);
	T->ccdes_key_set_odd_parity_fn(k, 8);
	check("DES odd parity", k, "0101010101010101");
	expect("libkern DES context sizes", T->ccdes_ecb_encrypt->size <= 256 && T->cctdes_ecb_encrypt->size <= 768);
}

static void
test_cbc_ctr(void)
{
	uint8_t key[16], iv[16], pt[64], out[64], ctx[1024];
	unhex("2b7e151628aed2a6abf7158809cf4f3c", key);
	unhex("6bc1bee22e409f96e93d7e117393172aae2d8a571e03ac9c9eb76fac45af8e51", pt);

	const struct ccmode_cbc *cbc = T->ccaes_cbc_encrypt;
	expect("libkern AES-CBC context size", cbc->size <= 280 && T->ccaes_cbc_decrypt->size <= 280);
	unhex("000102030405060708090a0b0c0d0e0f", iv);
	cbc->init(cbc, (cccbc_ctx *)ctx, 16, key);
	cbc->cbc((cccbc_ctx *)ctx, (cccbc_iv *)iv, 2, pt, out);
	check("AES-CBC SP 800-38A F.2.1", out, "7649abac8119b246cee98e9b12e9197d5086cb9b507219ee95db113a917678b2");
	const struct ccmode_cbc *dcbc = T->ccaes_cbc_decrypt;
	unhex("000102030405060708090a0b0c0d0e0f", iv);
	dcbc->init(dcbc, (cccbc_ctx *)ctx, 16, key);
	dcbc->cbc((cccbc_ctx *)ctx, (cccbc_iv *)iv, 2, out, out);
	expect("AES-CBC decrypt in place", memcmp(out, pt, 32) == 0);

	const struct ccmode_ctr *ctr = T->ccaes_ctr_crypt;
	unhex("f0f1f2f3f4f5f6f7f8f9fafbfcfdfeff", iv);
	ctr->init(ctr, (ccctr_ctx *)ctx, 16, key, iv);
	ctr->ctr((ccctr_ctx *)ctx, 5, pt, out);  // split mid-block
	ctr->ctr((ccctr_ctx *)ctx, 27, pt + 5, out + 5);
	check("AES-CTR SP 800-38A F.5.1", out, "874d6191b620e3261bef6864990db6ce9806f66b7970fdff8617187bb9fffdff");
}

static void
test_xts(void)
{
	struct { const char *k1, *k2, *tweak, *pt, *ct; } v[] = {
		{ "00000000000000000000000000000000", "00000000000000000000000000000000", "00000000000000000000000000000000",
		  "0000000000000000000000000000000000000000000000000000000000000000",
		  "917cf69ebd68b2ec9b9fe9a3eadda692cd43d2f59598ed858c02c2652fbf922e" },
		{ "11111111111111111111111111111111", "22222222222222222222222222222222", "33333333330000000000000000000000",
		  "4444444444444444444444444444444444444444444444444444444444444444",
		  "c454185e6a16936e39334038acef838bfb186fff7480adc4289382ecd6d394f0" },
	};
	const struct ccmode_xts *enc = T->ccaes_xts_encrypt, *dec = T->ccaes_xts_decrypt;
	expect("libkern AES-XTS context size", enc->size <= 1064 && dec->size <= 1064);
	for (int i = 0; i < 2; i++) {
		uint8_t k1[16], k2[16], tw[16], t[32], pt[32], out[32], ctx[1100];
		unhex(v[i].k1, k1); unhex(v[i].k2, k2); unhex(v[i].tweak, tw); unhex(v[i].pt, pt);
		enc->init(enc, (ccxts_ctx *)ctx, 16, k1, k2);
		enc->set_tweak((ccxts_ctx *)ctx, (ccxts_tweak *)t, tw);
		enc->xts((ccxts_ctx *)ctx, (ccxts_tweak *)t, 2, pt, out);
		check(i ? "AES-XTS IEEE 1619 #2" : "AES-XTS IEEE 1619 #1", out, v[i].ct);
		dec->init(dec, (ccxts_ctx *)ctx, 16, k1, k2);
		dec->set_tweak((ccxts_ctx *)ctx, (ccxts_tweak *)t, tw);
		dec->xts((ccxts_ctx *)ctx, (ccxts_tweak *)t, 2, out, out);
		expect("AES-XTS decrypt", memcmp(out, pt, 32) == 0);
	}
}

static void
gcm(const char *name, const char *key, const char *iv, const char *aad, const char *pt, const char *ct, const char *tag)
{
	uint8_t k[32], n[64], a[64], p[128], out[128], t[16], ctx[1024];
	size_t kn = unhex(key, k), nn = unhex(iv, n), an = unhex(aad, a), pn = unhex(pt, p);
	const struct ccmode_gcm *enc = T->ccaes_gcm_encrypt, *dec = T->ccaes_gcm_decrypt;
	enc->init(enc, (ccgcm_ctx *)ctx, kn, k);
	enc->set_iv((ccgcm_ctx *)ctx, nn, n);
	enc->gmac((ccgcm_ctx *)ctx, an, a);
	enc->gcm((ccgcm_ctx *)ctx, pn / 3, p, out);  // uneven pieces
	enc->gcm((ccgcm_ctx *)ctx, pn - pn / 3, p + pn / 3, out + pn / 3);
	enc->finalize((ccgcm_ctx *)ctx, 16, t);
	if (pn) {
		check(name, out, ct);
	}
	check(name, t, tag);
	dec->init(dec, (ccgcm_ctx *)ctx, kn, k);
	dec->set_iv((ccgcm_ctx *)ctx, nn, n);
	dec->gmac((ccgcm_ctx *)ctx, an, a);
	dec->gcm((ccgcm_ctx *)ctx, pn, out, out);
	uint8_t t2[16];
	memcpy(t2, t, 16);
	expect(name, dec->finalize((ccgcm_ctx *)ctx, 16, t2) == 0 && memcmp(out, p, pn) == 0);
	dec->reset((ccgcm_ctx *)ctx);
	dec->set_iv((ccgcm_ctx *)ctx, nn, n);
	dec->gmac((ccgcm_ctx *)ctx, an, a);
	dec->gcm((ccgcm_ctx *)ctx, pn, p, out);  // wrong ciphertext
	t2[0] ^= 0x80;
	expect("GCM rejects a bad tag", dec->finalize((ccgcm_ctx *)ctx, 16, t2) != 0);
}

static void
test_gcm(void)
{
	gcm("AES-GCM TC2", "00000000000000000000000000000000", "000000000000000000000000", "", "00000000000000000000000000000000",
	    "0388dace60b6a392f328c2b971b2fe78", "ab6e47d42cec13bdf53a67b21257bddf");
	gcm("AES-GCM TC4", "feffe9928665731c6d6a8f9467308308", "cafebabefacedbaddecaf888", "feedfacedeadbeeffeedfacedeadbeefabaddad2",
	    "d9313225f88406e5a55909c5aff5269a86a7a9531534f7da2e4c303d8a318a721c3c0c95956809532fcf0e2449a6b525b16aedf5aa0de657ba637b39",
	    "42831ec2217774244b7221b784d0d49ce3aa212f2c02a4e035c17e2329aca12e21d514b25466931c7d8f6a5aac84aa051ba30b396a0aac973d58e091",
	    "5bc94fbc3221a5db94fae95ae7121a47");
	gcm("AES-GCM TC5 (64-bit IV)", "feffe9928665731c6d6a8f9467308308", "cafebabefacedbad", "feedfacedeadbeeffeedfacedeadbeefabaddad2",
	    "d9313225f88406e5a55909c5aff5269a86a7a9531534f7da2e4c303d8a318a721c3c0c95956809532fcf0e2449a6b525b16aedf5aa0de657ba637b39",
	    "61353b4c2806934a777ff51fa22a4755699b2a714fcdc6f83766e5f97b6c742373806900e49f24b22b097544d4896b424989b5e1ebac0f07c23f4598",
	    "3612d2e79e3b0785561be14aaca2fccb");

	// init_with_iv and inc_iv, xnu's front ends over ndcrypto's layout.
	uint8_t k[16] = { 0 }, iv[12] = { 0 }, ctx[1024], next[12];
	expect("ccgcm_init_with_iv", T->ccgcm_init_with_iv_fn(T->ccaes_gcm_encrypt, (ccgcm_ctx *)ctx, 16, k, iv) == 0);
	T->ccaes_gcm_encrypt->reset((ccgcm_ctx *)ctx);
	expect("ccgcm_inc_iv", T->ccgcm_inc_iv_fn(T->ccaes_gcm_encrypt, (ccgcm_ctx *)ctx, next) == 0);
	check("ccgcm_inc_iv steps the IV", next, "000000000000000000000001");
}

static void
test_cts3(void)
{
	const char *key = "636869636b656e207465726979616b69";
	const char *pt = "4920776f756c64206c696b65207468652047656e6572616c2047617527732043";
	struct { size_t n; const char *ct, *next; } v[] = {
		{ 17, "c6353568f2bf8cb4d8a580362da7ff7f97", "c6353568f2bf8cb4d8a580362da7ff7f" },
		{ 31, "fc00783e0efdb2c1d445d4c8eff7ed2297687268d6ecccc0c07b25e25ecfe5", "fc00783e0efdb2c1d445d4c8eff7ed22" },
		{ 32, "39312523a78662d5be7fcbcc98ebf5a897687268d6ecccc0c07b25e25ecfe584", "39312523a78662d5be7fcbcc98ebf5a8" },
	};
	uint8_t k[16], p[32], out[32], back[32], iv[16], ctx[1024];
	unhex(key, k);
	unhex(pt, p);
	for (int i = 0; i < 3; i++) {
		char name[48];
		snprintf(name, sizeof(name), "CBC-CS3 RFC 3962 %zu bytes", v[i].n);
		memset(iv, 0, 16);
		T->ccaes_cbc_encrypt->init(T->ccaes_cbc_encrypt, (cccbc_ctx *)ctx, 16, k);
		T->ccpad_cts3_encrypt_fn(T->ccaes_cbc_encrypt, (cccbc_ctx *)ctx, (cccbc_iv *)iv, v[i].n, p, out);
		check(name, out, v[i].ct);
		check(name, iv, v[i].next);
		memset(iv, 0, 16);
		T->ccaes_cbc_decrypt->init(T->ccaes_cbc_decrypt, (cccbc_ctx *)ctx, 16, k);
		T->ccpad_cts3_decrypt_fn(T->ccaes_cbc_decrypt, (cccbc_ctx *)ctx, (cccbc_iv *)iv, v[i].n, out, back);
		expect(name, memcmp(back, p, v[i].n) == 0);
		check(name, iv, v[i].next);
	}
}

static void
test_chachapoly(void)
{
	const char *pt = "Ladies and Gentlemen of the class of '99: If I could offer you only one tip for the future, sunscreen would be it.";
	uint8_t key[32], nonce[12], aad[12], out[128], back[128], tag[16];
	unhex("808182838485868788898a8b8c8d8e8f909192939495969798999a9b9c9d9e9f", key);
	unhex("070000004041424344454647", nonce);
	unhex("50515253c0c1c2c3c4c5c6c7", aad);
	size_t n = strlen(pt);
	ccchacha20poly1305_fns_t f = T->ccchacha20poly1305_fns;
	ccchacha20poly1305_ctx ctx;
	const struct ccchacha20poly1305_info *info = f->info();
	f->init(info, &ctx, key);
	f->setnonce(info, &ctx, nonce);
	f->aad(info, &ctx, sizeof(aad), aad);
	f->encrypt(info, &ctx, 50, pt, out);
	f->encrypt(info, &ctx, n - 50, pt + 50, out + 50);
	f->finalize(info, &ctx, tag);
	check("ChaCha20-Poly1305 RFC 8439 2.8.2", out,
	    "d31a8d34648e60db7b86afbc53ef7ec2a4aded51296e08fea9e2b5a736ee62d63dbea45e8ca9671282fafb69da92728b1a71de0a9e060b2905d6a5b67ecd3b3692ddbd7f2d778b8c9803aee328091b58fab324e4fad675945585808b4831d7bc3ff4def08e4b7a9de576d26586cec64b6116");
	check("ChaCha20-Poly1305 RFC 8439 tag", tag, "1ae10b594f09e26a7e902ecbd0600691");
	f->init(info, &ctx, key);
	f->setnonce(info, &ctx, nonce);
	f->aad(info, &ctx, sizeof(aad), aad);
	f->decrypt(info, &ctx, n, out, back);
	expect("ChaCha20-Poly1305 decrypt and verify", f->verify(info, &ctx, tag) == 0 && memcmp(back, pt, n) == 0);
	f->reset(info, &ctx);
	f->setnonce(info, &ctx, nonce);
	f->aad(info, &ctx, sizeof(aad), aad);
	f->decrypt(info, &ctx, n, out, back);
	tag[15] ^= 1;
	expect("ChaCha20-Poly1305 rejects a bad tag", f->verify(info, &ctx, tag) != 0);
}

static void
test_rsa(void)
{
	static const char mod[] = "d5c013c2ebd79a3c2b3b5341de3d2de0161900a3c7e1b640f1edc435bc73d88ee16c44b80d9d8e706de46d0e318ea6d9bad0ef67a799b98723221365bffc1c88d0209ae58644bc1674c013a7577072586d04f68bc1abc0d67f3c277f7ec4ee5af1a0800169e0d9d4937384e465e3712d73d2a2df7ce13009fadc7aaa21d8a038708b009578a7b9db3a7c094eae82b7ecfbea2e55a263c90baab05086a1130be4efaccbf3a7343b832539376048e09ecc4a0118522a606d416bfe214e86463a7ac5e6fdc3b5b6d55767cb45259d2a8f9790711bee8f7cb16bdf21e6df8a9e8d77125ecbf7eca4776ff6e5b544e897fce5014589539002fc28856051de589f6a3b";
	static const char sig[] = "4146236859e3aabdd04586f5990e1340f44030b51d21ada48b89c0e6ae8bcd2077bf2c5fb0a5f3024cdaf3b7c8ff1b83c64af0e7c7dcc3697476b38117f4a22159fdbc4081efdcff0e16130638ed0182d0e2c68971930e0716e3da67abfb7339f7340596a09403cd9eee67a35b6d1159f869330f928dd081bbff6cc712282e663896d0319a2850af0861a01a945ea8c152150c0cd0c85dd7423be5878c729d49386d20d240d1d828cabe587eb8271117c29f8b00489c8b54e9ab878fc2a0f2812e2827ebf90671bddc8d4700900db053eb645dc91da69d9f2a606e5f9d5f7ab57ed1e6ffddad4f0dbb9fd94c82b3e5dd1adf1bf185f4ea86fe1fb4ef7f275521";
	uint8_t n[256], s[256], e[3] = { 1, 0, 1 }, h[32];
	unhex(mod, n);
	unhex(sig, s);
	unhex("ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad", h);  // SHA-256("abc")
	struct { ccrsa_pub_ctx_decl(ccn_sizeof(4096), key); } pub;
	ccrsa_ctx_n(pub.key) = ccn_nof(4096);
	expect("RSA make_pub", T->ccrsa_make_pub_fn(pub.key, 3, e, 256, n) == 0);
	bool valid = false;
	const uint8_t *oid = CC_DIGEST_OID_SHA256;
	expect("RSA PKCS#1 v1.5 verify (OpenSSL signature)", T->ccrsa_verify_pkcs1v15_fn(pub.key, oid, 32, h, 256, s, &valid) == 0 && valid);
	h[0] ^= 1;
	T->ccrsa_verify_pkcs1v15_fn(pub.key, oid, 32, h, 256, s, &valid);
	expect("RSA rejects a different digest", !valid);
	h[0] ^= 1;
	s[100] ^= 1;
	T->ccrsa_verify_pkcs1v15_fn(pub.key, oid, 32, h, 256, s, &valid);
	expect("RSA rejects a changed signature", !valid);
}

static void
test_random(void)
{
	uint8_t ctx[256], a[64], b[64];
	expect("kmem RNG context fits CRYPTO_RANDOM_MAX_CTX_SIZE", T->random_kmem_ctx_size_fn() <= 256);
	T->random_kmem_init_fn(ctx);
	T->random_generate_fn(ctx, a, sizeof(a));
	T->random_generate_fn(ctx, b, sizeof(b));
	expect("kmem RNG output changes", memcmp(a, b, sizeof(a)) != 0);
	int in_range = 1;
	for (int i = 0; i < 1000; i++) {
		uint64_t x;
		T->random_uniform_fn(ctx, 7, &x);
		in_range &= x < 7;
	}
	expect("random_uniform stays below its bound", in_range);

	// Kernel PRNG, driven the way register_and_init_prng and read_random do.
	struct cckprng_ctx *k = nd_kprng_ctx();
	uint8_t seed[64], nonce[8];
	fill(seed, sizeof(seed));
	fill(nonce, sizeof(nonce));
	nd_kprng_funcs.init_with_getentropy(k, 4, sizeof(seed), seed, sizeof(nonce), nonce, NULL, NULL);
	nd_kprng_funcs.initgen(k, 0);
	nd_kprng_funcs.initgen(k, 1);
	nd_kprng_funcs.refresh(k);
	nd_kprng_funcs.generate(k, 0, 64, a);
	nd_kprng_funcs.generate(k, 1, 64, b);
	expect("per-CPU generators differ", memcmp(a, b, 64) != 0);
	nd_kprng_funcs.reseed(k, 16, seed);
	nd_kprng_funcs.generate(k, 0, 64, b);
	expect("reseed rekeys the generators", memcmp(a, b, 64) != 0);
	int err = -1;
	struct ccrng_state *rng = T->ccrng_fn(&err);
	expect("ccrng", rng != NULL && err == 0 && rng->generate(rng, 16, a) == 0);
}

int
main(void)
{
	T = nd_crypto_functions();
	test_digests();
	test_hmac();
	test_block_ciphers();
	test_cbc_ctr();
	test_xts();
	test_gcm();
	test_cts3();
	test_chachapoly();
	test_rsa();
	test_random();
	printf("ndcrypto known-answer tests: %d passed, %d failed\n", passes, failures);
	return failures ? 1 : 0;
}
