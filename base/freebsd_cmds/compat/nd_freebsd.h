// SPDX-License-Identifier: BSD-2-Clause
// NeoDarwin-Language: portability: FreeBSD <sys/cdefs.h> and libc names that FreeBSD's programs use and Darwin's headers lack.
//
// Force-included ahead of every program base/freebsd_cmds builds
// (build.sh's -include), so the FreeBSD sources compile unchanged. Each
// name is defined only where Darwin's headers don't define it.
#ifndef ND_FREEBSD_H
#define ND_FREEBSD_H
#include <sys/cdefs.h>
#include <sys/types.h>
#include <stddef.h>
#include <stdint.h>
// FreeBSD's <sys/stat.h> and <sys/param.h> reach <sys/time.h> (struct
// itimerval, TIMESPEC_TO_TIMEVAL, utimes()); Darwin's don't.
#include <sys/time.h>

// <sys/_types.h>
#ifndef __uintptr_t
#define __uintptr_t uintptr_t
#endif
// FreeBSD's <sys/_types.h> sbintime_t, for <sys/_callout.h> (makefs).
typedef int64_t __sbintime_t;
// FreeBSD's <sys/param.h> power-of-two rounding.
#ifndef roundup2
#define roundup2(x, y) (((x) + ((y) - 1)) & (~((__typeof(x))(y) - 1)))
#endif
#ifndef rounddown2
#define rounddown2(x, y) ((x) & (~((__typeof(x))(y) - 1)))
#endif

// <sys/cdefs.h>
#ifndef __unreachable
#define __unreachable() __builtin_unreachable()
#endif
#ifndef __nonstring
#define __nonstring __attribute__((__nonstring__))
#endif
#ifndef __min_size
#define __min_size(x) static (x)
#endif
#ifndef __DECONST
#define __DECONST(type, var) ((type)(uintptr_t)(const void *)(var))
#endif
#ifndef __DEVOLATILE
#define __DEVOLATILE(type, var) ((type)(uintptr_t)(volatile void *)(var))
#endif
#ifndef __DEQUALIFY
#define __DEQUALIFY(type, var) ((type)(uintptr_t)(const volatile void *)(var))
#endif
#ifndef __predict_true
#define __predict_true(exp) __builtin_expect((exp), 1)
#define __predict_false(exp) __builtin_expect((exp), 0)
#endif
#ifndef __printf0like
#define __printf0like(fmtarg, firstvararg)
#endif
#ifndef __result_use_check
#define __result_use_check __attribute__((__warn_unused_result__))
#endif
#ifndef __alloc_size
#define __alloc_size(x) __attribute__((__alloc_size__(x)))
#endif
#ifndef __alloc_size2
#define __alloc_size2(n, x) __attribute__((__alloc_size__(n, x)))
#endif
#ifndef __malloc_like
#define __malloc_like __attribute__((__malloc__))
#endif
#ifndef __exported
#define __exported __attribute__((__visibility__("default")))
#endif
#ifndef __hidden
#define __hidden __attribute__((__visibility__("hidden")))
#endif
#ifndef __noinline
#define __noinline __attribute__((__noinline__))
#endif
#ifndef __always_inline
#define __always_inline __attribute__((__always_inline__))
#endif
#ifndef __nodiscard
#define __nodiscard __attribute__((__warn_unused_result__))
#endif
#ifndef __sentinel
#define __sentinel __attribute__((__sentinel__))
#endif
#ifndef __containerof
#define __containerof(x, s, m) ((s *)(void *)((char *)(x) - offsetof(s, m)))
#endif

#ifndef __packed
#define __packed __attribute__((__packed__))
#endif
#ifndef __va_list
#define __va_list __darwin_va_list
#endif

// <sys/param.h>
#ifndef nitems
#define nitems(x) (sizeof((x)) / sizeof((x)[0]))
#endif

// <sys/time.h>
#ifndef timespecadd
#define timespecadd(tsp, usp, vsp) do { \
	(vsp)->tv_sec = (tsp)->tv_sec + (usp)->tv_sec; \
	(vsp)->tv_nsec = (tsp)->tv_nsec + (usp)->tv_nsec; \
	if ((vsp)->tv_nsec >= 1000000000L) { (vsp)->tv_sec++; (vsp)->tv_nsec -= 1000000000L; } \
} while (0)
#define timespecsub(tsp, usp, vsp) do { \
	(vsp)->tv_sec = (tsp)->tv_sec - (usp)->tv_sec; \
	(vsp)->tv_nsec = (tsp)->tv_nsec - (usp)->tv_nsec; \
	if ((vsp)->tv_nsec < 0) { (vsp)->tv_sec--; (vsp)->tv_nsec += 1000000000L; } \
} while (0)
#endif
#ifndef timespeccmp
#define timespeccmp(tvp, uvp, cmp) \
	(((tvp)->tv_sec == (uvp)->tv_sec) ? ((tvp)->tv_nsec cmp (uvp)->tv_nsec) : ((tvp)->tv_sec cmp (uvp)->tv_sec))
#endif

__BEGIN_DECLS
// <stdlib.h>: FreeBSD's libc has it (OpenBSD's), Darwin's doesn't;
// nd_freebsd.c.
void *reallocarray(void *, size_t, size_t) __result_use_check __alloc_size2(2, 3);
void setproctitle(const char *, ...) __printf0like(1, 2);
// <string.h>
void *memrchr(const void *, int, size_t);
// <unistd.h>: pipe2(2), which xnu doesn't have (O_CLOEXEC and O_NONBLOCK
// only, set after pipe(2): not atomic against a concurrent fork).
int pipe2(int [2], int);
__END_DECLS
#endif
