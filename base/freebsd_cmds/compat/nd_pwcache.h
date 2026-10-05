// SPDX-License-Identifier: BSD-2-Clause
// NeoDarwin-Language: portability: FreeBSD's pwcache(3) next to Darwin's older one, whose user_from_uid() returns char *.
//
// Force-included by the programs that compile FreeBSD's
// contrib/libc-pwcache/pwcache.c (nmtree): uid_from_user(),
// gid_from_group(), pwcache_userdb() and pwcache_groupdb() aren't in
// Darwin's Libc, and FreeBSD's user_from_uid() and group_from_gid() must
// replace Darwin's for pwcache_userdb() to take effect. Darwin's <pwd.h>
// and <grp.h> come first, so their prototypes keep Darwin's names; FreeBSD's
// functions are nd_user_from_uid() and nd_group_from_gid() (nd_pwcache.c).
#ifndef ND_PWCACHE_H
#define ND_PWCACHE_H
#include <sys/types.h>
#include <grp.h>
#include <pwd.h>

#define user_from_uid nd_user_from_uid
#define group_from_gid nd_group_from_gid

__BEGIN_DECLS
const char *user_from_uid(uid_t, int);
const char *group_from_gid(gid_t, int);
int uid_from_user(const char *, uid_t *);
int gid_from_group(const char *, gid_t *);
int pwcache_userdb(int (*)(int), void (*)(void), struct passwd *(*)(const char *), struct passwd *(*)(uid_t));
int pwcache_groupdb(int (*)(int), void (*)(void), struct group *(*)(const char *), struct group *(*)(gid_t));
__END_DECLS
#endif
