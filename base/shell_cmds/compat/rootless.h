// SPDX-License-Identifier: BSD-2-Clause
// NeoDarwin-Language: expressibility: a compat header for Apple's C su(1), which includes it.
//
// libsystem_sandbox's private <rootless.h> (closed; not in the SDK). su(1)
// asks rootless_restricted_environment() whether it runs in the macOS
// installer's restricted environment, where it spawns only /bin/sh.
// NeoDarwin has no such environment: the answer is always 0 (not restricted).
#ifndef ND_ROOTLESS_H
#define ND_ROOTLESS_H
static inline int
rootless_restricted_environment(void)
{
	return 0;
}
#endif
