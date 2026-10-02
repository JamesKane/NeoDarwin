// SPDX-License-Identifier: BSD-2-Clause
// NeoDarwin-Language: expressibility: a Swift module over libSystem's C headers is declared by a C header.
//
// The libSystem interfaces netconfigd uses.
#ifndef NETCONFIGD_SHIM_H
#define NETCONFIGD_SHIM_H
#include <errno.h>
#include <fcntl.h>
#include <ifaddrs.h>
#include <net/if.h>
#include <net/if_dl.h>
#include <net/route.h>
#include <netinet/in.h>
#include <netinet6/in6_var.h>
#include <poll.h>
#include <signal.h>
#include <spawn.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

// ioctl(2), for IPv6 autoconfiguration (Main.swift, "IPv6"). It is variadic,
// which Swift can't call, and the requests are function-like macros (_IOWR),
// which Swift doesn't import; as launchctl's shim does.
static inline int nd_ioctl(int fd, unsigned long request, void *_Nonnull arg) {
	return ioctl(fd, request, arg);
}

// The interface requests macOS's IPConfiguration makes to start IPv6 on an
// interface, from xnu's <netinet6/in6_var.h>, where they are PRIVATE (the
// SDK's copy leaves them out): attach IPv6 (SIOCPROTOATTACH_IN6), start the
// link-local address (SIOCLL_START) and accept router advertisements
// (SIOCAUTOCONF_START, which sets IFEF_ACCEPT_RTADV).
static const unsigned long ND_SIOCPROTOATTACH_IN6 = _IOWR('i', 110, struct in6_aliasreq);
static const unsigned long ND_SIOCLL_START = _IOWR('i', 130, struct in6_aliasreq);
static const unsigned long ND_SIOCAUTOCONF_START = _IOWR('i', 132, struct in6_ifreq);
#endif
