// SPDX-License-Identifier: BSD-2-Clause
// NeoDarwin-Language: portability: CommonCrypto's C digest interface, which Apple's C sort calls.
//
// CC_SHA256_Init, CC_SHA256_Update and CC_SHA256_Final for sort
// (sort/commoncrypto.h: sort -R hashes keys with SHA-256). On macOS they
// come from libcommonCrypto, a closed part of libSystem that NeoDarwin
// doesn't stand in for (docs/base/libsystem.md); this is SHA-256 as FIPS
// 180-4 specifies it, over CommonDigest.h's CC_SHA256_CTX, linked into sort
// alone.

#include <stdint.h>
#include <string.h>
#include <CommonCrypto/CommonDigest.h>

static const uint32_t K[64] = {
	0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5,
	0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3, 0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174,
	0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc, 0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
	0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967,
	0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13, 0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85,
	0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
	0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3,
	0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208, 0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2,
};

#define ROR(x, n) (((x) >> (n)) | ((x) << (32 - (n))))

static void
compress(CC_SHA256_CTX *c, const unsigned char *p)
{
	uint32_t w[64], s[8], t1, t2;
	int i;

	for (i = 0; i < 16; i++)
		w[i] = (uint32_t)p[4 * i] << 24 | (uint32_t)p[4 * i + 1] << 16 | (uint32_t)p[4 * i + 2] << 8 | p[4 * i + 3];
	for (; i < 64; i++)
		w[i] = (ROR(w[i - 2], 17) ^ ROR(w[i - 2], 19) ^ (w[i - 2] >> 10)) + w[i - 7] +
		    (ROR(w[i - 15], 7) ^ ROR(w[i - 15], 18) ^ (w[i - 15] >> 3)) + w[i - 16];
	for (i = 0; i < 8; i++)
		s[i] = c->hash[i];
	for (i = 0; i < 64; i++) {
		t1 = s[7] + (ROR(s[4], 6) ^ ROR(s[4], 11) ^ ROR(s[4], 25)) + ((s[4] & s[5]) ^ (~s[4] & s[6])) + K[i] + w[i];
		t2 = (ROR(s[0], 2) ^ ROR(s[0], 13) ^ ROR(s[0], 22)) + ((s[0] & s[1]) ^ (s[0] & s[2]) ^ (s[1] & s[2]));
		memmove(&s[1], &s[0], 7 * sizeof(s[0]));
		s[4] += t1;
		s[0] = t1 + t2;
	}
	for (i = 0; i < 8; i++)
		c->hash[i] += s[i];
}

int
CC_SHA256_Init(CC_SHA256_CTX *c)
{
	static const uint32_t h0[8] = {
		0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a, 0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19,
	};

	memset(c, 0, sizeof(*c));
	memcpy(c->hash, h0, sizeof(h0));
	return (1);
}

// count[0] counts the bytes buffered in wbuf, count[1] the 64-byte blocks done.
int
CC_SHA256_Update(CC_SHA256_CTX *c, const void *data, CC_LONG len)
{
	const unsigned char *p = data;
	unsigned char *buf = (unsigned char *)c->wbuf;

	while (len > 0) {
		CC_LONG n = 64 - c->count[0];

		if (n > len)
			n = len;
		memcpy(buf + c->count[0], p, n);
		c->count[0] += n;
		p += n;
		len -= n;
		if (c->count[0] == 64) {
			compress(c, buf);
			c->count[0] = 0;
			c->count[1]++;
		}
	}
	return (1);
}

int
CC_SHA256_Final(unsigned char *md, CC_SHA256_CTX *c)
{
	unsigned char *buf = (unsigned char *)c->wbuf;
	uint64_t bits = ((uint64_t)c->count[1] * 64 + c->count[0]) * 8;
	int i;

	buf[c->count[0]++] = 0x80;
	if (c->count[0] > 56) {
		memset(buf + c->count[0], 0, 64 - c->count[0]);
		compress(c, buf);
		c->count[0] = 0;
	}
	memset(buf + c->count[0], 0, 56 - c->count[0]);
	for (i = 0; i < 8; i++)
		buf[56 + i] = (unsigned char)(bits >> (56 - 8 * i));
	compress(c, buf);
	for (i = 0; i < 8; i++) {
		md[4 * i] = (unsigned char)(c->hash[i] >> 24);
		md[4 * i + 1] = (unsigned char)(c->hash[i] >> 16);
		md[4 * i + 2] = (unsigned char)(c->hash[i] >> 8);
		md[4 * i + 3] = (unsigned char)c->hash[i];
	}
	memset(c, 0, sizeof(*c));
	return (1);
}
