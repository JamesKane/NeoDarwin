// SPDX-License-Identifier: BSD-2-Clause
// NeoDarwin-Language: expressibility: C header consumed by XNU's trust-cache code; Apple's libTrustCache is not published.
//
// NeoDarwin's definition of the libTrustCache interface XNU compiles against.
// Written from XNU's own use of these names (bsd/kern/kern_trustcache.c,
// bsd/sys/trust_caches.h, libkern/libkern/amfi/amfi.h), not from Apple's
// library. Layouts are NeoDarwin's ABI: the only other party is NeoDarwin's
// own code-signing policy module, which provides the AMFI function table.
#ifndef ND_TRUSTCACHE_API_H
#define ND_TRUSTCACHE_API_H

#include <sys/cdefs.h>
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include <img4/firmware.h>

__BEGIN_DECLS

#define kTCEntryHashSize 20u   /* code-directory hash length, CS_CDHASH_LEN */
#define kUUIDSize        16u

typedef enum {
	kTCTypeInvalid = 0,
	kTCTypeStatic,
	kTCTypeEngineering,
	kTCTypeLegacy,
	kTCTypeLTRS,
	kTCTypeDTRS,
	kTCTypeCryptex1BootOS,
	kTCTypeCryptex1BootApp,
	kTCTypeTotal
} TCType_t;

/* Per-type policy: which entitlement value a loader must hold, if any. */
typedef struct {
	const char *entitlementValue;
} TCTypeConfig_t;

extern const TCTypeConfig_t TCTypeConfig[kTCTypeTotal];

typedef enum {
	kTCQueryTypeStatic = 0,
	kTCQueryTypeLoadable,
	kTCQueryTypeAll,
	kTCQueryTypeTotal
} TCQueryType_t;

typedef uint64_t TCCapabilities_t;
#define kTCCapabilityNone ((TCCapabilities_t)0)

enum {
	kTCReturnSuccess = 0,
	kTCReturnError,
	kTCReturnNotFound,
	kTCReturnDuplicate,
};

typedef union {
	uint32_t rawValue;
	struct {
		uint8_t  component;
		uint8_t  error;
		uint16_t uniqueError;
	};
} TCReturn_t;

typedef struct TrustCache {
	struct TrustCache *next;
	TCType_t           type;
	TCCapabilities_t   capabilities;
	const uint8_t     *module;
	size_t             moduleSize;
	uint8_t            uuid[kUUIDSize];
} TrustCache_t;

typedef struct {
	TrustCache_t *staticTrustCaches;
	TrustCache_t *loadableTrustCaches;
} TrustCacheMutableRuntime_t;

typedef struct {
	TrustCacheMutableRuntime_t *mutableRuntime;
	const img4_runtime_t       *image4RT;
	bool allowSecondStaticTC;
	bool allowEngineeringTC;
	bool allowLegacyTC;
	bool allowPMAPTC;
} TrustCacheRuntime_t;

typedef struct {
	const TrustCache_t *trustCache;
	const void         *trustCacheEntry;
} TrustCacheQueryToken_t;

TCReturn_t trustCacheInitializeRuntime(
	TrustCacheRuntime_t        *runtime,
	TrustCacheMutableRuntime_t *mutableRuntime,
	bool                        allowSecondStaticTC,
	bool                        allowEngineeringTC,
	bool                        allowLegacyTC,
	const img4_runtime_t       *image4RT);

__END_DECLS

#endif
