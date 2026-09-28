// SPDX-License-Identifier: BSD-2-Clause
// NeoDarwin-Language: portability: the libTrustCache entry points XNU links against and calls through the AMFI table are C.
//
// NeoDarwin's libTrustCache. Apple's library is not published; its module
// format and lookup were (xnu's <kern/trustcache.h>, and nd_tc_lookup.c from
// an earlier xnu). This file holds the runtime XNU initialises directly
// (trustCacheInitializeRuntime, TCTypeConfig) and the functions behind
// amfi->TrustCache: module validation, the static and loadable lists, and
// queries. Callers (bsd/kern/kern_trustcache.c) hold the trust-cache lock.
//
// Supported: version 1 modules, the format xnu publishes. Not supported:
// Image4-manifested trust caches. NeoDarwin signs trust caches its own way
// (ndsign, later), so load() declines them.
//
// Loading is default deny: every trust-cache type requires an entitlement no
// Apple binary carries, until NeoDarwin's policy grants it.

#include <string.h>
#include <kern/trustcache.h>
#include "nd_trustcache.h"

#define ND_TRUST_CACHE_LOAD "neodarwin.trust-cache.load"

const TCTypeConfig_t TCTypeConfig[kTCTypeTotal] = {
	[kTCTypeInvalid]         = { .entitlementValue = ND_TRUST_CACHE_LOAD },
	[kTCTypeStatic]          = { .entitlementValue = ND_TRUST_CACHE_LOAD },
	[kTCTypeEngineering]     = { .entitlementValue = ND_TRUST_CACHE_LOAD },
	[kTCTypeLegacy]          = { .entitlementValue = ND_TRUST_CACHE_LOAD },
	[kTCTypeLTRS]            = { .entitlementValue = ND_TRUST_CACHE_LOAD },
	[kTCTypeDTRS]            = { .entitlementValue = ND_TRUST_CACHE_LOAD },
	[kTCTypeCryptex1BootOS]  = { .entitlementValue = ND_TRUST_CACHE_LOAD },
	[kTCTypeCryptex1BootApp] = { .entitlementValue = ND_TRUST_CACHE_LOAD },
};

static TCReturn_t
ret(uint8_t error)
{
	return (TCReturn_t){ .component = 0x4e /* 'N' */, .error = error };
}

TCReturn_t
trustCacheInitializeRuntime(
	TrustCacheRuntime_t        *runtime,
	TrustCacheMutableRuntime_t *mutableRuntime,
	bool                        allowSecondStaticTC,
	bool                        allowEngineeringTC,
	bool                        allowLegacyTC,
	const img4_runtime_t       *image4RT)
{
	memset(mutableRuntime, 0, sizeof(*mutableRuntime));
	memset(runtime, 0, sizeof(*runtime));
	runtime->mutableRuntime      = mutableRuntime;
	runtime->image4RT            = image4RT;
	runtime->allowSecondStaticTC = allowSecondStaticTC;
	runtime->allowEngineeringTC  = allowEngineeringTC;
	runtime->allowLegacyTC       = allowLegacyTC;
	return ret(kTCReturnSuccess);
}

// -- modules ---------------------------------------------------------------------

#define MODULE_HEADER_SIZE offsetof(struct trust_cache_module1, entries)

static const struct trust_cache_module1 *
module_of(const TrustCache_t *tc)
{
	return (const struct trust_cache_module1 *)(const void *)tc->module;
}

// A module is usable when it is version 1, its entries fit, and they are in
// strictly increasing cdhash order (the lookup is a binary search).
static bool
module_valid(const uint8_t *data, size_t size, size_t *module_size)
{
	if (data == NULL || size < MODULE_HEADER_SIZE) {
		return false;
	}
	const struct trust_cache_module1 *m = (const void *)data;
	if (m->version != 1) {
		return false;
	}
	size_t n = m->num_entries;
	if (n > (size - MODULE_HEADER_SIZE) / sizeof(struct trust_cache_entry1)) {
		return false;
	}
	for (size_t i = 1; i < n; i++) {
		if (memcmp(m->entries[i - 1].cdhash, m->entries[i].cdhash, kTCEntryHashSize) >= 0) {
			return false;
		}
	}
	*module_size = MODULE_HEADER_SIZE + n * sizeof(struct trust_cache_entry1);
	return true;
}

static bool
is_static(TCType_t type)
{
	return type == kTCTypeStatic;
}

static TrustCache_t *
find_uuid(TrustCache_t *list, const uint8_t uuid[kUUIDSize])
{
	for (TrustCache_t *tc = list; tc != NULL; tc = tc->next) {
		if (memcmp(tc->uuid, uuid, kUUIDSize) == 0) {
			return tc;
		}
	}
	return NULL;
}

