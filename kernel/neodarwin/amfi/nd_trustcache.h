// SPDX-License-Identifier: BSD-2-Clause
// NeoDarwin-Language: portability: the libTrustCache interface XNU calls through the AMFI table is C.
//
// NeoDarwin's trust-cache engine: the functions behind amfi->TrustCache
// (libkern/libkern/amfi/amfi.h), over Apple's version 1 module format
// (<kern/trustcache.h>) and Apple's lookup (nd_tc_lookup.c).

#ifndef ND_TRUSTCACHE_H
#define ND_TRUSTCACHE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <TrustCache/API.h>

struct trust_cache_module1;

bool nd_tc_lookup_module(const struct trust_cache_module1 *module, const uint8_t cdhash[kTCEntryHashSize],
    uint8_t *hash_type, uint8_t *flags);

TCReturn_t nd_tc_load_module(TrustCacheRuntime_t *runtime, TCType_t type, TrustCache_t *trustCache,
    uintptr_t dataAddr, size_t dataSize);
TCReturn_t nd_tc_load(TrustCacheRuntime_t *runtime, TCType_t type, TrustCache_t *trustCache,
    uintptr_t payloadAddr, size_t payloadSize, uintptr_t manifestAddr, size_t manifestSize);
TCReturn_t nd_tc_query(const TrustCacheRuntime_t *runtime, TCQueryType_t queryType,
    const uint8_t cdhash[kTCEntryHashSize], TrustCacheQueryToken_t *queryToken);
TCReturn_t nd_tc_get_capabilities(const TrustCache_t *trustCache, TCCapabilities_t *capabilities);
TCReturn_t nd_tc_query_get_tc_type(const TrustCacheQueryToken_t *queryToken, TCType_t *typeRet);
TCReturn_t nd_tc_query_get_capabilities(const TrustCacheQueryToken_t *queryToken, TCCapabilities_t *capabilities);
TCReturn_t nd_tc_query_get_hash_type(const TrustCacheQueryToken_t *queryToken, uint8_t *hashTypeRet);
TCReturn_t nd_tc_query_get_flags(const TrustCacheQueryToken_t *queryToken, uint64_t *flagsRet);
TCReturn_t nd_tc_query_get_constraint_category(const TrustCacheQueryToken_t *queryToken, uint8_t *constraintCategoryRet);
TCReturn_t nd_tc_construct_invalid(TrustCache_t *trustCache, const uint8_t *moduleAddr, size_t moduleSize);
TCReturn_t nd_tc_check_runtime_for_uuid(const TrustCacheRuntime_t *runtime, const uint8_t checkUUID[kUUIDSize],
    const TrustCache_t **trustCacheRet);
TCReturn_t nd_tc_extract_module(TrustCache_t *trustCache, const uint8_t *dataAddr, size_t dataSize);
TCReturn_t nd_tc_get_module(const TrustCache_t *trustCache, const uint8_t **moduleAddrRet, size_t *moduleSizeRet);
TCReturn_t nd_tc_get_uuid(const TrustCache_t *trustCache, uint8_t returnUUID[kUUIDSize]);

// Runtime trust-cache loads (P2-01's hook). XNU's load_trust_cache*() hand
// amfi->TrustCache.load a payload (here: a version 1 module) and a manifest
// (with Image4, its signature). NeoDarwin's grant is an ndsign signature
// over the module, checked against the package key chain. ndamfi registers
// the verifier (nd_amfi_grant.c); until one is registered every runtime load
// is refused. It returns true only for a manifest that grants `payload` as a
// trust cache of `type`, after which the module is loaded as a static one is
// (nd_tc_load_module's checks, the loadable list).
typedef bool (*nd_tc_grant_verifier_t)(TCType_t type, const uint8_t *payload, size_t payloadSize,
    const uint8_t *manifest, size_t manifestSize);
void nd_tc_set_grant_verifier(nd_tc_grant_verifier_t verifier);

#endif
