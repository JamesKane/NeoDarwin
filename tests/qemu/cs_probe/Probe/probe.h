// SPDX-License-Identifier: BSD-2-Clause
// NeoDarwin-Language: expressibility: a Swift module over libSystem's C headers is declared by a C header.
//
// What cs_probe asks the kernel, through runtime/probe.c: csops(2), which
// the SDK declares only in a private header, and setiopolicy_np(3), dlopen(3)
// and setuid(2), whose headers Embedded Swift imports poorly.
#ifndef CS_PROBE_H
#define CS_PROBE_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

// csops(CS_OPS_STATUS) of this process; false if the call fails.
bool nd_cs_status(uint32_t *flags);
// Whether the flags have CS_PLATFORM_BINARY, CS_VALID, CS_KILL.
bool nd_cs_platform(uint32_t flags);
bool nd_cs_valid(uint32_t flags);
bool nd_cs_kill(uint32_t flags);
// The length of the XML entitlements the kernel grants this process
// (csops CS_OPS_ENTITLEMENTS_BLOB, through ndamfi), 0 for none, -1 on
// error; `name` is set when the XML names that key.
long nd_cs_granted_xml(const char *name, bool *names);
// Drops to uid -2 (nobody) and asks the kernel to force case-sensitive
// lookups for this process, which a non-root process may do only with the
// entitlement com.apple.private.iopol.case_sensitivity (kern_resource.c).
// Returns 0 when granted, else the errno.
int nd_iopol_as_nobody(void);
// The process's argument i (_NSGetArgv), or NULL past the last.
const char *nd_argument(int i);
// dlopen(path); NULL when it loads, else dlerror()'s text.
const char *nd_dlopen_error(const char *path);
#endif