TCReturn_t
nd_tc_load_module(TrustCacheRuntime_t *runtime, TCType_t type, TrustCache_t *trustCache, uintptr_t dataAddr, size_t dataSize)
{
	if (runtime == NULL || runtime->mutableRuntime == NULL || trustCache == NULL ||
	    type <= kTCTypeInvalid || type >= kTCTypeTotal) {
		return ret(kTCReturnError);
	}
	if (type == kTCTypeEngineering && !runtime->allowEngineeringTC) {
		return ret(kTCReturnNotPermitted);
	}
	if (type == kTCTypeLegacy && !runtime->allowLegacyTC) {
		return ret(kTCReturnNotPermitted);
	}
	const uint8_t *data = (const uint8_t *)dataAddr;
	size_t module_size = 0;
	if (!module_valid(data, dataSize, &module_size)) {
		return ret(kTCReturnInvalidModule);
	}
	const struct trust_cache_module1 *m = (const void *)data;
	TrustCacheMutableRuntime_t *mr = runtime->mutableRuntime;
	if (find_uuid(mr->staticTrustCaches, m->uuid) || find_uuid(mr->loadableTrustCaches, m->uuid)) {
		return ret(kTCReturnDuplicate);
	}
	memset(trustCache, 0, sizeof(*trustCache));
	trustCache->type = type;
	trustCache->capabilities = kTCCapabilityHashType | kTCCapabilityFlags;
	trustCache->module = data;
	trustCache->moduleSize = module_size;
	memcpy(trustCache->uuid, m->uuid, kUUIDSize);
	TrustCache_t **list = is_static(type) ? &mr->staticTrustCaches : &mr->loadableTrustCaches;
	trustCache->next = *list;
	*list = trustCache;
	return ret(kTCReturnSuccess);
}

TCReturn_t
nd_tc_load(TrustCacheRuntime_t *runtime, TCType_t type, TrustCache_t *trustCache, uintptr_t payloadAddr, size_t payloadSize,
    uintptr_t manifestAddr, size_t manifestSize)
{
	(void)runtime; (void)type; (void)trustCache; (void)payloadAddr; (void)payloadSize; (void)manifestAddr; (void)manifestSize;
	return ret(kTCReturnNotPermitted);  // Image4-manifested: see the file header
}

TCReturn_t
nd_tc_construct_invalid(TrustCache_t *trustCache, const uint8_t *moduleAddr, size_t moduleSize)
{
	if (trustCache == NULL) {
		return ret(kTCReturnError);
	}
	memset(trustCache, 0, sizeof(*trustCache));
	trustCache->type = kTCTypeInvalid;
	trustCache->module = moduleAddr;
	trustCache->moduleSize = moduleSize;
	return ret(kTCReturnSuccess);
}

TCReturn_t
nd_tc_extract_module(TrustCache_t *trustCache, const uint8_t *dataAddr, size_t dataSize)
{
	(void)trustCache; (void)dataAddr; (void)dataSize;
	return ret(kTCReturnNotPermitted);  // extraction is from Image4 payloads
}

// -- queries -------------------------------------------------------------------------

static bool
search(const TrustCache_t *list, const uint8_t cdhash[kTCEntryHashSize], TrustCacheQueryToken_t *token)
{
	for (const TrustCache_t *tc = list; tc != NULL; tc = tc->next) {
		const struct trust_cache_module1 *m = module_of(tc);
		uint8_t hash_type, flags;
		if (tc->type != kTCTypeInvalid && nd_tc_lookup_module(m, cdhash, &hash_type, &flags)) {
			// The lookup reports values, not the entry; find the entry for the token.
			size_t lo = 0, hi = m->num_entries;
			while (lo < hi) {
				size_t mid = lo + (hi - lo) / 2;
				int c = memcmp(cdhash, m->entries[mid].cdhash, kTCEntryHashSize);
				if (c == 0) {
					token->trustCache = tc;
					token->trustCacheEntry = &m->entries[mid];
					return true;
				}
				if (c > 0) {
					lo = mid + 1;
				} else {
					hi = mid;
				}
			}
		}
	}
	return false;
}

