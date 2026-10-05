// SPDX-License-Identifier: BSD-2-Clause
// NeoDarwin-Language: portability: FreeBSD libc functions that Darwin's Libc lacks, for base/freebsd_cmds' programs.
//
// Linked into every program base/freebsd_cmds builds; ld's -dead_strip
// drops what a program doesn't call. Declared in nd_freebsd.h.
#include <errno.h>
#include <stdint.h>
#include <stdlib.h>

// reallocarray(3), as FreeBSD's lib/libc/stdlib/reallocarray.c (OpenBSD's):
// realloc(ptr, nmemb * size), failing with ENOMEM if the product overflows.
void *
reallocarray(void *optr, size_t nmemb, size_t size)
{
	size_t n;

	if (__builtin_mul_overflow(nmemb, size, &n)) {
		errno = ENOMEM;
		return (NULL);
	}
	return (realloc(optr, n));
}

// cpuset_getaffinity(2) (<sys/cpuset.h>): every active CPU, since xnu has
// no affinity masks for user threads.
#include <sys/types.h>
#include <sys/sysctl.h>
#include <string.h>
struct _cpuset { uint64_t __bits[1024 / 64]; };

int
cpuset_getaffinity(int level, int which, id_t id, size_t size, struct _cpuset *mask)
{
	int n;
	size_t len = sizeof(n);

	(void)level; (void)which; (void)id;
	if (size < sizeof(*mask)) {
		errno = ERANGE;
		return (-1);
	}
	if (sysctlbyname("hw.activecpu", &n, &len, NULL, 0) != 0)
		return (-1);
	memset(mask, 0, size);
	for (int i = 0; i < n && i < 1024; i++)
		mask->__bits[i / 64] |= 1ULL << (i % 64);
	return (0);
}

int
cpuset_setaffinity(int level, int which, id_t id, size_t size, const struct _cpuset *mask)
{
	(void)level; (void)which; (void)id; (void)size; (void)mask;
	errno = ENOTSUP;
	return (-1);
}

// setusercontext(3) (<login_cap.h>): no login classes, so a class's
// settings are empty and only the identity changes, in FreeBSD's order.
#include <grp.h>
#include <pwd.h>
#include <unistd.h>
#define ND_LOGIN_SETGROUP	0x0001
#define ND_LOGIN_SETLOGIN	0x0002
#define ND_LOGIN_SETUSER	0x0040
struct login_cap;

int
setusercontext(struct login_cap *lc, const struct passwd *pwd, uid_t uid, unsigned int flags)
{
	(void)lc;
	if (pwd == NULL)
		flags &= ~(ND_LOGIN_SETGROUP | ND_LOGIN_SETLOGIN);
	if (flags & ND_LOGIN_SETGROUP) {
		if (setgid(pwd->pw_gid) != 0)
			return (-1);
		if (initgroups(pwd->pw_name, (int)pwd->pw_gid) == -1)
			return (-1);
	}
	if ((flags & ND_LOGIN_SETLOGIN) && setlogin(pwd->pw_name) != 0)
		return (-1);
	if ((flags & ND_LOGIN_SETUSER) && setuid(uid) != 0)
		return (-1);
	return (0);
}

// setproctitle(3): Darwin has no way to set the title ps(1) shows short of
// rewriting the argument area, so the process keeps its command line.
void
setproctitle(const char *fmt, ...)
{
	(void)fmt;
}

// memrchr(3), as FreeBSD's lib/libc/string/memrchr.c.
void *
memrchr(const void *s, int c, size_t n)
{
	const unsigned char *cp;

	if (n != 0) {
		cp = (const unsigned char *)s + n;
		do {
			if (*(--cp) == (unsigned char)c)
				return ((void *)(uintptr_t)cp);
		} while (--n != 0);
	}
	return (NULL);
}
