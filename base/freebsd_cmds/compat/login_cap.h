// SPDX-License-Identifier: BSD-2-Clause
// NeoDarwin-Language: portability: FreeBSD's setusercontext(3) for a system without login classes.
//
// NeoDarwin has no login.conf(5) classes (Open Directory replaces them on
// macOS), so setusercontext() does what FreeBSD's does for a user whose
// class sets nothing: group, groups, login name and user (nd_freebsd.c).
// The resource, priority, umask, path and environment flags change nothing.
#ifndef ND_LOGIN_CAP_H
#define ND_LOGIN_CAP_H
#include <sys/cdefs.h>
#include <sys/types.h>
#include <pwd.h>

#define LOGIN_SETGROUP		0x0001
#define LOGIN_SETLOGIN		0x0002
#define LOGIN_SETPATH		0x0004
#define LOGIN_SETPRIORITY	0x0008
#define LOGIN_SETRESOURCES	0x0010
#define LOGIN_SETUMASK		0x0020
#define LOGIN_SETUSER		0x0040
#define LOGIN_SETENV		0x0080
#define LOGIN_SETMAC		0x0100
#define LOGIN_SETCPUMASK	0x0200
#define LOGIN_SETLOGINCLASS	0x0400
#define LOGIN_SETALL		0x07ff

typedef struct login_cap login_cap_t;

__BEGIN_DECLS
int setusercontext(login_cap_t *, const struct passwd *, uid_t, unsigned int);
__END_DECLS
#endif
