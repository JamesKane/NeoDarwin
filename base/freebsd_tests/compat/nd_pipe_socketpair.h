/* SPDX-License-Identifier: BSD-2-Clause */
// NeoDarwin-Language: portability: FreeBSD tests rely on bidirectional pipes, which Darwin's pipe(2) doesn't give.
/* pipe(2) as a socketpair, for FreeBSD tests that use both ends of a pipe
 * (FreeBSD's pipes are bidirectional, Darwin's aren't): sbin/pfctl's
 * pfctl_test reads its child's output from the end it didn't hand over. */
#ifndef ND_PIPE_SOCKETPAIR_H
#define ND_PIPE_SOCKETPAIR_H
#include <sys/socket.h>
#include <unistd.h>
static inline int nd_pipe_socketpair(int fds[2]) { return socketpair(AF_UNIX, SOCK_STREAM, 0, fds); }
#define pipe(fds) nd_pipe_socketpair(fds)
#endif
