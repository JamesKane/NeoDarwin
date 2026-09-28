// SPDX-License-Identifier: BSD-2-Clause
// NeoDarwin-Language: portability: maps the libc header FreeBSD's userland crypto branches include onto XNU's kernel equivalent.
#ifndef ND_COMPAT_KERNEL_STDIO_H
#define ND_COMPAT_KERNEL_STDIO_H
#include <stddef.h>
// The RELEASE kernel links snprintf but not sprintf. Only des_options()
// (des_ecb.c) formats, into a fixed array, so bound it by that array.
int snprintf(char *buf, size_t size, const char *fmt, ...);
#define sprintf(buf, ...) snprintf((buf), sizeof(buf), __VA_ARGS__)
#endif
