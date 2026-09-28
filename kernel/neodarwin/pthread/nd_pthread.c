// SPDX-License-Identifier: BSD-2-Clause
// NeoDarwin-Language: portability: XNU's startup interface and libpthread's kext entry point are C.
//
// Starts pthread's kernel half, built into the kernel from Apple's libpthread
// (libpthread-539 kern/, APSL; patch 0010). On Apple systems pthread.kext is
// loaded and its pthread_start() registers the pthread function table;
// bsd_init() calls pthread_init() through that table and panics without it.
// With no kext loading at this stage, the kernel calls pthread_start() itself.

#include <kern/debug.h>
#include <kern/startup.h>
#include <mach/kmod.h>

kern_return_t pthread_start(kmod_info_t *ki, void *d);

static void
nd_pthread_start(void)
{
	if (pthread_start(NULL, NULL) != KERN_SUCCESS) {
		panic("NeoDarwin: libpthread's pthread_start failed");
	}
}

STARTUP(EARLY_BOOT, STARTUP_RANK_LAST, nd_pthread_start);
