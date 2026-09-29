// SPDX-License-Identifier: BSD-2-Clause
// NeoDarwin-Language: portability: the two <ctype.h> functions msun's nan() uses, without Darwin's locale-aware ones that live in libsystem_c.
//
// nan(3)'s argument is hex digits in the C locale. Darwin's <ctype.h> inlines
// isxdigit through the rune tables and digittoint through __maskrune, both in
// libsystem_c, which libsystem_m sits below (Apple's links only libdyld and
// libcompiler_rt).
#ifndef ND_MSUN_CTYPE_H
#define ND_MSUN_CTYPE_H
static inline int
isxdigit(int c)
{
	return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F');
}

static inline int
digittoint(int c)
{
	return c <= '9' ? c - '0' : (c | 0x20) - 'a' + 10;
}
#endif
