// SPDX-License-Identifier: BSD-2-Clause
// NeoDarwin-Language: portability: a wrapper that compiles OpenSSH's C Ed25519 unmodified, in the kernel and on the host.
//
// Ed25519 for ndsign (P2-01, docs/architecture/packaging.md §4): OpenSSH's
// openssh/ed25519.c (public domain, SUPERCOP ref10 as OpenBSD maintains it,
// from @apple_openssh), compiled unmodified. Its "includes.h" and
// "crypto_api.h" are kept out by their include guards; this file supplies
// what they would: the integer types, SHA-512 and random bytes. The same
// file builds into the kernel (bsd/ndamfi, ndamfi's trust-cache grant
// verifier) and into //tools/ndsign on the host, so the host signs exactly
// what the kernel verifies (RFC 8032: signatures are deterministic).

#include "nd_ndsign.h"

#include <stddef.h>
#include <stdint.h>
#include <string.h>

#define INCLUDES_H
#define crypto_api_h

typedef int8_t crypto_int8;
typedef uint8_t crypto_uint8;
typedef int16_t crypto_int16;
typedef uint16_t crypto_uint16;
typedef int32_t crypto_int32;
typedef uint32_t crypto_uint32;
typedef int64_t crypto_int64;
typedef uint64_t crypto_uint64;

#define crypto_hash_sha512(out, in, len) (nd_sha512((out), (in), (size_t)(len)), 0)
#ifdef KERNEL
#define randombytes(buf, len) nd_random_bytes((buf), (size_t)(len))
#else
// Host: nd_ed25519_keypair_from_seed() runs the keypair function with a
// given seed as its randomness (single-threaded tools only).
const uint8_t *nd_ed25519_fixed_seed;
static void
seeded_random(void *buf, size_t len)
{
	if (nd_ed25519_fixed_seed != NULL) {
		memcpy(buf, nd_ed25519_fixed_seed, len);
	} else {
		nd_random_bytes(buf, len);
	}
}
#define randombytes(buf, len) seeded_random((buf), (size_t)(len))
#endif

// The file's external functions, under NeoDarwin's names.
#define fe25519_getparity nd_ed25519_fe25519_getparity
#define ge25519_unpackneg_vartime nd_ed25519_ge25519_unpackneg_vartime
#define ge25519_isneutral_vartime nd_ed25519_ge25519_isneutral_vartime
#define crypto_sign_ed25519_keypair nd_ed25519_keypair_raw
#define crypto_sign_ed25519 nd_ed25519_sign_raw
#define crypto_sign_ed25519_open nd_ed25519_open_raw

int nd_ed25519_keypair_raw(unsigned char *pk, unsigned char *sk);
int nd_ed25519_sign_raw(unsigned char *sm, unsigned long long *smlen, const unsigned char *m,
    unsigned long long mlen, const unsigned char *sk);
int nd_ed25519_open_raw(unsigned char *m, unsigned long long *mlen, const unsigned char *sm,
    unsigned long long smlen, const unsigned char *pk);

// The kernel's overlay and the host's cc_library put OpenSSH's file under
// openssh/; ndpkg's build (base/ndpkg) has that directory itself on the
// include path.
#if __has_include("openssh/ed25519.c")
#include "openssh/ed25519.c"
#else
#include "ed25519.c"
#endif
