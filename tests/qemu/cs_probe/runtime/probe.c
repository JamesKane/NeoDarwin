// SPDX-License-Identifier: BSD-2-Clause
// NeoDarwin-Language: expressibility: csops(2) is declared only in the SDK's private headers, and setiopolicy_np's and dlopen's macros don't import into Embedded Swift.
#include <crt_externs.h>
#include <dlfcn.h>
#include <errno.h>
#include <sys/resource.h>
#include <arpa/inet.h>

#include "probe.h"

// <sys/codesign.h> (XNU's bsd/sys/codesign.h), not in the public SDK.
int csops(pid_t pid, unsigned int ops, void *useraddr, size_t usersize);
#define CS_OPS_STATUS            0
#define CS_OPS_ENTITLEMENTS_BLOB 7
#define CS_VALID                 0x00000001
#define CS_KILL                  0x00000200
#define CS_PLATFORM_BINARY       0x04000000
// <sys/resource_private.h>, also private.
#define IOPOL_TYPE_VFS_HFS_CASE_SENSITIVITY                 1
#define IOPOL_VFS_HFS_CASE_SENSITIVITY_FORCE_CASE_SENSITIVE 1

bool
nd_cs_status(uint32_t *flags)
{
	return csops(getpid(), CS_OPS_STATUS, flags, sizeof(*flags)) == 0;
}

bool nd_cs_platform(uint32_t flags) { return (flags & CS_PLATFORM_BINARY) != 0; }
bool nd_cs_valid(uint32_t flags) { return (flags & CS_VALID) != 0; }
bool nd_cs_kill(uint32_t flags) { return (flags & CS_KILL) != 0; }

long
nd_cs_granted_xml(const char *name, bool *names)
{
	// The blob: magic, big-endian length, then the plist. csops copies
	// nothing when the kernel grants no entitlements.
	static uint8_t buffer[16384];
	memset(buffer, 0, sizeof(buffer));
	*names = false;
	if (csops(getpid(), CS_OPS_ENTITLEMENTS_BLOB, buffer, sizeof(buffer)) != 0) {
		return errno == ERANGE ? (long)sizeof(buffer) : -1;
	}
	uint32_t length;
	memcpy(&length, buffer + 4, sizeof(length));
	length = ntohl(length);
	if (length <= 8 || length > sizeof(buffer)) {
		return 0;
	}
	char key[256];
	snprintf(key, sizeof(key), "<key>%s</key>", name);
	*names = memmem(buffer + 8, length - 8, key, strlen(key)) != NULL;
	return (long)length - 8;
}

int
nd_iopol_as_nobody(void)
{
	if (setuid((uid_t)-2) != 0) {
		return errno;
	}
	if (setiopolicy_np(IOPOL_TYPE_VFS_HFS_CASE_SENSITIVITY, IOPOL_SCOPE_PROCESS,
	    IOPOL_VFS_HFS_CASE_SENSITIVITY_FORCE_CASE_SENSITIVE) != 0) {
		return errno;
	}
	return 0;
}

const char *
nd_argument(int i)
{
	return i >= 0 && i < *_NSGetArgc() ? (*_NSGetArgv())[i] : NULL;
}

const char *
nd_dlopen_error(const char *path)
{
	void *h = dlopen(path, RTLD_NOW | RTLD_LOCAL);
	if (h != NULL) {
		dlclose(h);
		return NULL;
	}
	const char *why = dlerror();
	return why ? why : "unknown";
}
