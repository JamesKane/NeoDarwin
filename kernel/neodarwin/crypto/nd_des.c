// SPDX-License-Identifier: BSD-2-Clause
// NeoDarwin-Language: portability: corecrypto mode descriptors are C structs consumed by XNU.
//
// DES and three-key triple DES as corecrypto ECB modes, on FreeBSD's DES
// (sys/crypto/des, Eric Young's SSLeay code). Kept for the legacy consumers
// (NFS Kerberos); the key helpers mirror corecrypto's ccdes_key_is_weak and
// ccdes_key_set_odd_parity over every 8-byte key in the buffer.

#include "nd_modes.h"
#include <corecrypto/cc_error.h>
#include <sys/types.h>
#include <crypto/des/des.h>

struct tdes_ctx {
	des_key_schedule ks[3];
};

static int
des_init(const struct ccmode_ecb *ecb, ccecb_ctx *ctx, size_t key_nbytes, const void *key)
{
	(void)ecb;
	if (key_nbytes != 8) {
		return CCERR_PARAMETER;
	}
	des_set_key_unchecked(key, *(des_key_schedule *)ctx);
	return CCERR_OK;
}

static int
des_crypt(const ccecb_ctx *ctx, size_t nblocks, const void *in, void *out, int direction)
{
	const uint8_t *i = in;
	uint8_t *o = out;
	for (size_t b = 0; b < nblocks; b++, i += 8, o += 8) {
		unsigned char block[8];
		for (int k = 0; k < 8; k++) {
			block[k] = i[k];
		}
		des_ecb_encrypt(block, o, *(des_key_schedule *)(uintptr_t)ctx, direction);
	}
	return CCERR_OK;
}

static int
des_encrypt(const ccecb_ctx *ctx, size_t nblocks, const void *in, void *out)
{
	return des_crypt(ctx, nblocks, in, out, DES_ENCRYPT);
}

static int
des_decrypt(const ccecb_ctx *ctx, size_t nblocks, const void *in, void *out)
{
	return des_crypt(ctx, nblocks, in, out, DES_DECRYPT);
}

static int
tdes_init(const struct ccmode_ecb *ecb, ccecb_ctx *ctx, size_t key_nbytes, const void *key)
{
	(void)ecb;
	if (key_nbytes != 24) {
		return CCERR_PARAMETER;
	}
	struct tdes_ctx *t = (struct tdes_ctx *)ctx;
	const uint8_t *k = key;
	for (int n = 0; n < 3; n++) {
		des_set_key_unchecked(k + 8 * n, t->ks[n]);
	}
	return CCERR_OK;
}

static int
tdes_crypt(const ccecb_ctx *ctx, size_t nblocks, const void *in, void *out, int direction)
{
	struct tdes_ctx *t = (struct tdes_ctx *)(uintptr_t)ctx;
	const uint8_t *i = in;
	uint8_t *o = out;
	for (size_t b = 0; b < nblocks; b++, i += 8, o += 8) {
		unsigned char block[8];
		for (int k = 0; k < 8; k++) {
			block[k] = i[k];
		}
		des_ecb3_encrypt(block, o, t->ks[0], t->ks[1], t->ks[2], direction);
	}
	return CCERR_OK;
}

static int
tdes_encrypt(const ccecb_ctx *ctx, size_t nblocks, const void *in, void *out)
{
	return tdes_crypt(ctx, nblocks, in, out, DES_ENCRYPT);
}

static int
tdes_decrypt(const ccecb_ctx *ctx, size_t nblocks, const void *in, void *out)
{
	return tdes_crypt(ctx, nblocks, in, out, DES_DECRYPT);
}

const struct ccmode_ecb nd_des_ecb_encrypt = { .size = sizeof(des_key_schedule), .block_size = 8, .init = des_init, .ecb = des_encrypt };
const struct ccmode_ecb nd_des_ecb_decrypt = { .size = sizeof(des_key_schedule), .block_size = 8, .init = des_init, .ecb = des_decrypt };
const struct ccmode_ecb nd_tdes_ecb_encrypt = { .size = sizeof(struct tdes_ctx), .block_size = 8, .init = tdes_init, .ecb = tdes_encrypt };
const struct ccmode_ecb nd_tdes_ecb_decrypt = { .size = sizeof(struct tdes_ctx), .block_size = 8, .init = tdes_init, .ecb = tdes_decrypt };

int
nd_des_key_is_weak(void *key, unsigned long length)
{
	const unsigned char *k = key;
	for (unsigned long off = 0; off + 8 <= length; off += 8) {
		if (des_is_weak_key(k + off)) {
			return 1;
		}
	}
	return 0;
}

void
nd_des_key_set_odd_parity(void *key, unsigned long length)
{
	unsigned char *k = key;
	for (unsigned long off = 0; off + 8 <= length; off += 8) {
		des_set_odd_parity(k + off);
	}
}

_Static_assert(sizeof(des_key_schedule) <= 64 * 4, "DES context exceeds libkern's DES_ECB_CTX_MAX_SIZE");
_Static_assert(sizeof(struct tdes_ctx) <= 64 * 4 * 3, "3DES context exceeds libkern's DES3_ECB_CTX_MAX_SIZE");

const struct ccmode_cbc nd_des_cbc_encrypt = ND_CBC_MODE(nd_des_ecb_encrypt, sizeof(des_key_schedule), 8, nd_cbc_encrypt);
const struct ccmode_cbc nd_des_cbc_decrypt = ND_CBC_MODE(nd_des_ecb_decrypt, sizeof(des_key_schedule), 8, nd_cbc_decrypt);
const struct ccmode_cbc nd_tdes_cbc_encrypt = ND_CBC_MODE(nd_tdes_ecb_encrypt, sizeof(struct tdes_ctx), 8, nd_cbc_encrypt);
const struct ccmode_cbc nd_tdes_cbc_decrypt = ND_CBC_MODE(nd_tdes_ecb_decrypt, sizeof(struct tdes_ctx), 8, nd_cbc_decrypt);
