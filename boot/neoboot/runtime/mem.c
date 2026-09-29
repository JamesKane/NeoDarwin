// SPDX-License-Identifier: BSD-2-Clause
// NeoDarwin-Language: expressibility: memory routines the compiler emits calls to; built with -fno-builtin so they cannot recurse into themselves.
#include <stddef.h>
void *memset(void *d, int c, size_t n) { unsigned char *p = d; while (n--) *p++ = (unsigned char)c; return d; }
void *memcpy(void *d, const void *s, size_t n) { unsigned char *p = d; const unsigned char *q = s; while (n--) *p++ = *q++; return d; }
void *memmove(void *d, const void *s, size_t n) { unsigned char *p = d; const unsigned char *q = s; if (p < q) { while (n--) *p++ = *q++; } else { p += n; q += n; while (n--) *--p = *--q; } return d; }

// The Windows AArch64 code generator calls __chkstk before allocating a frame
// over 4 KiB, with the size / 16 in x15, to touch each guard page in turn.
// UEFI stacks are committed up front and have no guard pages, so there is
// nothing to probe; x15 and every other register are left as they are.
__attribute__((naked)) void __chkstk(void) { __asm__ volatile("ret"); }
