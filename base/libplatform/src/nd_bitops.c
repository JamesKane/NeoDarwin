// SPDX-License-Identifier: BSD-2-Clause
// NeoDarwin-Language: portability: C library entry points with the C ABI libplatform exports.
//
// ffs, ffsl, fls and flsl for arm64. libplatform exports them on arm64 from
// assembly in src/string/arm64, which Apple did not publish (the tarball has
// only src/string/generic, whose ffsll.c and flsll.c cover the long long
// forms). Semantics follow ffs(3): bit positions count from 1, and 0 means no
// bit is set.

#include <strings.h>

int
ffs(int i)
{
	return i == 0 ? 0 : __builtin_ctz((unsigned int)i) + 1;
}

int
ffsl(long i)
{
	return i == 0 ? 0 : __builtin_ctzl((unsigned long)i) + 1;
}

int
fls(int i)
{
	return i == 0 ? 0 : (int)(sizeof(int) * 8) - __builtin_clz((unsigned int)i);
}

int
flsl(long i)
{
	return i == 0 ? 0 : (int)(sizeof(long) * 8) - __builtin_clzl((unsigned long)i);
}
