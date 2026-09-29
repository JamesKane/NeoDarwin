/* SPDX-License-Identifier: BSD-2-Clause */
/* NeoDarwin-Language: portability: plain C over ACPI table bytes, built into the kernel and on the host for its tests. */
/*
 * The two ACPI tables PCI MSIs need (docs/kernel/gic-its.md):
 *  - the MADT's GIC ITS structures (ACPI 6.5 §5.2.12.18, type 0xF): where
 *    each ITS is and its translation ID;
 *  - the IORT (Arm DEN 0049): how a PCI requester ID becomes an ITS
 *    DeviceID. A root complex node's ID mappings send requester IDs either
 *    straight to an ITS group, or to an SMMU, whose own mappings send its
 *    stream IDs on to an ITS group. The result is the ID at the ITS and
 *    which ITS, plus each SMMU crossed on the way (NeoDarwin programs no
 *    SMMU; the log names them).
 * Both take a whole table, header included, as the firmware laid it out.
 */

#ifndef _ND_IORT_H
#define _ND_IORT_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

struct nd_madt_its {
	uint32_t id;            /* GIC ITS ID: the IORT's ITS group names it */
	uint64_t base;          /* physical address of ITS_base */
};

/* The MADT's GIC ITS structures, in table order: stores up to `max` and
 * returns how many the table has. */
unsigned int nd_madt_its(const uint8_t *madt, size_t length, struct nd_madt_its *out, unsigned int max);

#define ND_IORT_MAX_HOPS 4

enum nd_iort_status {
	ND_IORT_OK = 0,
	ND_IORT_BAD_TABLE,      /* too short, or a node or mapping out of bounds */
	ND_IORT_NO_ROOT_COMPLEX,/* no root complex node for the segment */
	ND_IORT_NO_MAPPING,     /* no ID mapping covers the ID at some node */
	ND_IORT_NOT_ITS,        /* the path ends somewhere other than an ITS group */
	ND_IORT_TOO_DEEP,       /* more than ND_IORT_MAX_HOPS SMMUs on the way */
};

struct nd_iort_route {
	uint32_t device_id;     /* the ID the ITS sees: the MSI's DeviceID */
	uint32_t its_count;     /* ITSs in the group */
	uint32_t its_id;        /* the group's first ITS identifier */
	unsigned int smmu_count;
	struct {
		uint8_t type;           /* IORT node type: 3 SMMUv1/v2, 4 SMMUv3 */
		uint64_t base;
		uint32_t stream_id;     /* the ID arriving at it */
	} smmu[ND_IORT_MAX_HOPS];
};

/* Follows requester ID `rid` of PCI segment `segment` from its root complex
 * node to an ITS group. Mappings flagged "single mapping" are an SMMU's or
 * component's own interrupts, not a translation of input IDs, and are
 * skipped. */
enum nd_iort_status nd_iort_msi_route(const uint8_t *iort, size_t length, uint32_t segment, uint32_t rid,
    struct nd_iort_route *route);

const char *nd_iort_status_string(enum nd_iort_status status);

#ifdef __cplusplus
}
#endif

#endif /* _ND_IORT_H */
