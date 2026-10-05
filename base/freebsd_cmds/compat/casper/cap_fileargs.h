// SPDX-License-Identifier: BSD-2-Clause
// NeoDarwin-Language: portability: FreeBSD's cap_fileargs as its WITHOUT_CASPER inline fallbacks.
//
// FreeBSD's lib/libcasper/services/cap_fileargs/cap_fileargs.h without
// WITH_CASPER: the files are opened directly (libcasper.h).
#ifndef ND_CAP_FILEARGS_H
#define ND_CAP_FILEARGS_H
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <libcasper.h>
#include <sys/capsicum.h>

#define FA_OPEN		1
#define FA_LSTAT	2
#define FA_REALPATH	4

typedef struct fileargs {
	int	fa_flags;
	mode_t	fa_mode;
} fileargs_t;

static inline fileargs_t *
fileargs_init(int argc __unused, char *argv[] __unused, int flags, mode_t mode,
    cap_rights_t *rightsp __unused, int operations __unused)
{
	fileargs_t *fa = (fileargs_t *)malloc(sizeof(*fa));

	if (fa != NULL) {
		fa->fa_flags = flags;
		fa->fa_mode = mode;
	}
	return (fa);
}
static inline fileargs_t *
fileargs_cinit(cap_channel_t *cas __unused, int argc, char *argv[], int flags,
    mode_t mode, cap_rights_t *rightsp, int operations)
{
	return (fileargs_init(argc, argv, flags, mode, rightsp, operations));
}
#define fileargs_lstat(fa, name, sb)	lstat(name, sb)
#define fileargs_open(fa, name)		open(name, (fa)->fa_flags, (fa)->fa_mode)
#define fileargs_realpath(fa, p, r)	realpath(p, r)
static inline FILE *
fileargs_fopen(fileargs_t *fa __unused, const char *name, const char *mode)
{
	return (fopen(name, mode));
}
#define fileargs_free(fa)		(free(fa))
#endif
