// SPDX-License-Identifier: BSD-2-Clause
// NeoDarwin-Language: portability: a stand-in with the C interface of a closed Apple library.
//
// dyld's copy of sandbox_check(), which Apple's dyld links statically from
// libsystem_sandbox's closed archive. dyld asks it only to explain an EPERM
// (was the open, stat or mmap blocked by the sandbox?) and whether the
// page-in-linking syscall is allowed. NeoDarwin has no sandbox policy, so
// every check allows (0), as the libsystem_sandbox stand-in answers
// (base/standins/libsystem_sandbox).

#include <sandbox/private.h>

// The flag dyld ORs into the filter type; its value is NeoDarwin's
// (base/sdk's sandbox/private.h), clear of every filter type.
const enum sandbox_filter_type SANDBOX_CHECK_NO_REPORT = (enum sandbox_filter_type)0x40000000;

int
sandbox_check(pid_t pid, const char *operation, enum sandbox_filter_type type, ...)
{
	(void)pid;
	(void)operation;
	(void)type;
	return 0;
}
