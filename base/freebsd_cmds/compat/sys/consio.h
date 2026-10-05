// SPDX-License-Identifier: BSD-2-Clause
// NeoDarwin-Language: portability: FreeBSD's virtual-terminal ioctl numbers; xnu has no vt(4), so a terminal refuses them.
//
// FreeBSD's sys/sys/consio.h defines the vt(4) and syscons ioctls. NeoDarwin
// has no virtual terminals: the numbers are FreeBSD's, and every terminal
// answers them with ENOTTY, as a FreeBSD pty does (lock -v fails "locking
// vty").
#ifndef ND_SYS_CONSIO_H
#define ND_SYS_CONSIO_H
#include <sys/ioccom.h>
// FreeBSD's consio.h reaches ioctl(2)'s declaration.
#include <sys/ioctl.h>
#define VT_LOCKSWITCH	_IOW('v', 8, int)
#define VT_ACTIVATE	_IO('v', 5)
#define VT_GETACTIVE	_IOR('v', 7, int)
#endif