TCReturn_t
nd_tc_query(const TrustCacheRuntime_t *runtime, TCQueryType_t queryType, const uint8_t cdhash[kTCEntryHashSize],
    TrustCacheQueryToken_t *queryToken)
{
	if (runtime == NULL || runtime->mutableRuntime == NULL || cdhash == NULL || queryToken == NULL) {
		return ret(kTCReturnError);
	}
	const TrustCacheMutableRuntime_t *mr = runtime->mutableRuntime;
	bool found = false;
	if (queryType == kTCQueryTypeStatic || queryType == kTCQueryTypeAll) {
		found = search(mr->staticTrustCaches, cdhash, queryToken);
	}
	if (!found && (queryType == kTCQueryTypeLoadable || queryType == kTCQueryTypeAll)) {
		found = search(mr->loadableTrustCaches, cdhash, queryToken);
	}
	return ret(found ? kTCReturnSuccess : kTCReturnNotFound);
}

static const struct trust_cache_entry1 *
entry_of(const TrustCacheQueryToken_t *token)
{
	return token ? token->trustCacheEntry : NULL;
}

TCReturn_t
nd_tc_get_capabilities(const TrustCache_t *trustCache, TCCapabilities_t *capabilities)
{
	if (trustCache == NULL || capabilities == NULL) {
		return ret(kTCReturnError);
	}
	*capabilities = trustCache->capabilities;
	return ret(kTCReturnSuccess);
}

TCReturn_t
nd_tc_query_get_tc_type(const TrustCacheQueryToken_t *queryToken, TCType_t *typeRet)
{
	if (queryToken == NULL || queryToken->trustCache == NULL || typeRet == NULL) {
		return ret(kTCReturnError);
	}
	*typeRet = queryToken->trustCache->type;
	return ret(kTCReturnSuccess);
}

TCReturn_t
nd_tc_query_get_capabilities(const TrustCacheQueryToken_t *queryToken, TCCapabilities_t *capabilities)
{
	if (queryToken == NULL) {
		return ret(kTCReturnError);
	}
	return nd_tc_get_capabilities(queryToken->trustCache, capabilities);
}

TCReturn_t
nd_tc_query_get_hash_type(const TrustCacheQueryToken_t *queryToken, uint8_t *hashTypeRet)
{
	const struct trust_cache_entry1 *e = entry_of(queryToken);
	if (e == NULL || hashTypeRet == NULL) {
		return ret(kTCReturnError);
	}
	*hashTypeRet = e->hash_type;
	return ret(kTCReturnSuccess);
}

TCReturn_t
nd_tc_query_get_flags(const TrustCacheQueryToken_t *queryToken, uint64_t *flagsRet)
{
	const struct trust_cache_entry1 *e = entry_of(queryToken);
	if (e == NULL || flagsRet == NULL) {
		return ret(kTCReturnError);
	}
	*flagsRet = e->flags;
	return ret(kTCReturnSuccess);
}

// Version 1 modules carry no launch-constraint category: report category 0.
TCReturn_t
nd_tc_query_get_constraint_category(const TrustCacheQueryToken_t *queryToken, uint8_t *constraintCategoryRet)
{
	if (entry_of(queryToken) == NULL || constraintCategoryRet == NULL) {
		return ret(kTCReturnError);
	}
	*constraintCategoryRet = 0;
	return ret(kTCReturnSuccess);
}

TCReturn_t
nd_tc_check_runtime_for_uuid(const TrustCacheRuntime_t *runtime, const uint8_t checkUUID[kUUIDSize],
    const TrustCache_t **trustCacheRet)
{
	if (runtime == NULL || runtime->mutableRuntime == NULL || checkUUID == NULL) {
		return ret(kTCReturnError);
	}
	const TrustCache_t *tc = find_uuid(runtime->mutableRuntime->staticTrustCaches, checkUUID);
	if (tc == NULL) {
		tc = find_uuid(runtime->mutableRuntime->loadableTrustCaches, checkUUID);
	}
	if (trustCacheRet) {
		*trustCacheRet = tc;
	}
	return ret(tc ? kTCReturnSuccess : kTCReturnNotFound);
}

TCReturn_t
nd_tc_get_module(const TrustCache_t *trustCache, const uint8_t **moduleAddrRet, size_t *moduleSizeRet)
{
	if (trustCache == NULL || moduleAddrRet == NULL || moduleSizeRet == NULL) {
		return ret(kTCReturnError);
	}
	*moduleAddrRet = trustCache->module;
	*moduleSizeRet = trustCache->moduleSize;
	return ret(kTCReturnSuccess);
}

TCReturn_t
nd_tc_get_uuid(const TrustCache_t *trustCache, uint8_t returnUUID[kUUIDSize])
{
	if (trustCache == NULL || returnUUID == NULL) {
		return ret(kTCReturnError);
	}
	memcpy(returnUUID, trustCache->uuid, kUUIDSize);
	return ret(kTCReturnSuccess);
}
