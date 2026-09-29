// SPDX-License-Identifier: BSD-2-Clause
// NeoDarwin-Language: portability: a stand-in with the C interface of a closed Apple library.
//
// Stand-in for libsystem_sandbox.dylib, the client side of Apple's sandbox
// (Sandbox.kext's policy), which Apple does not publish. objc4 uses
// sandbox_check() to decide whether its error messages may go to syslog.
// NeoDarwin has no sandbox policy, so every process is unsandboxed and every
// check allows (0), as sandbox_check() answers for an unsandboxed process.
// More of the interface is added as NeoDarwin libraries come to need it
// (docs/base/libsystem.md).

#include <sandbox/private.h>

int
sandbox_check(pid_t pid, const char *operation, enum sandbox_filter_type type, ...)
{
	(void)pid;
	(void)operation;
	(void)type;
	return 0;
}
