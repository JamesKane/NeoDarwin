// SPDX-License-Identifier: BSD-2-Clause
// NeoDarwin-Language: portability: a stand-in with the C interface of a closed Apple library.
//
// Stand-in for libcorecrypto.dylib, which Apple does not publish (only its
// interface headers, in xnu's EXTERNAL_HEADERS/corecrypto). Libc uses its
// random-number generator: ccrng() for arc4random's seed and ccrng_uniform()
// for arc4random_uniform. Both draw from the kernel's entropy (getentropy),
// the source Libc's own static build uses. Other corecrypto interfaces are
// added as NeoDarwin libraries come to need them (docs/base/libsystem.md).

#include <corecrypto/cc_error.h>
#include <corecrypto/ccrng.h>
#include <stdint.h>
#include <sys/random.h>

static int
nd_generate(struct ccrng_state *rng, size_t outlen, void *out)
{
	(void)rng;
	uint8_t *p = out;
	while (outlen > 0) {
		size_t n = outlen > 256 ? 256 : outlen;   // getentropy's limit per call
		if (getentropy(p, n) != 0) {
			return CCERR_INTERNAL;
		}
		p += n;
		outlen -= n;
	}
	return CCERR_OK;
}

static struct ccrng_state nd_rng = { .generate = nd_generate };

struct ccrng_state *
ccrng(int *error)
{
	if (error != NULL) {
		*error = CCERR_OK;
	}
	return &nd_rng;
}

// A uniform value in [0, bound): draws below 2^64 mod bound would bias the
// result towards small values, so they are drawn again.
int
ccrng_uniform(struct ccrng_state *rng, uint64_t bound, uint64_t *rand)
{
	if (bound == 0 || rand == NULL) {
		return CCERR_PARAMETER;
	}
	uint64_t threshold = (0 - bound) % bound, x;
	do {
		int err = ccrng_generate(rng, sizeof(x), &x);
		if (err != CCERR_OK) {
			return err;
		}
	} while (x < threshold);
	*rand = x % bound;
	return CCERR_OK;
}
