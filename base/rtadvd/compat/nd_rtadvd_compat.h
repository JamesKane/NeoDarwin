// SPDX-License-Identifier: BSD-2-Clause
// NeoDarwin-Language: expressibility: a compat header for FreeBSD's C rtadvd, included before its sources.
//
// FreeBSD libc and <sys/param.h> interfaces rtadvd uses that Darwin's don't
// have (base/rtadvd/build.sh includes this first in every source). Each is
// FreeBSD's definition, or the nearest Darwin has.
#ifndef ND_RTADVD_COMPAT_H
#define ND_RTADVD_COMPAT_H
#include <stddef.h>
#include <time.h>

// <sys/param.h>
#ifndef nitems
#define nitems(x)	(sizeof((x)) / sizeof((x)[0]))
#endif

// <time.h>: FreeBSD's coarse monotonic clock. rtadvd keeps its timers on
// it; Darwin's CLOCK_MONOTONIC is the same clock at full precision.
#ifndef CLOCK_MONOTONIC_FAST
#define CLOCK_MONOTONIC_FAST	CLOCK_MONOTONIC
#endif
// rtadvctl's timestamps: likewise CLOCK_REALTIME.
#ifndef CLOCK_REALTIME_FAST
#define CLOCK_REALTIME_FAST	CLOCK_REALTIME
#endif

// <poll.h>: an infinite poll(2) timeout (Darwin's defines it only outside
// POSIX mode).
#ifndef INFTIM
#define INFTIM	(-1)
#endif

#endif
