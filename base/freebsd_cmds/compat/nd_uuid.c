// SPDX-License-Identifier: BSD-2-Clause
// NeoDarwin-Language: portability: FreeBSD's uuidgen(2) and uuid(3) over Darwin's uuid_generate_time(3).
//
// FreeBSD's kernel makes version 1 UUIDs (time and node) for uuidgen(2);
// Darwin's libuuid makes the same with uuid_generate_time(), as 16 bytes in
// network order, which become FreeBSD's struct uuid's host-order fields
// here. This file sees Darwin's uuid_t, so FreeBSD's is spelled struct uuid.
#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <uuid/uuid.h>

struct uuid {
	uint32_t	time_low;
	uint16_t	time_mid;
	uint16_t	time_hi_and_version;
	uint8_t		clock_seq_hi_and_reserved;
	uint8_t		clock_seq_low;
	uint8_t		node[6];
};

static void
nd_uuid_from_bytes(struct uuid *u, const unsigned char b[16])
{
	u->time_low = (uint32_t)b[0] << 24 | (uint32_t)b[1] << 16 | (uint32_t)b[2] << 8 | b[3];
	u->time_mid = (uint16_t)(b[4] << 8 | b[5]);
	u->time_hi_and_version = (uint16_t)(b[6] << 8 | b[7]);
	u->clock_seq_hi_and_reserved = b[8];
	u->clock_seq_low = b[9];
	memcpy(u->node, b + 10, 6);
}

int
uuidgen(struct uuid *store, int count)
{
	uuid_t b;

	if (count < 1 || count > 2048) {
		errno = EINVAL;
		return (-1);
	}
	for (int i = 0; i < count; i++) {
		uuid_generate_time(b);
		nd_uuid_from_bytes(&store[i], b);
	}
	return (0);
}

void
uuid_create(struct uuid *u, uint32_t *status)
{
	if (status != NULL)
		*status = 0;
	uuidgen(u, 1);
}

void
uuid_create_nil(struct uuid *u, uint32_t *status)
{
	if (status != NULL)
		*status = 0;
	memset(u, 0, sizeof(*u));
}

int32_t
uuid_is_nil(const struct uuid *u, uint32_t *status)
{
	static const struct uuid nil;

	if (status != NULL)
		*status = 0;
	return (u == NULL || memcmp(u, &nil, sizeof(nil)) == 0);
}

int32_t
uuid_equal(const struct uuid *a, const struct uuid *b, uint32_t *status)
{
	if (status != NULL)
		*status = 0;
	if (a == b)
		return (1);
	if (a == NULL)
		return (uuid_is_nil(b, NULL));
	if (b == NULL)
		return (uuid_is_nil(a, NULL));
	return (memcmp(a, b, sizeof(*a)) == 0);
}

// As FreeBSD's lib/libc/uuid/uuid_to_string.c: lower case, nil for NULL.
void
uuid_to_string(const struct uuid *u, char **s, uint32_t *status)
{
	static const struct uuid nil;

	if (status != NULL)
		*status = 0;
	if (s == NULL)
		return;
	if (u == NULL)
		u = &nil;
	asprintf(s, "%08x-%04x-%04x-%02x%02x-%02x%02x%02x%02x%02x%02x",
	    u->time_low, u->time_mid, u->time_hi_and_version,
	    u->clock_seq_hi_and_reserved, u->clock_seq_low, u->node[0],
	    u->node[1], u->node[2], u->node[3], u->node[4], u->node[5]);
	if (*s == NULL && status != NULL)
		*status = 3;	// uuid_s_no_memory
}

void
uuid_from_string(const char *s, struct uuid *u, uint32_t *status)
{
	uuid_t b;

	if (s == NULL || *s == '\0') {
		memset(u, 0, sizeof(*u));
		if (status != NULL)
			*status = 0;
		return;
	}
	if (uuid_parse(s, b) != 0) {
		if (status != NULL)
			*status = 2;	// uuid_s_invalid_string_uuid
		return;
	}
	nd_uuid_from_bytes(u, b);
	if (status != NULL)
		*status = 0;
}
