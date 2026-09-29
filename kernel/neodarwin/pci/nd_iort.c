/* SPDX-License-Identifier: BSD-2-Clause */
/* NeoDarwin-Language: portability: plain C over ACPI table bytes, built into the kernel and on the host for its tests. */
/*
 * MADT GIC ITS structures and IORT ID mappings (nd_iort.h). Every read is
 * bounds-checked against the table's length and done bytewise: firmware
 * tables are packed, so fields are not aligned.
 */

#include "nd_iort.h"

#define ACPI_HEADER_SIZE      36
#define MADT_FIRST_ENTRY      44       /* header, local interrupt controller address, flags */
#define MADT_GIC_ITS          0x0f
#define MADT_GIC_ITS_LENGTH   20

#define IORT_NODE_ITS_GROUP   0
#define IORT_NODE_ROOT_COMPLEX 2
#define IORT_NODE_SMMU        3
#define IORT_NODE_SMMUV3      4
#define IORT_NODE_HEADER      16       /* type, length, revision, identifier, mapping count, mapping offset */
#define IORT_ID_MAPPING       20
#define IORT_SINGLE_MAPPING   0x1

static uint32_t
le32(const uint8_t *p)
{
	return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}

static uint64_t
le64(const uint8_t *p)
{
	return (uint64_t)le32(p) | (uint64_t)le32(p + 4) << 32;
}

static uint16_t
le16(const uint8_t *p)
{
	return (uint16_t)(p[0] | p[1] << 8);
}

unsigned int
nd_madt_its(const uint8_t *madt, size_t length, struct nd_madt_its *out, unsigned int max)
{
	unsigned int found = 0;
	if (madt == NULL || length < MADT_FIRST_ENTRY) {
		return 0;
	}
	uint32_t total = le32(madt + 4);
	if (total < length) {
		length = total;
	}
	for (size_t off = MADT_FIRST_ENTRY; off + 2 <= length;) {
		uint8_t type = madt[off], len = madt[off + 1];
		if (len < 2 || off + len > length) {
			break;
		}
		if (type == MADT_GIC_ITS && len >= MADT_GIC_ITS_LENGTH) {
			if (found < max && out != NULL) {
				out[found].id = le32(madt + off + 4);
				out[found].base = le64(madt + off + 8);
			}
			found++;
		}
		off += len;
	}
	return found;
}

/* A node at `off`, with its header in bounds; its length, or 0. */
static uint32_t
node_length(const uint8_t *iort, size_t length, uint32_t off)
{
	if (off < ACPI_HEADER_SIZE || (size_t)off + IORT_NODE_HEADER > length) {
		return 0;
	}
	uint32_t len = le16(iort + off + 1);
	if (len < IORT_NODE_HEADER || (size_t)off + len > length) {
		return 0;
	}
	return len;
}

/* The ID mapping of the node at `off` covering `id` (not a single mapping):
 * the output ID and the output node's offset. */
static enum nd_iort_status
map_id(const uint8_t *iort, size_t length, uint32_t off, uint32_t id, uint32_t *out_id, uint32_t *out_node)
{
	uint32_t len = node_length(iort, length, off);
	if (len == 0) {
		return ND_IORT_BAD_TABLE;
	}
	const uint8_t *node = iort + off;
	uint32_t count = le32(node + 8), ref = le32(node + 12);
	if (count == 0) {
		return ND_IORT_NO_MAPPING;
	}
	if (ref < IORT_NODE_HEADER || ref > len || (len - ref) / IORT_ID_MAPPING < count) {
		return ND_IORT_BAD_TABLE;
	}
	for (uint32_t i = 0; i < count; i++) {
		const uint8_t *m = node + ref + i * IORT_ID_MAPPING;
		uint32_t in = le32(m), n = le32(m + 4), outBase = le32(m + 8), outRef = le32(m + 12), flags = le32(m + 16);
		if (flags & IORT_SINGLE_MAPPING) {
			continue;
		}
		/* "Number of IDs" is the count minus one. */
		if (id >= in && id - in <= n) {
			*out_id = outBase + (id - in);
			*out_node = outRef;
			return ND_IORT_OK;
		}
	}
	return ND_IORT_NO_MAPPING;
}

enum nd_iort_status
nd_iort_msi_route(const uint8_t *iort, size_t length, uint32_t segment, uint32_t rid, struct nd_iort_route *route)
{
	if (iort == NULL || route == NULL || length < ACPI_HEADER_SIZE + 12) {
		return ND_IORT_BAD_TABLE;
	}
	uint32_t total = le32(iort + 4);
	if (total < length) {
		length = total;
	}
	*route = (struct nd_iort_route){ 0 };
	uint32_t nodes = le32(iort + 36), off = le32(iort + 40), rc = 0;
	for (uint32_t i = 0; i < nodes; i++) {
		uint32_t len = node_length(iort, length, off);
		if (len == 0) {
			return ND_IORT_BAD_TABLE;
		}
		/* Root complex: PCI segment number at 28. */
		if (iort[off] == IORT_NODE_ROOT_COMPLEX && len >= 32 && le32(iort + off + 28) == segment) {
			rc = off;
			break;
		}
		off += len;
	}
	if (rc == 0) {
		return ND_IORT_NO_ROOT_COMPLEX;
	}
	uint32_t id = rid, node = rc;
	for (unsigned int hop = 0;; hop++) {
		uint32_t next;
		enum nd_iort_status s = map_id(iort, length, node, id, &id, &next);
		if (s != ND_IORT_OK) {
			return s;
		}
		uint32_t len = node_length(iort, length, next);
		if (len == 0) {
			return ND_IORT_BAD_TABLE;
		}
		uint8_t type = iort[next];
		if (type == IORT_NODE_ITS_GROUP) {
			if (len < 24) {
				return ND_IORT_BAD_TABLE;
			}
			route->device_id = id;
			route->its_count = le32(iort + next + 16);
			route->its_id = le32(iort + next + 20);
			return ND_IORT_OK;
		}
		if (type != IORT_NODE_SMMU && type != IORT_NODE_SMMUV3) {
			return ND_IORT_NOT_ITS;
		}
		if (hop >= ND_IORT_MAX_HOPS || len < 24) {
			return hop >= ND_IORT_MAX_HOPS ? ND_IORT_TOO_DEEP : ND_IORT_BAD_TABLE;
		}
		route->smmu[route->smmu_count].type = type;
		route->smmu[route->smmu_count].base = le64(iort + next + 16);
		route->smmu[route->smmu_count].stream_id = id;
		route->smmu_count++;
		node = next;
	}
}

const char *
nd_iort_status_string(enum nd_iort_status status)
{
	switch (status) {
	case ND_IORT_OK: return "ok";
	case ND_IORT_BAD_TABLE: return "malformed IORT";
	case ND_IORT_NO_ROOT_COMPLEX: return "no IORT root complex node for the segment";
	case ND_IORT_NO_MAPPING: return "no IORT ID mapping covers the requester ID";
	case ND_IORT_NOT_ITS: return "the IORT sends the requester ID somewhere other than an ITS";
	case ND_IORT_TOO_DEEP: return "too many SMMUs between the root complex and the ITS";
	}
	return "unknown";
}
