/* SPDX-License-Identifier: BSD-2-Clause */
/* NeoDarwin-Language: portability: a host test of the kernel's C table parser, in the same language. */
/*
 * nd_iort.c against real tables, read from acpidump text files (neoboot's
 * testdata): QEMU virt, where the root complex maps requester IDs straight
 * to the ITS group, and the Radxa Dragon Q8B, where seven root complexes map
 * them through one SMMUv3 (segment N's requester IDs are stream IDs
 * N << 16 | RID, which the SMMU maps to DeviceIDs 0x80000 up).
 *   iort_test QEMU_ACPIDUMP Q8B_ACPIDUMP
 */

#include "nd_iort.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int failures;

#define CHECK(cond, ...)                                                   \
	do {                                                               \
		if (!(cond)) {                                             \
			fprintf(stderr, "FAIL %s:%d: ", __FILE__, __LINE__); \
			fprintf(stderr, __VA_ARGS__);                      \
			fprintf(stderr, "\n");                             \
			failures++;                                        \
		}                                                          \
	} while (0)

/* The table `sig` from an acpidump text file: "SIG @ 0x..." then lines of
 * "    OFFS: hh hh ... ". Returns malloc'd bytes, or NULL. */
static uint8_t *
load_table(const char *path, const char *sig, size_t *length)
{
	FILE *f = fopen(path, "r");
	if (f == NULL) {
		perror(path);
		return NULL;
	}
	char line[512];
	uint8_t *buf = NULL;
	size_t used = 0, cap = 0;
	int in = 0;
	while (fgets(line, sizeof(line), f) != NULL) {
		if (!in) {
			if (strncmp(line, sig, 4) == 0 && strncmp(line + 4, " @ ", 3) == 0) {
				in = 1;
			}
			continue;
		}
		char *colon = strchr(line, ':');
		if (colon == NULL) {
			if (used != 0) {
				break;              /* the blank line after the table */
			}
			continue;
		}
		char *p = colon + 1;
		for (int i = 0; i < 16; i++) {
			while (*p == ' ') {
				p++;
			}
			char *end;
			unsigned long v = strtoul(p, &end, 16);
			if (end != p + 2) {
				break;
			}
			if (used == cap) {
				cap = cap ? 2 * cap : 4096;
				buf = realloc(buf, cap);
			}
			buf[used++] = (uint8_t)v;
			p = end;
		}
	}
	fclose(f);
	*length = used;
	return buf;
}

static void
test_qemu(const char *path)
{
	size_t madtLen, iortLen;
	uint8_t *madt = load_table(path, "APIC", &madtLen);
	uint8_t *iort = load_table(path, "IORT", &iortLen);
	CHECK(madt != NULL && iort != NULL, "QEMU: MADT and IORT present");
	if (madt == NULL || iort == NULL) {
		return;
	}
	struct nd_madt_its its[4];
	unsigned int n = nd_madt_its(madt, madtLen, its, 4);
	CHECK(n == 1, "QEMU: one ITS, found %u", n);
	CHECK(its[0].id == 0 && its[0].base == 0x8080000, "QEMU: ITS 0 at 0x8080000, found %u at 0x%llx", its[0].id,
	    (unsigned long long)its[0].base);

	struct nd_iort_route r;
	enum nd_iort_status s = nd_iort_msi_route(iort, iortLen, 0, 0x20, &r);
	CHECK(s == ND_IORT_OK, "QEMU: 00:04.0 routes (%s)", nd_iort_status_string(s));
	CHECK(r.device_id == 0x20 && r.its_id == 0 && r.its_count == 1 && r.smmu_count == 0,
	    "QEMU: 00:04.0 is DeviceID 0x20 at ITS 0 directly, got 0x%x at ITS %u, %u SMMUs", r.device_id, r.its_id,
	    r.smmu_count);
	s = nd_iort_msi_route(iort, iortLen, 0, 0x200, &r);
	CHECK(s == ND_IORT_OK && r.device_id == 0x200, "QEMU: 02:00.0 is DeviceID 0x200");
	s = nd_iort_msi_route(iort, iortLen, 1, 0, &r);
	CHECK(s == ND_IORT_NO_ROOT_COMPLEX, "QEMU: no segment 1 (%s)", nd_iort_status_string(s));
	/* A truncated table is refused, not overrun. */
	s = nd_iort_msi_route(iort, 60, 0, 0x20, &r);
	CHECK(s != ND_IORT_OK, "QEMU: truncated IORT refused");
	free(madt);
	free(iort);
}

static void
test_q8b(const char *path)
{
	size_t madtLen, iortLen;
	uint8_t *madt = load_table(path, "APIC", &madtLen);
	uint8_t *iort = load_table(path, "IORT", &iortLen);
	CHECK(madt != NULL && iort != NULL, "Q8B: MADT and IORT present");
	if (madt == NULL || iort == NULL) {
		return;
	}
	struct nd_madt_its its[4];
	unsigned int n = nd_madt_its(madt, madtLen, its, 4);
	CHECK(n == 1 && its[0].id == 0 && its[0].base == 0x17a40000, "Q8B: ITS 0 at 0x17a40000 (%u found)", n);

	/* NVMe is on segment 2; its first function below the root port. */
	struct nd_iort_route r;
	enum nd_iort_status s = nd_iort_msi_route(iort, iortLen, 2, 0x100, &r);
	CHECK(s == ND_IORT_OK, "Q8B: 2:01:00.0 routes (%s)", nd_iort_status_string(s));
	CHECK(r.device_id == 0xa0100 && r.its_id == 0 && r.its_count == 1, "Q8B: 2:01:00.0 is DeviceID 0xa0100 at ITS 0, got 0x%x at %u",
	    r.device_id, r.its_id);
	CHECK(r.smmu_count == 1 && r.smmu[0].type == 4 && r.smmu[0].base == 0x14f80000 && r.smmu[0].stream_id == 0x20100,
	    "Q8B: through the SMMUv3 at 0x14f80000 as stream 0x20100, got %u SMMUs, type %u, 0x%llx, 0x%x", r.smmu_count,
	    r.smmu[0].type, (unsigned long long)r.smmu[0].base, r.smmu[0].stream_id);
	s = nd_iort_msi_route(iort, iortLen, 0, 0, &r);
	CHECK(s == ND_IORT_OK && r.device_id == 0x80000, "Q8B: 0:00:00.0 is DeviceID 0x80000, got 0x%x", r.device_id);
	s = nd_iort_msi_route(iort, iortLen, 6, 0xffff, &r);
	CHECK(s == ND_IORT_OK && r.device_id == 0xeffff, "Q8B: 6:ff:1f.7 is DeviceID 0xeffff, got 0x%x", r.device_id);
	s = nd_iort_msi_route(iort, iortLen, 7, 0, &r);
	CHECK(s == ND_IORT_NO_ROOT_COMPLEX, "Q8B: no segment 7");
	free(madt);
	free(iort);
}

int
main(int argc, char **argv)
{
	if (argc != 3) {
		fprintf(stderr, "usage: iort_test QEMU_ACPIDUMP Q8B_ACPIDUMP\n");
		return 2;
	}
	test_qemu(argv[1]);
	test_q8b(argv[2]);
	if (failures != 0) {
		fprintf(stderr, "%d failures\n", failures);
		return 1;
	}
	printf("iort_test: all checks passed\n");
	return 0;
}
