// SPDX-License-Identifier: BSD-2-Clause
// NeoDarwin-Language: portability: XNU's crypto_random and cckprng interfaces are C function tables.
//
// Randomness for XNU, in two layers:
//
//  - A ChaCha20 generator with fast key erasure (Bernstein): each 64-byte
//    keystream block rekeys the generator with its first half and yields the
//    second. ChaCha20 is FreeBSD's (sys/crypto/chacha20). The kmem slot
//    randomiser uses one per CPU (crypto_random_kmem_*); the kernel PRNG
//    below uses one per CPU for read_random().
//  - The kernel PRNG (cckprng_funcs, installed with register_and_init_prng):
//    xnu's own HMAC-DRBG (SHA-256) is the central pool, seeded at boot and
//    fed from the kernel's entropy source on refresh; per-CPU generators
//    rekey from it whenever its epoch advances.

#include "ndcrypto.h"
#include <corecrypto/cc_error.h>
#include <corecrypto/cc_priv.h>
#include <corecrypto/ccdrbg.h>
#include <corecrypto/cckprng.h>
#include <corecrypto/ccsha2.h>
#include <stdatomic.h>
#include <string.h>

#include <sys/types.h>
#include <crypto/chacha20/chacha.h>

// -- ChaCha20 generator ---------------------------------------------------------

struct nd_chacha_gen {
	struct chacha_ctx c;
	uint8_t out[32];
	uint8_t avail;
};

static void
gen_key(struct nd_chacha_gen *g, const uint8_t key[32])
{
	static const uint8_t zero[8];
	chacha_keysetup(&g->c, key, 256);
	chacha_ivsetup(&g->c, zero, zero);
	g->avail = 0;
}

static void
gen_bytes(struct nd_chacha_gen *g, void *out, size_t nbytes)
{
	uint8_t *o = out;
	while (nbytes > 0) {
		if (g->avail == 0) {
			uint8_t block[64] = { 0 };
			chacha_encrypt_bytes(&g->c, block, block, 64);
			gen_key(g, block);  // the first half becomes the next key
			memcpy(g->out, block + 32, 32);
			g->avail = 32;
			cc_clear(sizeof(block), block);
		}
		size_t n = nbytes < g->avail ? nbytes : g->avail;
		memcpy(o, g->out + (32 - g->avail), n);
		cc_clear(n, g->out + (32 - g->avail));
		g->avail = (uint8_t)(g->avail - n);
		o += n;
		nbytes -= n;
	}
}

// -- crypto_random (kmem) ----------------------------------------------------------

size_t
nd_random_kmem_ctx_size(void)
{
	return sizeof(struct nd_chacha_gen);  // <= CRYPTO_RANDOM_MAX_CTX_SIZE (256); checked in nd_register.c
}

void
nd_random_kmem_init(void *ctx)
{
	uint8_t key[32];
	nd_platform_seed(key, sizeof(key));
	gen_key(ctx, key);
	cc_clear(sizeof(key), key);
}

void
nd_random_generate(void *ctx, void *out, size_t nbytes)
{
	gen_bytes(ctx, out, nbytes);
}

// Uniform in [0, bound) by rejection: discard draws below 2^64 mod bound.
void
nd_random_uniform(void *ctx, uint64_t bound, uint64_t *out)
{
	if (bound <= 1) {
		*out = 0;
		return;
	}
	uint64_t threshold = (0 - bound) % bound;
	uint64_t x;
	do {
		gen_bytes(ctx, &x, sizeof(x));
	} while (x < threshold);
	*out = x % bound;
}

// -- ccrng ---------------------------------------------------------------------------

static int
rng_generate(struct ccrng_state *rng, size_t nbytes, void *out)
{
	(void)rng;
	nd_platform_random(out, nbytes);
	return CCERR_OK;
}

static struct ccrng_state nd_rng_state = { .generate = rng_generate };

struct ccrng_state *
nd_rng(int *error)
{
	if (error) {
		*error = CCERR_OK;
	}
	return &nd_rng_state;
}

// -- kernel PRNG ---------------------------------------------------------------------

#define ND_KPRNG_MAX_GENS 64
#define ND_KPRNG_REFRESH_PERIOD 256  // refreshes between entropy pulls
#define ND_KPRNG_DRBG_STATE 1280     // CCKPRNG_DRBG_STATE_MAX_SIZE

struct nd_kprng {
	atomic_flag lock;
	struct ccdrbg_info info;
	struct ccdrbg_nisthmac_custom custom;
	_Alignas(16) uint8_t drbg[ND_KPRNG_DRBG_STATE];
	_Atomic uint64_t epoch;
	_Atomic uint32_t refreshes;
	cckprng_getentropy getentropy;
	void *getentropy_arg;
	unsigned ngens;
	struct {
		uint64_t epoch;  // 0: not yet keyed
		struct nd_chacha_gen gen;
	} gens[ND_KPRNG_MAX_GENS];
};

static struct nd_kprng kprng = { .lock = ATOMIC_FLAG_INIT };

struct cckprng_ctx *
nd_kprng_ctx(void)
{
	return (struct cckprng_ctx *)(void *)&kprng;
}

