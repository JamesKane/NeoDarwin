// SPDX-License-Identifier: BSD-2-Clause
// NeoDarwin-Language: portability: FreeBSD's Capsicum calls as the no-ops a WITHOUT_CAPSICUM build compiles to.
//
// xnu has no capability mode (docs/architecture/freebsd-parity.md §5), so
// FreeBSD's programs run as FreeBSD's own WITHOUT_CAPSICUM builds do: every
// cap_* call succeeds and limits nothing. The rights are kept in the
// structure only so that code that builds and passes them compiles.
#ifndef ND_SYS_CAPSICUM_H
#define ND_SYS_CAPSICUM_H
#include <sys/cdefs.h>
#include <sys/types.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h>

typedef struct cap_rights { uint64_t cr_rights[2]; } cap_rights_t;

#define CAP_READ	0x0000000000000001ULL
#define CAP_WRITE	0x0000000000000002ULL
#define CAP_SEEK	0x0000000000000004ULL
#define CAP_MMAP	0x0000000000000008ULL
#define CAP_MMAP_R	(CAP_MMAP | CAP_SEEK | CAP_READ)
#define CAP_MMAP_W	(CAP_MMAP | CAP_SEEK | CAP_WRITE)
#define CAP_CREATE	0x0000000000000040ULL
#define CAP_FCNTL	0x0000000000000080ULL
#define CAP_FSTAT	0x0000000000000100ULL
#define CAP_FSYNC	0x0000000000000200ULL
#define CAP_FTRUNCATE	0x0000000000000400ULL
#define CAP_LOOKUP	0x0000000000000800ULL
#define CAP_IOCTL	0x0000000000001000ULL
#define CAP_EVENT	0x0000000000002000ULL
#define CAP_FSTATAT	(CAP_FSTAT | CAP_LOOKUP)
#define CAP_UNLINKAT	0x0000000000004000ULL
#define CAP_FCHMOD	0x0000000000008000ULL
#define CAP_FUTIMES	0x0000000000010000ULL
#define CAP_PREAD	(CAP_SEEK | CAP_READ)
#define CAP_PWRITE	(CAP_SEEK | CAP_WRITE)
#define CAP_ACCEPT	0x0000000000020000ULL
#define CAP_CONNECT	0x0000000000040000ULL
#define CAP_GETPEERNAME	0x0000000000080000ULL
#define CAP_GETSOCKNAME	0x0000000000100000ULL
#define CAP_SETSOCKOPT	0x0000000000200000ULL
#define CAP_GETSOCKOPT	0x0000000000400000ULL
#define CAP_RECV	CAP_READ
#define CAP_SEND	CAP_WRITE
#define CAP_FCNTL_GETFL	0x0001
#define CAP_FCNTL_SETFL	0x0002

static inline cap_rights_t *
nd_cap_rights_set(cap_rights_t *r, int n, ...)
{
	va_list ap;
	va_start(ap, n);
	for (int i = 0; i < n; i++)
		r->cr_rights[0] |= va_arg(ap, unsigned long long);
	va_end(ap);
	return (r);
}
#define ND_CAP_NARGS(...) (sizeof((unsigned long long[]){ __VA_ARGS__ }) / sizeof(unsigned long long))
#define cap_rights_init(r, ...) \
	((r)->cr_rights[0] = (r)->cr_rights[1] = 0, nd_cap_rights_set((r), (int)ND_CAP_NARGS(0ULL, ##__VA_ARGS__), 0ULL, ##__VA_ARGS__))
#define cap_rights_set(r, ...) nd_cap_rights_set((r), (int)ND_CAP_NARGS(0ULL, ##__VA_ARGS__), 0ULL, ##__VA_ARGS__)
#define cap_rights_clear(r, ...) (r)
static inline bool cap_rights_is_set(const cap_rights_t *r __unused, ...) { return (true); }

static inline int cap_enter(void) { return (0); }
static inline int cap_getmode(unsigned int *modep) { *modep = 0; return (0); }
static inline bool cap_sandboxed(void) { return (false); }
static inline int cap_rights_limit(int fd __unused, const cap_rights_t *r __unused) { return (0); }
static inline int cap_ioctls_limit(int fd __unused, const unsigned long *c __unused, size_t n __unused) { return (0); }
static inline int cap_fcntls_limit(int fd __unused, uint32_t f __unused) { return (0); }
#endif
