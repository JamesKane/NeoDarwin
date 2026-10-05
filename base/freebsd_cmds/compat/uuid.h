// SPDX-License-Identifier: BSD-2-Clause
// NeoDarwin-Language: portability: FreeBSD's DCE 1.1 uuid(3) calls, which Darwin's Libc lacks (it has libuuid's).
//
// The subset of FreeBSD's lib/libc/uuid that its programs call, over
// FreeBSD's struct uuid (<sys/uuid.h>); nd_uuid.c implements them.
#ifndef ND_UUID_H
#define ND_UUID_H
#include <sys/uuid.h>

#define uuid_s_ok			0
#define uuid_s_bad_version		1
#define uuid_s_invalid_string_uuid	2
#define uuid_s_no_memory		3

__BEGIN_DECLS
void uuid_create(uuid_t *, uint32_t *);
void uuid_create_nil(uuid_t *, uint32_t *);
int32_t uuid_equal(const uuid_t *, const uuid_t *, uint32_t *);
int32_t uuid_is_nil(const uuid_t *, uint32_t *);
void uuid_to_string(const uuid_t *, char **, uint32_t *);
void uuid_from_string(const char *, uuid_t *, uint32_t *);
__END_DECLS
#endif
