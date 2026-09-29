/*
 * SPDX-License-Identifier: BSD-2-Clause
 * NeoDarwin-Language: expressibility: a C header standing in for one from Apple's internal SDK.
 *
 * Stands in for the internal SDK's <sandbox/private.h>, the private
 * interface of libsystem_sandbox, which Apple does not publish. It declares
 * the part NeoDarwin's libraries use: sandbox_check(), which asks whether a
 * process's sandbox allows an operation, with the filter types in Apple's
 * numbering. libsystem_sandbox is a stand-in (base/standins); NeoDarwin has
 * no sandbox policy, so every check allows.
 *
 * dyld links its own copy statically (base/dyld/src/nd_sandbox.c), as Apple's
 * dyld does from a static archive; it also passes SANDBOX_FILTER_SYSCALL_NUMBER
 * and ORs SANDBOX_CHECK_NO_REPORT into the filter type. Apple publishes
 * neither value: SANDBOX_CHECK_NO_REPORT is a constant the library defines
 * (as Apple's is), and SANDBOX_FILTER_SYSCALL_NUMBER's value is NeoDarwin's,
 * past the numbered filters above.
 */
#ifndef _SANDBOX_PRIVATE_H_
#define _SANDBOX_PRIVATE_H_

#include <sys/cdefs.h>
#include <sys/types.h>

__BEGIN_DECLS

enum sandbox_filter_type {
	SANDBOX_FILTER_NONE,
	SANDBOX_FILTER_PATH,
	SANDBOX_FILTER_GLOBAL_NAME,
	SANDBOX_FILTER_LOCAL_NAME,
	SANDBOX_FILTER_SYSCALL_NUMBER = 0x100,
};

// A flag ORed into the filter type: check without logging a violation.
extern const enum sandbox_filter_type SANDBOX_CHECK_NO_REPORT;

// Returns 0 if pid's sandbox allows operation on the filter's argument (the
// variadic argument, typed by type), 1 if it denies it, -1 on error.
int sandbox_check(pid_t pid, const char *operation, enum sandbox_filter_type type, ...);

__END_DECLS

#endif /* _SANDBOX_PRIVATE_H_ */
