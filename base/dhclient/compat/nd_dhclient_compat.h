// SPDX-License-Identifier: BSD-2-Clause
// NeoDarwin-Language: expressibility: a compat header for FreeBSD's C dhclient, included before its sources.
//
// FreeBSD libc and <sys/param.h> interfaces dhclient uses that Darwin's
// don't have (base/dhclient/build.sh includes this first in every source).
// Each is FreeBSD's definition.
#ifndef ND_DHCLIENT_COMPAT_H
#define ND_DHCLIENT_COMPAT_H
#include <errno.h>
#include <stdint.h>
#include <stdlib.h>
#include <unistd.h>

// <sys/param.h>
#ifndef nitems
#define nitems(x)	(sizeof((x)) / sizeof((x)[0]))
#endif

// <net/if_vlan_var.h>: an 802.1Q tag's VLAN ID and length (bpf.c's filter
// accepts priority-tagged frames, VLAN 0).
#ifndef EVL_VLID_MASK
#define EVL_VLID_MASK		0x0fff
#endif
#ifndef ETHER_VLAN_ENCAP_LEN
#define ETHER_VLAN_ENCAP_LEN	4
#endif

// <sys/time.h>'s timespec arithmetic (FreeBSD and NetBSD; Darwin has the
// timeval forms only).
#ifndef timespeccmp
#define timespeccmp(tsp, usp, cmp)					\
	(((tsp)->tv_sec == (usp)->tv_sec) ?				\
	    ((tsp)->tv_nsec cmp (usp)->tv_nsec) :			\
	    ((tsp)->tv_sec cmp (usp)->tv_sec))
#endif
#ifndef timespecadd
#define timespecadd(tsp, usp, vsp)					\
	do {								\
		(vsp)->tv_sec = (tsp)->tv_sec + (usp)->tv_sec;		\
		(vsp)->tv_nsec = (tsp)->tv_nsec + (usp)->tv_nsec;	\
		if ((vsp)->tv_nsec >= 1000000000L) {			\
			(vsp)->tv_sec++;				\
			(vsp)->tv_nsec -= 1000000000L;			\
		}							\
	} while (0)
#endif
#ifndef timespecsub
#define timespecsub(tsp, usp, vsp)					\
	do {								\
		(vsp)->tv_sec = (tsp)->tv_sec - (usp)->tv_sec;		\
		(vsp)->tv_nsec = (tsp)->tv_nsec - (usp)->tv_nsec;	\
		if ((vsp)->tv_nsec < 0) {				\
			(vsp)->tv_sec--;				\
			(vsp)->tv_nsec += 1000000000L;			\
		}							\
	} while (0)
#endif

// <poll.h>: an infinite poll(2) timeout (Darwin's defines it only
// outside POSIX mode).
#ifndef INFTIM
#define INFTIM	(-1)
#endif

// setproctitle(3): the process title isn't settable on Darwin.
#define setproctitle(...)	((void)0)

// daemonfd(3) (FreeBSD 12): daemon(3) with given descriptors for the
// working directory and the standard ones. dhclient passes -1 (don't
// chdir) and /dev/null; only go_daemon() calls it, which -d (NeoDarwin's
// launchd job, netconfigd) never reaches.
static inline int
daemonfd(int chdirfd, int nullfd)
{
	(void)nullfd;
	return daemon(chdirfd == -1, 0);
}

// reallocarray(3), with its overflow check.
static inline void *
nd_reallocarray(void *ptr, size_t nmemb, size_t size)
{
	if (size != 0 && nmemb > SIZE_MAX / size) {
		errno = ENOMEM;
		return NULL;
	}
	return realloc(ptr, nmemb * size);
}
#define reallocarray(p, n, s)	nd_reallocarray((p), (n), (s))
#endif
