// SPDX-License-Identifier: BSD-2-Clause
// NeoDarwin-Language: portability: FreeBSD's cpuset(2) affinity query, which xnu lacks.
//
// xnu has no CPU affinity masks for user threads (only affinity tags, a
// hint), so every thread may run on every active CPU: cpuset_getaffinity()
// answers the CPUs 0 to hw.activecpu - 1 (nd_freebsd.c), which is what
// FreeBSD answers for a thread in the root set. Setting affinity fails
// with ENOTSUP.
#ifndef ND_SYS_CPUSET_H
#define ND_SYS_CPUSET_H
#include <sys/cdefs.h>
#include <sys/types.h>
#include <stdint.h>
#include <string.h>

#define CPU_SETSIZE	1024
typedef struct _cpuset { uint64_t __bits[CPU_SETSIZE / 64]; } cpuset_t;
typedef int cpulevel_t;
typedef int cpuwhich_t;
typedef int cpusetid_t;

#define CPU_ZERO(p)	memset((p), 0, sizeof(cpuset_t))
#define CPU_SET(n, p)	((p)->__bits[(n) / 64] |= 1ULL << ((n) % 64))
#define CPU_CLR(n, p)	((p)->__bits[(n) / 64] &= ~(1ULL << ((n) % 64)))
#define CPU_ISSET(n, p)	(((p)->__bits[(n) / 64] & (1ULL << ((n) % 64))) != 0)
static inline int
nd_cpu_count(const cpuset_t *p)
{
	int n = 0;
	for (size_t i = 0; i < CPU_SETSIZE / 64; i++)
		n += __builtin_popcountll(p->__bits[i]);
	return (n);
}
#define CPU_COUNT(p)	nd_cpu_count(p)

#define CPU_LEVEL_ROOT		1
#define CPU_LEVEL_CPUSET	2
#define CPU_LEVEL_WHICH		3
#define CPU_WHICH_TID		1
#define CPU_WHICH_PID		2
#define CPU_WHICH_CPUSET	3

__BEGIN_DECLS
int cpuset_getaffinity(cpulevel_t, cpuwhich_t, id_t, size_t, cpuset_t *);
int cpuset_setaffinity(cpulevel_t, cpuwhich_t, id_t, size_t, const cpuset_t *);
__END_DECLS
#endif
