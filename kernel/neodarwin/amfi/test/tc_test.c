// SPDX-License-Identifier: BSD-2-Clause
// NeoDarwin-Language: portability: exercises the C trust-cache engine behind XNU's AMFI table.
//
// ndamfi's trust-cache engine against modules in Apple's version 1 format:
// loading, validation (version, size, sort order), duplicates, static versus
// loadable queries, per-entry hash type and flags, UUID lookups, and the
// Image4 path it declines. With arguments (MODULE CDHASH-FILE), also a
// module that //tools/trustcache wrote, round-tripped: loaded as the static
// trust cache, the listed binary's cdhash found in it with SHA-256's hash
// type, and a cdhash one bit away not found.

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <kern/trustcache.h>
#include "../nd_trustcache.h"

static int passes, failures;

static void
expect(const char *what, int ok)
{
	if (ok) { passes++; } else { failures++; printf("FAIL %s\n", what); }
}

// A module with `n` entries whose cdhashes are {k, k, ...} for k = 1..n
// (sorted), hash type 2 and flags k.
static uint8_t *
module(uint8_t uuid_byte, uint32_t n, size_t *size)
{
	*size = offsetof(struct trust_cache_module1, entries) + n * sizeof(struct trust_cache_entry1);
	struct trust_cache_module1 *m = calloc(1, *size);
	m->version = 1;
	memset(m->uuid, uuid_byte, sizeof(m->uuid));
	m->num_entries = n;
	for (uint32_t i = 0; i < n; i++) {
		memset(m->entries[i].cdhash, (int)(i + 1), CS_CDHASH_LEN);
		m->entries[i].hash_type = 2;
		m->entries[i].flags = (uint8_t)(i + 1);
	}
	return (uint8_t *)m;
}

static bool
grant_only_ltrs(TCType_t type, const uint8_t *payload, size_t payloadSize, const uint8_t *manifest, size_t manifestSize)
{
	(void)payload; (void)payloadSize;
	return type == kTCTypeLTRS && manifestSize == 4 && memcmp(manifest, "ok!", 4) == 0;
}

static bool
parse_hex(const char *s, uint8_t *out, size_t n)
{
	for (size_t i = 0; i < n; i++) {
		unsigned v;
		if (sscanf(s + 2 * i, "%2x", &v) != 1) {
			return false;
		}
		out[i] = (uint8_t)v;
	}
	return true;
}

static void
tool_module(const char *module_path, const char *cdhash_path)
{
	static uint8_t data[1 << 16];
	char line[256] = { 0 };
	FILE *f = fopen(module_path, "rb");
	size_t n = f ? fread(data, 1, sizeof(data), f) : 0;
	if (f) { fclose(f); }
	f = fopen(cdhash_path, "r");
	bool have_line = f && fgets(line, sizeof(line), f) != NULL;
	if (f) { fclose(f); }
	uint8_t cd[CS_CDHASH_LEN];
	expect("tool's module and cdhash read", n > 0 && have_line && parse_hex(line, cd, sizeof(cd)));

	TrustCacheRuntime_t rt;
	TrustCacheMutableRuntime_t mrt;
	TrustCache_t tc;
	TrustCacheQueryToken_t tok;
	uint8_t ht = 0;
	trustCacheInitializeRuntime(&rt, &mrt, false, false, false, NULL);
	expect("tool's module loads as the static trust cache",
	    nd_tc_load_module(&rt, kTCTypeStatic, &tc, (uintptr_t)data, n).error == kTCReturnSuccess);
	expect("its binary is listed with SHA-256's hash type",
	    nd_tc_query(&rt, kTCQueryTypeStatic, cd, &tok).error == kTCReturnSuccess &&
	    nd_tc_query_get_hash_type(&tok, &ht).error == kTCReturnSuccess && ht == 2);
	cd[CS_CDHASH_LEN - 1] ^= 1;
	expect("a cdhash one bit away is not", nd_tc_query(&rt, kTCQueryTypeAll, cd, &tok).error == kTCReturnNotFound);
}

