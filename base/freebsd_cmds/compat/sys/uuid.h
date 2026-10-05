// SPDX-License-Identifier: BSD-2-Clause
// NeoDarwin-Language: portability: FreeBSD's DCE 1.1 struct uuid and uuidgen(2), which Darwin replaces with libuuid's byte-array uuid_t.
//
// FreeBSD's uuid_t is struct uuid, its fields in host byte order; Darwin's
// (<uuid/uuid.h>, <unistd.h>) is unsigned char[16]. A program that includes
// this header gets FreeBSD's, so it must come before anything that reaches
// Darwin's <sys/_types/_uuid_t.h>; that header is guarded by _UUID_T.
// uuidgen() is a library call here (nd_uuid.c, over uuid_generate_time(3)),
// not a system call.
#ifndef ND_SYS_UUID_H
#define ND_SYS_UUID_H
#include <sys/cdefs.h>
#include <errno.h>
#include <stdint.h>

#define _UUID_NODE_LEN	6

struct uuid {
	uint32_t	time_low;
	uint16_t	time_mid;
	uint16_t	time_hi_and_version;
	uint8_t		clock_seq_hi_and_reserved;
	uint8_t		clock_seq_low;
	uint8_t		node[_UUID_NODE_LEN];
};

#ifdef _UUID_T
#error "<sys/uuid.h> (FreeBSD's) after Darwin's uuid_t"
#endif
#define _UUID_T
typedef struct uuid uuid_t;

#define UUID_NODE_LEN	_UUID_NODE_LEN
#define UUIDGEN_BATCH_MAX	2048

__BEGIN_DECLS
int uuidgen(struct uuid *, int);
__END_DECLS
#endif
