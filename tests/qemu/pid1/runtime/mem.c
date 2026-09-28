// SPDX-License-Identifier: BSD-2-Clause
// NeoDarwin-Language: expressibility: memory routines the compiler emits calls to; built with -fno-builtin so they cannot recurse into themselves.
#include <stddef.h>
void *memset(void *d, int c, size_t n) { unsigned char *p = d; while (n--) *p++ = (unsigned char)c; return d; }
void *memcpy(void *d, const void *s, size_t n) { unsigned char *p = d; const unsigned char *q = s; while (n--) *p++ = *q++; return d; }
void *memmove(void *d, const void *s, size_t n) { unsigned char *p = d; const unsigned char *q = s; if (p < q) { while (n--) *p++ = *q++; } else { p += n; q += n; while (n--) *--p = *--q; } return d; }
void bzero(void *d, size_t n) { memset(d, 0, n); }