static void
lock(struct nd_kprng *k)
{
	while (atomic_flag_test_and_set_explicit(&k->lock, memory_order_acquire)) {
	}
}

static void
unlock(struct nd_kprng *k)
{
	atomic_flag_clear_explicit(&k->lock, memory_order_release);
}

static void
drbg_generate(struct nd_kprng *k, size_t nbytes, void *out)
{
	int rc = ccdrbg_generate(&k->info, (struct ccdrbg_state *)k->drbg, nbytes, out, 0, NULL);
	if (rc != CCDRBG_STATUS_OK) {
		cc_abort("ndcrypto: kernel PRNG DRBG failed");
	}
}

static void
kprng_init_with_getentropy(struct cckprng_ctx *ctx, unsigned max_ngens, size_t seed_nbytes, const void *seed,
    size_t nonce_nbytes, const void *nonce, cckprng_getentropy getentropy, void *getentropy_arg)
{
	struct nd_kprng *k = (struct nd_kprng *)(void *)ctx;
	k->custom.di = ccsha256_di();
	k->custom.strictFIPS = 0;
	ccdrbg_factory_nisthmac(&k->info, &k->custom);
	if (k->info.size > sizeof(k->drbg) || max_ngens > ND_KPRNG_MAX_GENS) {
		cc_abort("ndcrypto: kernel PRNG state too small");
	}
	static const char ps[] = "NeoDarwin ndcrypto kernel PRNG";
	if (ccdrbg_init(&k->info, (struct ccdrbg_state *)k->drbg, seed_nbytes, seed, nonce_nbytes, nonce, sizeof(ps) - 1, ps) != CCDRBG_STATUS_OK) {
		cc_abort("ndcrypto: kernel PRNG DRBG init failed");
	}
	k->getentropy = getentropy;
	k->getentropy_arg = getentropy_arg;
	k->ngens = max_ngens;
	atomic_store(&k->epoch, 1);
}

static void
kprng_init(struct cckprng_ctx *ctx, size_t seed_nbytes, const void *seed, size_t nonce_nbytes, const void *nonce,
    cckprng_getentropy getentropy, void *getentropy_arg)
{
	kprng_init_with_getentropy(ctx, ND_KPRNG_MAX_GENS, seed_nbytes, seed, nonce_nbytes, nonce, getentropy, getentropy_arg);
}

static void
kprng_initgen(struct cckprng_ctx *ctx, unsigned gen_idx)
{
	struct nd_kprng *k = (struct nd_kprng *)(void *)ctx;
	if (gen_idx >= k->ngens) {
		cc_abort("ndcrypto: kernel PRNG generator index out of range");
	}
	k->gens[gen_idx].epoch = 0;  // keyed on first use
}

static void
kprng_reseed(struct cckprng_ctx *ctx, size_t nbytes, const void *seed)
{
	struct nd_kprng *k = (struct nd_kprng *)(void *)ctx;
	lock(k);
	(void)ccdrbg_reseed(&k->info, (struct ccdrbg_state *)k->drbg, nbytes, seed, 0, NULL);
	atomic_fetch_add(&k->epoch, 1);
	unlock(k);
}

// Every ND_KPRNG_REFRESH_PERIOD calls, pull what the kernel's entropy source
// has and reseed the pool. getentropy may decline (it try-locks); that is fine.
static void
kprng_refresh(struct cckprng_ctx *ctx)
{
	struct nd_kprng *k = (struct nd_kprng *)(void *)ctx;
	if (k->getentropy == NULL || atomic_fetch_add(&k->refreshes, 1) % ND_KPRNG_REFRESH_PERIOD != 0) {
		return;
	}
	uint8_t entropy[CCKPRNG_ENTROPY_SIZE];
	size_t n = sizeof(entropy);
	if (k->getentropy(&n, entropy, k->getentropy_arg) > 0 && n > 0) {
		kprng_reseed(ctx, n, entropy);
	}
	cc_clear(sizeof(entropy), entropy);
}

static void
kprng_generate(struct cckprng_ctx *ctx, unsigned gen_idx, size_t nbytes, void *out)
{
	struct nd_kprng *k = (struct nd_kprng *)(void *)ctx;
	if (gen_idx >= k->ngens || nbytes > CCKPRNG_GENERATE_MAX_NBYTES) {
		cc_abort("ndcrypto: kernel PRNG generate contract violated");
	}
	uint64_t epoch = atomic_load(&k->epoch);
	if (k->gens[gen_idx].epoch != epoch) {
		uint8_t key[32];
		lock(k);
		drbg_generate(k, sizeof(key), key);
		unlock(k);
		gen_key(&k->gens[gen_idx].gen, key);
		cc_clear(sizeof(key), key);
		k->gens[gen_idx].epoch = epoch;
	}
	gen_bytes(&k->gens[gen_idx].gen, out, nbytes);
}

const struct cckprng_funcs nd_kprng_funcs = {
	.init = kprng_init, .initgen = kprng_initgen, .reseed = kprng_reseed, .refresh = kprng_refresh,
	.generate = kprng_generate, .init_with_getentropy = kprng_init_with_getentropy,
};
