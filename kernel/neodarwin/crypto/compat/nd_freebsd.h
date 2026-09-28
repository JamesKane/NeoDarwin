// SPDX-License-Identifier: BSD-2-Clause
// NeoDarwin-Language: portability: the FreeBSD kernel environment FreeBSD's crypto sources assume, supplied so they compile unmodified in XNU and on the host.
//
// Force-included (-include) ahead of every FreeBSD source ndcrypto builds.
#ifndef ND_COMPAT_FREEBSD_H
#define ND_COMPAT_FREEBSD_H
#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include <sys/types.h>

#ifndef __nonstring
#define __nonstring __attribute__((__nonstring__))  // FreeBSD <sys/cdefs.h>
#endif

#ifndef __min_size
#define __min_size(x) static (x)  // FreeBSD <sys/cdefs.h>
#endif

// xnu's libkern KPI exports FreeBSD's DES names (libkern/crypto/corecrypto_des.c)
// and aliases its SHA-1 names in the export lists; ndcrypto's copies of
// FreeBSD's functions take a prefix so both can link.
#define des_ecb_encrypt nd_fb_des_ecb_encrypt
#define des_encrypt1 nd_fb_des_encrypt1
#define des_encrypt2 nd_fb_des_encrypt2
#define des_encrypt3 nd_fb_des_encrypt3
#define des_decrypt3 nd_fb_des_decrypt3
#define des_ecb3_encrypt nd_fb_des_ecb3_encrypt
#define des_set_odd_parity nd_fb_des_set_odd_parity
#define des_fixup_key_parity nd_fb_des_fixup_key_parity
#define des_is_weak_key nd_fb_des_is_weak_key
#define des_set_key nd_fb_des_set_key
#define des_key_sched nd_fb_des_key_sched
#define des_set_key_checked nd_fb_des_set_key_checked
#define des_set_key_unchecked nd_fb_des_set_key_unchecked
#define des_check_key_parity nd_fb_des_check_key_parity
#define des_options nd_fb_des_options
#define des_check_key nd_fb_des_check_key
#define des_SPtrans nd_fb_des_SPtrans
#define sha1_init nd_fb_sha1_init
#define sha1_pad nd_fb_sha1_pad
#define sha1_loop nd_fb_sha1_loop
#define sha1_result nd_fb_sha1_result

// FreeBSD's explicit_bzero: a clear the compiler may not elide.
static inline void
nd_explicit_bzero(void *p, size_t n)
{
	volatile unsigned char *v = p;
	while (n--) {
		*v++ = 0;
	}
}
#define explicit_bzero nd_explicit_bzero

// des_options() (des_ecb.c) formats a description with sprintf; nothing in
// ndcrypto calls it, but it must compile.
#include <stdio.h>
#endif
