// SPDX-License-Identifier: BSD-2-Clause
// NeoDarwin-Language: portability: libquarantine's responsibility C interface as Apple's C gcore calls it.
//
// The responsibility SPI gcore's notes.c calls for its crash-info note
// (base/system_cmds/build.sh). macOS implements it in libquarantine, which
// is closed, over the Quarantine MAC policy's responsibility tracking.
// NeoDarwin has neither, so every process is its own responsible process:
// what macOS reports for a process nobody else is responsible for.
#ifndef _ND_RESPONSIBILITY_H_
#define _ND_RESPONSIBILITY_H_
#include <stddef.h>
#include <stdint.h>
#include <sys/types.h>
#include <mach/kern_return.h>

static inline kern_return_t
responsibility_get_responsible_for_pid(pid_t pid, pid_t *rpid, uint64_t *rpidversion, size_t *pathlen, char *path)
{
	(void)rpidversion; (void)pathlen; (void)path;
	if (rpid != NULL)
		*rpid = pid;
	return KERN_SUCCESS;
}
#endif
