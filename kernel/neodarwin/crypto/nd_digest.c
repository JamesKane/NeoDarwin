// SPDX-License-Identifier: BSD-2-Clause
// NeoDarwin-Language: portability: corecrypto digest descriptors are C structs consumed by XNU.
//
// MD5, SHA-1, SHA-384 and SHA-512 as corecrypto digest descriptors. The block
// functions are FreeBSD's (sys/crypto), driven unmodified: each adapter loads
// the corecrypto state into a FreeBSD context with an empty buffer, hands it
// whole blocks, and stores the state back. xnu's ccdigest_init/update and
// ccdigest_final_64be (SHA-1) do the rest; MD5 and SHA-512 need the
// little-endian and 128-byte-block finals written here.

#include "ndcrypto.h"
#include <corecrypto/ccdigest_priv.h>
#include <corecrypto/cc_priv.h>
#include <string.h>

#include <sys/md5.h>
#include <crypto/sha1.h>
#include <crypto/sha2/sha512.h>

// KAME declares these only under _KERNEL; ndcrypto builds FreeBSD's
// userland branches in both the kernel and host builds.
void sha1_loop(struct sha1_ctxt *, const uint8_t *, size_t);

void ccdigest_final_64be(const struct ccdigest_info *di, ccdigest_ctx_t ctx, unsigned char *digest);

// -- block functions -----------------------------------------------------

static void
md5_compress(ccdigest_state_t state, size_t nblocks, const void *data)
{
	MD5_CTX c;
	memset(&c, 0, sizeof(c));
	memcpy(c.state, state, sizeof(c.state));
	MD5Update(&c, data, (unsigned int)(nblocks * MD5_BLOCK_LENGTH));
	memcpy(state, c.state, sizeof(c.state));
	cc_clear(sizeof(c), &c);
}

static void
sha1_compress(ccdigest_state_t state, size_t nblocks, const void *data)
{
	struct sha1_ctxt c;
	memset(&c, 0, sizeof(c));
	memcpy(c.h.b32, state, sizeof(c.h.b32));
	sha1_loop(&c, data, nblocks * 64);
	memcpy(state, c.h.b32, sizeof(c.h.b32));
	cc_clear(sizeof(c), &c);
}

static void
sha512_compress(ccdigest_state_t state, size_t nblocks, const void *data)
{
	SHA512_CTX c;
	memset(&c, 0, sizeof(c));
	memcpy(c.state, state, sizeof(c.state));
	SHA512_Update(&c, data, nblocks * SHA512_BLOCK_LENGTH);
	memcpy(state, c.state, sizeof(c.state));
	cc_clear(sizeof(c), &c);
}

// -- finals ----------------------------------------------------------------

// Merkle-Damgard padding for a block of `block` bytes ending in a length
// field of `lenfield` bytes. The length is stored big- or little-endian.
static void
final_common(const struct ccdigest_info *di, ccdigest_ctx_t ctx, size_t block, size_t lenfield, bool big_endian)
{
	if (ccdigest_num(di, ctx) >= block) {
		ccdigest_num(di, ctx) = 0;
	}
	ccdigest_nbits(di, ctx) += ccdigest_num(di, ctx) * 8;
	ccdigest_data(di, ctx)[ccdigest_num(di, ctx)++] = 0x80;
	if (ccdigest_num(di, ctx) > block - lenfield) {
		while (ccdigest_num(di, ctx) < block) {
			ccdigest_data(di, ctx)[ccdigest_num(di, ctx)++] = 0;
		}
		di->compress(ccdigest_state(di, ctx), 1, ccdigest_data(di, ctx));
		ccdigest_num(di, ctx) = 0;
	}
	while (ccdigest_num(di, ctx) < block - 8) {
		ccdigest_data(di, ctx)[ccdigest_num(di, ctx)++] = 0;
	}
	uint64_t nbits = ccdigest_nbits(di, ctx);
	if (big_endian) {
		cc_store64_be(nbits, ccdigest_data(di, ctx) + block - 8);
	} else {
		cc_store64_le(nbits, ccdigest_data(di, ctx) + block - 8);
	}
	di->compress(ccdigest_state(di, ctx), 1, ccdigest_data(di, ctx));
}

