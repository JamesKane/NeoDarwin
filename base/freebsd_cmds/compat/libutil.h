// SPDX-License-Identifier: BSD-2-Clause
// NeoDarwin-Language: portability: FreeBSD <libutil.h> calls that macOS's libutil lacks.
//
// macOS's libutil (base/libutil, Apple's libutil) is an older FreeBSD
// libutil. This adds what FreeBSD's programs use from the newer one:
// fparseln(3), compiled from FreeBSD's lib/libutil/fparseln.c into the
// program that calls it (build.sh), and pidfile_fileno(3), which needs
// only the pidfh structure Apple's header declares.
#ifndef ND_LIBUTIL_H
#define ND_LIBUTIL_H
#include <sys/param.h>
#include <errno.h>
#include <stdio.h>
#include_next <libutil.h>

#define FPARSELN_UNESCESC	0x01
#define FPARSELN_UNESCCONT	0x02
#define FPARSELN_UNESCCOMM	0x04
#define FPARSELN_UNESCREST	0x08
#define FPARSELN_UNESCALL	0x0f

__BEGIN_DECLS
char *fparseln(FILE *, size_t *, size_t *, const char[3], int);
__END_DECLS

static __inline int
pidfile_fileno(const struct pidfh *pfh)
{
	if (pfh == NULL || pfh->pf_fd == -1) {
		errno = EINVAL;
		return (-1);
	}
	return (pfh->pf_fd);
}
#endif
