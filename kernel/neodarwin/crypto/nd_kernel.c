// SPDX-License-Identifier: BSD-2-Clause
// NeoDarwin-Language: portability: XNU's startup and registration interfaces are C.
//
// Installs ndcrypto in XNU at EARLY_BOOT, before kmem_crypto_init
// (osfmk/vm/vm_kern.c, EARLY_BOOT rank MIDDLE): first the crypto_functions
// table, then the kernel PRNG behind read_random(), whose setup uses the table. Apple's corecrypto kext
// does both on Apple systems.

#include "ndcrypto.h"
#include <kern/debug.h>
#include <kern/startup.h>
#include <prng/random.h>
#include <sys/random.h>
#include <libkern/crypto/register_crypto.h>

void kprintf(const char *fmt, ...);

// Seed material before the kernel PRNG exists: read_frandom() draws on
// xnu's early DRBG, seeded from the loader's /chosen/random-seed, and on the
// kernel PRNG once it is installed.
void
nd_platform_seed(void *out, size_t nbytes)
{
	read_frandom(out, (u_int)nbytes);
}

void
nd_platform_random(void *out, size_t nbytes)
{
	read_random(out, (u_int)nbytes);
}

static void
ndcrypto_register(void)
{
	// The table first: register_and_init_prng() -> entropy_init() hashes
	// with SHA-512 through it.
	if (register_crypto_functions((crypto_functions_t)(uintptr_t)nd_crypto_functions()) != 0) {
		panic("ndcrypto: register_crypto_functions failed");
	}
	register_and_init_prng(nd_kprng_ctx(), &nd_kprng_funcs);
	kprintf("ndcrypto: kernel crypto provider registered\n");
}

STARTUP(EARLY_BOOT, STARTUP_RANK_FIRST, ndcrypto_register);