static void
md5_final(const struct ccdigest_info *di, ccdigest_ctx_t ctx, unsigned char *digest)
{
	ccdigest_di_decl(di, tmp);
	cc_memcpy(tmp, ctx, ccdigest_di_size(di));
	final_common(di, tmp, 64, 8, false);
	for (unsigned i = 0; i < di->output_size / 4; i++) {
		cc_store32_le(ccdigest_state_u32(di, tmp)[i], digest + 4 * i);
	}
	ccdigest_di_clear(di, tmp);
}

// SHA-384/512: 128-byte blocks and a 128-bit length, whose high 64 bits are
// always zero here (2^64 bits is beyond any kernel buffer).
static void
sha512_final(const struct ccdigest_info *di, ccdigest_ctx_t ctx, unsigned char *digest)
{
	ccdigest_di_decl(di, tmp);
	cc_memcpy(tmp, ctx, ccdigest_di_size(di));
	final_common(di, tmp, 128, 16, true);
	for (unsigned i = 0; i < di->output_size / 8; i++) {
		cc_store64_be(ccdigest_state_u64(di, tmp)[i], digest + 8 * i);
	}
	ccdigest_di_clear(di, tmp);
}

// -- descriptors -------------------------------------------------------------

static const uint32_t md5_initial[4] = { 0x67452301, 0xefcdab89, 0x98badcfe, 0x10325476 };
static const uint32_t sha1_initial[5] = { 0x67452301, 0xefcdab89, 0x98badcfe, 0x10325476, 0xc3d2e1f0 };
static const uint64_t sha384_initial[8] = {
	0xcbbb9d5dc1059ed8ULL, 0x629a292a367cd507ULL, 0x9159015a3070dd17ULL, 0x152fecd8f70e5939ULL,
	0x67332667ffc00b31ULL, 0x8eb44a8768581511ULL, 0xdb0c2e0d64f98fa7ULL, 0x47b5481dbefa4fa4ULL,
};
static const uint64_t sha512_initial[8] = {
	0x6a09e667f3bcc908ULL, 0xbb67ae8584caa73bULL, 0x3c6ef372fe94f82bULL, 0xa54ff53a5f1d36f1ULL,
	0x510e527fade682d1ULL, 0x9b05688c2b3e6c1fULL, 0x1f83d9abfb41bd6bULL, 0x5be0cd19137e2179ULL,
};

const struct ccdigest_info nd_md5_di = {
	.output_size = 16, .state_size = 16, .block_size = 64,
	.oid_size = 10, .oid = CC_DIGEST_OID_MD5,
	.initial_state = md5_initial, .compress = md5_compress, .final = md5_final,
};

const struct ccdigest_info nd_sha1_di = {
	.output_size = 20, .state_size = 20, .block_size = 64,
	.oid_size = 7, .oid = CC_DIGEST_OID_SHA1,
	.initial_state = sha1_initial, .compress = sha1_compress, .final = ccdigest_final_64be,
};

const struct ccdigest_info nd_sha384_di = {
	.output_size = 48, .state_size = 64, .block_size = 128,
	.oid_size = 11, .oid = CC_DIGEST_OID_SHA384,
	.initial_state = sha384_initial, .compress = sha512_compress, .final = sha512_final,
};

const struct ccdigest_info nd_sha512_di = {
	.output_size = 64, .state_size = 64, .block_size = 128,
	.oid_size = 11, .oid = CC_DIGEST_OID_SHA512,
	.initial_state = sha512_initial, .compress = sha512_compress, .final = sha512_final,
};
