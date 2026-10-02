// SPDX-License-Identifier: BSD-2-Clause
// NeoDarwin-Language: expressibility: a compat header for FreeBSD's C rtsold, included before its sources.
//
// FreeBSD libc and <sys/cdefs.h> interfaces rtsold uses that Darwin's don't
// have, and xnu's interface flags for patch 0001 (base/rtsold/build.sh
// includes this first in every source). Each FreeBSD one is FreeBSD's
// definition or does what FreeBSD's does. rtsold.h uses <stdio.h>'s FILE
// without including it, and cap_script.c open(2) without <fcntl.h>, which
// FreeBSD's headers bring in on the way.
#ifndef ND_RTSOLD_COMPAT_H
#define ND_RTSOLD_COMPAT_H
#include <fcntl.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <net/if.h>
#include <time.h>
#include <unistd.h>

// <sys/param.h>
#ifndef nitems
#define nitems(x)	(sizeof((x)) / sizeof((x)[0]))
#endif

// <sys/cdefs.h>
#ifndef __DECONST
#define __DECONST(type, var)	((type)(uintptr_t)(const void *)(var))
#endif

// <time.h>: FreeBSD's cheaper, less precise monotonic clock; rtsold's
// timers are in seconds.
#ifndef CLOCK_MONOTONIC_FAST
#define CLOCK_MONOTONIC_FAST	CLOCK_MONOTONIC
#endif

// closefrom(2) (FreeBSD 8): close every descriptor from lowfd up. cap_script.c
// calls it in the child before running a script.
static inline void
nd_closefrom(int lowfd)
{
	int fd, max = getdtablesize();
	for (fd = lowfd; fd < max; fd++)
		(void)close(fd);
}
#define closefrom(fd)	nd_closefrom(fd)

// xnu's extended interface flags (private: <sys/sockio_private.h>,
// <net/if_private.h>), which patch 0001 reads where FreeBSD reads ND flags:
// SIOCGIFEFLAGS returns them in the request's union, a 64-bit word.
#define ND_SIOCGIFEFLAGS	_IOWR('i', 142, struct ifreq)
#define ND_IFEF_IPV6_DISABLED	0x00000020	// coupled to ND6_IFF_IFDISABLED
#define ND_IFEF_ACCEPT_RTADV	0x00000040	// accepts IPv6 RAs on the interface

static inline int
nd_ifeflags(int s, const char *name, uint64_t *eflags)
{
	struct ifreq ifr;

	memset(&ifr, 0, sizeof(ifr));
	strlcpy(ifr.ifr_name, name, sizeof(ifr.ifr_name));
	if (ioctl(s, ND_SIOCGIFEFLAGS, &ifr) < 0)
		return (-1);
	memcpy(eflags, &ifr.ifr_ifru, sizeof(*eflags));
	return (0);
}
#endif
