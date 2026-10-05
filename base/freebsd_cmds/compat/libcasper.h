// SPDX-License-Identifier: BSD-2-Clause
// NeoDarwin-Language: portability: FreeBSD's libcasper as its WITHOUT_CASPER inline fallbacks.
//
// FreeBSD's lib/libcasper/libcasper/libcasper.h without WITH_CASPER: no
// Casper daemon, so cap_init() returns a placeholder channel and each
// service runs in the program itself (casper/cap_*.h).
#ifndef ND_LIBCASPER_H
#define ND_LIBCASPER_H
#include <sys/cdefs.h>
#include <stdlib.h>

typedef struct cap_channel { int cch_fd; } cap_channel_t;

static inline cap_channel_t *
cap_init(void)
{
	return ((cap_channel_t *)calloc(1, sizeof(cap_channel_t)));
}
static inline void cap_close(cap_channel_t *chan) { free(chan); }
static inline cap_channel_t *cap_service_open(const cap_channel_t *chan __unused, const char *name __unused)
{
	return (cap_init());
}
#endif
