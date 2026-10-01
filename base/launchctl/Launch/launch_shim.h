// SPDX-License-Identifier: BSD-2-Clause
// NeoDarwin-Language: expressibility: a Swift module over libSystem's C headers is declared by a C header.
//
// The interfaces launchctl uses: libSystem's C headers, launchd's launch_msg
// interface and one private launchd call.
//
// macOS's <launch.h> marks launch_msg and launch_data deprecated since 10.10,
// in favour of XPC services that NeoDarwin's launchd-842 doesn't provide;
// launch_msg is how its launchctl talks to it (docs/base/session.md). The
// SDK header is part of the Darwin module, so its marking can't be undone
// for one import; the calls launchctl uses are declared here instead, as
// launchd-842's own <launch.h> (launchd-842.92.1 liblaunch/launch.h), which
// predates the deprecation, declares them. libxpc implements them.
#ifndef LAUNCHCTL_LAUNCH_SHIM_H
#define LAUNCHCTL_LAUNCH_SHIM_H
#include <stdbool.h>
#include <stddef.h>

#define LAUNCH_KEY_SUBMITJOB "SubmitJob"
#define LAUNCH_KEY_REMOVEJOB "RemoveJob"
#define LAUNCH_KEY_STARTJOB "StartJob"
#define LAUNCH_KEY_STOPJOB "StopJob"
#define LAUNCH_KEY_GETJOBS "GetJobs"
#define LAUNCH_JOBKEY_LABEL "Label"
#define LAUNCH_JOBKEY_DISABLED "Disabled"
#define LAUNCH_JOBKEY_PID "PID"
#define LAUNCH_JOBKEY_LASTEXITSTATUS "LastExitStatus"
#define LAUNCH_JOBKEY_SOCKETS "Sockets"

typedef struct _launch_data *launch_data_t;
typedef void (*launch_data_dict_iterator_t)(const launch_data_t _Nullable lval, const char *_Nullable key,
    void *_Nullable ctx);

typedef enum {
	LAUNCH_DATA_DICTIONARY = 1,
	LAUNCH_DATA_ARRAY,
	LAUNCH_DATA_FD,
	LAUNCH_DATA_INTEGER,
	LAUNCH_DATA_REAL,
	LAUNCH_DATA_BOOL,
	LAUNCH_DATA_STRING,
	LAUNCH_DATA_OPAQUE,
	LAUNCH_DATA_ERRNO,
	LAUNCH_DATA_MACHPORT,
} launch_data_type_t;

launch_data_t _Nullable launch_data_alloc(launch_data_type_t type);
void launch_data_free(launch_data_t _Nonnull ld);
launch_data_type_t launch_data_get_type(const launch_data_t _Nonnull ld);
bool launch_data_dict_insert(launch_data_t _Nonnull ldict, const launch_data_t _Nonnull lval, const char *_Nonnull key);
launch_data_t _Nullable launch_data_dict_lookup(const launch_data_t _Nonnull ldict, const char *_Nonnull key);
void launch_data_dict_iterate(const launch_data_t _Nonnull ldict, launch_data_dict_iterator_t _Nonnull iterator,
    void *_Nullable ctx);
bool launch_data_array_set_index(launch_data_t _Nonnull larray, const launch_data_t _Nonnull lval, size_t idx);
launch_data_t _Nullable launch_data_new_integer(long long val);
launch_data_t _Nullable launch_data_new_bool(bool val);
launch_data_t _Nullable launch_data_new_real(double val);
launch_data_t _Nullable launch_data_new_string(const char *_Nonnull val);
launch_data_t _Nullable launch_data_new_opaque(const void *_Nullable bytes, size_t sz);
launch_data_t _Nullable launch_data_new_fd(int fd);
long long launch_data_get_integer(const launch_data_t _Nonnull ld);
int launch_data_get_errno(const launch_data_t _Nonnull ld);
// Sends a request to launchd and returns its reply, NULL with errno set on failure.
launch_data_t _Nullable launch_msg(const launch_data_t _Nonnull request);

#include <dirent.h>
#include <dlfcn.h>
#include <stdbool.h>
#include <errno.h>
#include <netdb.h>
#include <net/if.h>
#include <netinet/in.h>
#include <netinet6/in6_var.h>
#include <netinet6/nd6.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <spawn.h>
#include <sys/mount.h>
#include <sys/ioctl.h>
#include <sys/reboot.h>
#include <sys/socket.h>
#include <sys/sysctl.h>
#include <sys/time.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

// launchd-842's libvproc (vproc_priv.h): while "global on demand" is set,
// launchd starts no job for Mach-service demand, so a bootstrap loads every
// job before any starts. Returns NULL on success.
void *_Nullable _vproc_set_global_on_demand(bool val);

// ioctl(2), for the loopback interface (Network.swift). It is variadic,
// which Swift can't call (arm64 Darwin passes variadic arguments on the
// stack, so no non-variadic declaration of it is right either), and the
// interface requests are function-like macros (_IOW, _IOWR), which Swift
// doesn't import.
static inline int nd_ioctl(int fd, unsigned long request, void *_Nonnull arg) {
	return ioctl(fd, request, arg);
}
static const unsigned long ND_SIOCGIFFLAGS = SIOCGIFFLAGS;
static const unsigned long ND_SIOCSIFFLAGS = SIOCSIFFLAGS;
static const unsigned long ND_SIOCAIFADDR = SIOCAIFADDR;
static const unsigned long ND_SIOCAIFADDR_IN6 = SIOCAIFADDR_IN6;
#endif
