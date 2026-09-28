// SPDX-License-Identifier: BSD-2-Clause
// NeoDarwin-Language: portability: maps the libc header libsodium includes onto XNU's kernel equivalent, as FreeBSD's sys/crypto/libsodium/stdlib.h does for its kernel.
#ifndef ND_COMPAT_KERNEL_STDLIB_H
#define ND_COMPAT_KERNEL_STDLIB_H
#include <stddef.h>
#include <kern/debug.h>
#define abort() panic("libsodium error at %s:%d", __FILE__, __LINE__)
#endif
