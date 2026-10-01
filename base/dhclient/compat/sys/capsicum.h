// SPDX-License-Identifier: BSD-2-Clause
// NeoDarwin-Language: expressibility: a compat header for FreeBSD's C dhclient, which includes it.
//
// FreeBSD's <sys/capsicum.h>, for dhclient (base/dhclient/build.sh). xnu has
// no Capsicum: a descriptor can't be limited and there is no capability
// mode. The rights calls succeed without doing anything, and cap_getmode()
// fails (ENOSYS, as on a FreeBSD kernel without CAPABILITY_MODE), so dhclient
// takes its own fallback: chroot to /var/empty and the unprivileged user.
#ifndef ND_DHCLIENT_SYS_CAPSICUM_H
#define ND_DHCLIENT_SYS_CAPSICUM_H
#include <errno.h>
#include <sys/types.h>

typedef struct { int nd_unused; } cap_rights_t;

#define CAP_READ	0
#define CAP_WRITE	0
#define CAP_EVENT	0
#define CAP_IOCTL	0
#define CAP_FCNTL	0
#define CAP_FSTAT	0
#define CAP_FSYNC	0
#define CAP_FTRUNCATE	0
#define CAP_SEEK	0
#define CAP_FCNTL_GETFL	0

#define cap_rights_init(rights, ...)	((void)(rights))

static inline int
cap_getmode(u_int *modep)
{
	(void)modep;
	errno = ENOSYS;
	return -1;
}
#endif
