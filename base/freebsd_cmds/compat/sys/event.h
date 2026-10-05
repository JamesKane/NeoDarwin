// SPDX-License-Identifier: BSD-2-Clause
// NeoDarwin-Language: portability: FreeBSD's kqueuex(2), which xnu lacks, over kqueue(2) and fcntl(2).
#ifndef ND_SYS_EVENT_H
#define ND_SYS_EVENT_H
#include_next <sys/event.h>
#include <fcntl.h>
#include <unistd.h>

#ifndef KQUEUE_CLOEXEC
#define KQUEUE_CLOEXEC 0x00000001
static __inline int
kqueuex(unsigned int flags)
{
	int kq = kqueue();

	if (kq != -1 && (flags & KQUEUE_CLOEXEC) != 0 && fcntl(kq, F_SETFD, FD_CLOEXEC) == -1) {
		close(kq);
		return (-1);
	}
	return (kq);
}
#endif
#endif
