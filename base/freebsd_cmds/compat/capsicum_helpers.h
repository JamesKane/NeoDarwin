// SPDX-License-Identifier: BSD-2-Clause
// NeoDarwin-Language: portability: FreeBSD's libcapsicum helpers as the no-ops a WITHOUT_CAPSICUM build compiles to.
//
// FreeBSD's lib/libcapsicum/capsicum_helpers.h for a kernel without
// capability mode (xnu; docs/architecture/freebsd-parity.md §5): nothing is
// limited and entering capability mode succeeds without effect, as on a
// FreeBSD kernel built without CAPABILITY_MODE.
#ifndef ND_CAPSICUM_HELPERS_H
#define ND_CAPSICUM_HELPERS_H
#include <sys/capsicum.h>

#define CAPH_IGNORE_EBADF	0x0001
#define CAPH_READ		0x0002
#define CAPH_WRITE		0x0004
#define CAPH_LOOKUP		0x0008

static inline int caph_limit_stream(int fd __unused, int flags __unused) { return (0); }
static inline int caph_limit_stdin(void) { return (0); }
static inline int caph_limit_stderr(void) { return (0); }
static inline int caph_limit_stdout(void) { return (0); }
static inline int caph_limit_stdio(void) { return (0); }
static inline void caph_cache_tzdata(void) { }
static inline void caph_cache_catpages(void) { }
static inline int caph_enter(void) { return (0); }
static inline int caph_enter_casper(void) { return (0); }
static inline int caph_rights_limit(int fd __unused, const cap_rights_t *r __unused) { return (0); }
static inline int caph_ioctls_limit(int fd __unused, const unsigned long *c __unused, size_t n __unused) { return (0); }
static inline int caph_fcntls_limit(int fd __unused, uint32_t f __unused) { return (0); }
#endif
