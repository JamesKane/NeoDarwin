// SPDX-License-Identifier: BSD-2-Clause
// NeoDarwin-Language: portability: FreeBSD's per-architecture long double layout for fpmath.h, which FreeBSD has for no platform matching arm64 Darwin.
//
// On arm64 Darwin, long double is IEEE double (LDBL_MANT_DIG 53), little-
// endian in both byte and word order. FreeBSD's aarch64 header describes the
// 128-bit format, and its arm header picks the word order from __VFP_FP__ or
// __ARM_EABI__, which clang doesn't define for arm64, so neither fits. This
// is the arm layout with that choice made.
#ifndef ND_MSUN_FPMATH_H
#define ND_MSUN_FPMATH_H

#define _IEEE_WORD_ORDER _LITTLE_ENDIAN

union IEEEl2bits {
	long double e;
	struct {
		unsigned int manl : 32;
		unsigned int manh : 20;
		unsigned int exp : 11;
		unsigned int sign : 1;
	} bits;
};

#define LDBL_NBIT 0
#define LDBL_IMPLICIT_NBIT
#define mask_nbit_l(u) ((void)0)

#define LDBL_MANH_SIZE 20
#define LDBL_MANL_SIZE 32

#define LDBL_TO_ARRAY32(u, a) do {           \
	(a)[0] = (uint32_t)(u).bits.manl;    \
	(a)[1] = (uint32_t)(u).bits.manh;    \
} while (0)
#endif