int
main(int argc, char **argv)
{
	if (argc == 3) {
		tool_module(argv[1], argv[2]);
	}

	TrustCacheRuntime_t rt;
	TrustCacheMutableRuntime_t mrt;
	expect("runtime init", trustCacheInitializeRuntime(&rt, &mrt, false, false, false, NULL).error == kTCReturnSuccess);

	size_t s0, s1;
	uint8_t *static0 = module(0xa0, 100, &s0);
	uint8_t *loadable = module(0xb0, 3, &s1);
	TrustCache_t tc0, tc1, tc2;
	expect("load static", nd_tc_load_module(&rt, kTCTypeStatic, &tc0, (uintptr_t)static0, s0).error == kTCReturnSuccess);
	expect("load loadable", nd_tc_load_module(&rt, kTCTypeLTRS, &tc1, (uintptr_t)loadable, s1).error == kTCReturnSuccess);
	expect("duplicate UUID rejected", nd_tc_load_module(&rt, kTCTypeLTRS, &tc2, (uintptr_t)loadable, s1).error == kTCReturnDuplicate);
	expect("engineering refused when not allowed", nd_tc_load_module(&rt, kTCTypeEngineering, &tc2, (uintptr_t)loadable, s1).error == kTCReturnNotPermitted);

	// Validation.
	size_t sb;
	uint8_t *bad = module(0xc0, 4, &sb);
	((struct trust_cache_module1 *)bad)->version = 2;
	expect("version 2 rejected", nd_tc_load_module(&rt, kTCTypeLTRS, &tc2, (uintptr_t)bad, sb).error == kTCReturnInvalidModule);
	((struct trust_cache_module1 *)bad)->version = 1;
	expect("truncated module rejected", nd_tc_load_module(&rt, kTCTypeLTRS, &tc2, (uintptr_t)bad, sb - 1).error == kTCReturnInvalidModule);
	struct trust_cache_module1 *m = (void *)bad;
	uint8_t tmp[CS_CDHASH_LEN];
	memcpy(tmp, m->entries[1].cdhash, CS_CDHASH_LEN);
	memcpy(m->entries[1].cdhash, m->entries[2].cdhash, CS_CDHASH_LEN);
	memcpy(m->entries[2].cdhash, tmp, CS_CDHASH_LEN);
	expect("unsorted module rejected", nd_tc_load_module(&rt, kTCTypeLTRS, &tc2, (uintptr_t)bad, sb).error == kTCReturnInvalidModule);

	// Queries.
	uint8_t cd[CS_CDHASH_LEN];
	TrustCacheQueryToken_t tok;
	int all_found = 1;
	for (int k = 1; k <= 100; k++) {
		memset(cd, k, sizeof(cd));
		uint8_t ht = 0;
		uint64_t flags = 0;
		TCType_t type = kTCTypeInvalid;
		all_found &= nd_tc_query(&rt, kTCQueryTypeStatic, cd, &tok).error == kTCReturnSuccess &&
		    nd_tc_query_get_hash_type(&tok, &ht).error == kTCReturnSuccess && ht == 2 &&
		    nd_tc_query_get_flags(&tok, &flags).error == kTCReturnSuccess && flags == (uint64_t)k &&
		    nd_tc_query_get_tc_type(&tok, &type).error == kTCReturnSuccess && type == kTCTypeStatic;
	}
	expect("every static entry found with its hash type and flags", all_found);
	memset(cd, 0xff, sizeof(cd));
	expect("absent cdhash not found", nd_tc_query(&rt, kTCQueryTypeAll, cd, &tok).error == kTCReturnNotFound);
	memset(cd, 101, sizeof(cd));
	expect("just past the last entry not found", nd_tc_query(&rt, kTCQueryTypeStatic, cd, &tok).error == kTCReturnNotFound);
	memset(cd, 2, sizeof(cd));
	expect("loadable-only query skips static", nd_tc_query(&rt, kTCQueryTypeLoadable, cd, &tok).error == kTCReturnSuccess &&
	    tok.trustCache == &tc1);
	memset(cd, 50, sizeof(cd));
	expect("static entry absent from loadable query", nd_tc_query(&rt, kTCQueryTypeLoadable, cd, &tok).error == kTCReturnNotFound);

	TCCapabilities_t caps = 0;
	expect("capabilities", nd_tc_get_capabilities(&tc0, &caps).error == kTCReturnSuccess &&
	    caps == (kTCCapabilityHashType | kTCCapabilityFlags));
	uint8_t uuid[16];
	const TrustCache_t *found = NULL;
	memset(uuid, 0xb0, sizeof(uuid));
	expect("check runtime for UUID", nd_tc_check_runtime_for_uuid(&rt, uuid, &found).error == kTCReturnSuccess && found == &tc1);
	memset(uuid, 0xee, sizeof(uuid));
	expect("unknown UUID not found", nd_tc_check_runtime_for_uuid(&rt, uuid, &found).error == kTCReturnNotFound);
	expect("Image4-manifested load declined", nd_tc_load(&rt, kTCTypeLTRS, &tc2, 1, 1, 1, 1).error == kTCReturnNotPermitted);

	// Runtime loads through the grant hook (P2-01): refused without a
	// verifier, then only what the verifier grants.
	size_t sg;
	uint8_t *granted = module(0xd0, 2, &sg);
	TrustCache_t tc3;
	expect("runtime load refused without a verifier",
	    nd_tc_load(&rt, kTCTypeLTRS, &tc3, (uintptr_t)granted, sg, (uintptr_t)"ok!", 4).error == kTCReturnNotPermitted);
	nd_tc_set_grant_verifier(grant_only_ltrs);
	expect("a bad grant is refused",
	    nd_tc_load(&rt, kTCTypeLTRS, &tc3, (uintptr_t)granted, sg, (uintptr_t)"no!", 4).error == kTCReturnNotPermitted);
	expect("a static load is never granted",
	    nd_tc_load(&rt, kTCTypeStatic, &tc3, (uintptr_t)granted, sg, (uintptr_t)"ok!", 4).error == kTCReturnNotPermitted);
	expect("a granted module loads as loadable",
	    nd_tc_load(&rt, kTCTypeLTRS, &tc3, (uintptr_t)granted, sg, (uintptr_t)"ok!", 4).error == kTCReturnSuccess &&
	    tc3.type == kTCTypeLTRS);
	nd_tc_set_grant_verifier(NULL);

	printf("ndamfi trust-cache tests: %d passed, %d failed\n", passes, failures);
	return failures ? 1 : 0;
}
