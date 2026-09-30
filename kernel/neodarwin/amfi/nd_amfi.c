// SPDX-License-Identifier: BSD-2-Clause
// NeoDarwin-Language: portability: XNU's AMFI and Image4 interfaces are C function tables registered at startup.
//
// ndamfi: NeoDarwin's provider for the two interfaces XNU's code-signing
// path requires (bsd/kern/kern_trustcache.c panics without them). On Apple
// systems the closed AppleMobileFileIntegrity and AppleImage4 kexts
// register them.
//
//  - amfi->TrustCache: NeoDarwin's libTrustCache (nd_trustcache.c) over
//    Apple's published module format and lookup.
//  - amfi entitlement members (P1-15): over NDEntitlements objects
//    (nd_entitlements_os.cpp), which the code-signing policy (nd_amfi_policy.c)
//    attaches only to binaries a trust cache lists. A process has exactly the
//    entitlements its signature carries if it is trusted, none otherwise, and
//    none from a blob that doesn't parse. amfi->CoreEntitlements itself is
//    left unset: only the PPL pmap path, not built for SBSA, reaches it.
//  - the code-signing policy: a MAC policy registered here, and the
//    enforcement decision (nd_amfi_policy.c).
//  - img4if: registered with a version below 15, so kern_trustcache.c
//    declines Image4 objects itself; no Image4 function is ever called.

#include <kern/startup.h>
#include <libkern/amfi/amfi.h>
#include <libkern/img4/interface.h>
#include <kern/trustcache.h>
#include <sys/systm.h>
#include <uuid/uuid.h>
#include "nd_amfi_internal.h"
#include "nd_trustcache.h"

// -- entitlements (P1-15): nd_entitlements_os.cpp ----------------------------------

static void
ent_invalidate(void *osentitlements)
{
	nd_osent_invalidate(osentitlements);
}

static void *
ent_as_dict(void *osentitlements)
{
	return nd_osent_as_dict(osentitlements);
}

// DER transmuted from XML: not provided. csops(CS_OPS_DER_ENTITLEMENTS_BLOB)
// returns a signature's own DER blob when it has one.
static bool
ent_get_transmuted(void *osentitlements, const CS_GenericBlob **blob)
{
	(void)osentitlements;
	*blob = NULL;
	return false;
}

static bool
ent_get_xml(void *osentitlements, CS_GenericBlob **blob)
{
	return nd_osent_get_xml(osentitlements, blob);
}

static bool
no_profile_exemptions(const uint8_t **profile, size_t *length)
{
	*profile = NULL;
	*length = 0;
	return false;
}

static bool
no_udid(const uint8_t **udid, size_t *length)
{
	*udid = NULL;
	*length = 0;
	return false;
}

static void *
no_context_object(CEQueryContext_t ctx)
{
	(void)ctx;
	return NULL;
}

static kern_return_t
adjust_with_monitor(void *os_entitlements, const CEQueryContext_t ce_ctx, const void *monitor_sig_obj,
    const char *identity, const uint32_t code_signing_flags)
{
	(void)os_entitlements; (void)ce_ctx; (void)monitor_sig_obj; (void)identity; (void)code_signing_flags;
	return KERN_NOT_SUPPORTED;  // SBSA has no code-signing monitor
}

// The signature's storage is final here: read its entitlements into the
// object the policy attached (none for an untrusted binary).
static kern_return_t
adjust_without_monitor(void *os_entitlements, struct cs_blob *blob)
{
	unsigned count = 0;
	return nd_osent_adopt(os_entitlements, blob, &count);
}

static kern_return_t
query_boolean(const void *os_entitlements, const char *name)
{
	return nd_osent_query_bool(os_entitlements, name);
}

static kern_return_t
query_boolean_proc(const proc_t proc, const char *name)
{
	return nd_osent_query_bool_proc(proc, name);
}

static kern_return_t
query_string(const void *os_entitlements, const char *name, const char *value)
{
	return nd_osent_query_string(os_entitlements, name, value);
}

static kern_return_t
query_string_proc(const proc_t proc, const char *name, const char *value)
{
	return nd_osent_query_string_proc(proc, name, value);
}

static kern_return_t
copy_object(const void *os_entitlements, const char *name, void **object)
{
	return nd_osent_copy_object(os_entitlements, name, object);
}

static kern_return_t
copy_object_proc(const proc_t proc, const char *name, void **object)
{
	return nd_osent_copy_object_proc(proc, name, object);
}

// -- trust caches: the engine, and a line per module loaded ----------------------

static TCReturn_t
load_module(TrustCacheRuntime_t *runtime, TCType_t type, TrustCache_t *trustCache, uintptr_t dataAddr, size_t dataSize)
{
	TCReturn_t r = nd_tc_load_module(runtime, type, trustCache, dataAddr, dataSize);
	if (r.error == kTCReturnSuccess) {
		const struct trust_cache_module1 *m = (const void *)trustCache->module;
		uuid_string_t uuid;
		uuid_unparse_upper(m->uuid, uuid);
		printf("ndamfi: %s trust cache %s: %u entries\n", type == kTCTypeStatic ? "static" : "loadable", uuid, m->num_entries);
	}
	return r;
}

static const amfi_t nd_amfi = {
	.OSEntitlements_invalidate = ent_invalidate,
	.OSEntitlements_asdict = ent_as_dict,
	.OSEntitlements_get_transmuted = ent_get_transmuted,
	.OSEntitlements_get_xml = ent_get_xml,
	.get_legacy_profile_exemptions = no_profile_exemptions,
	.get_udid = no_udid,
	.query_context_to_object = no_context_object,
	.TrustCache = {
		.version = TRUST_CACHE_INTERFACE_VERSION,
		.loadModule = load_module,
		.load = nd_tc_load,
		.query = nd_tc_query,
		.getCapabilities = nd_tc_get_capabilities,
		.queryGetTCType = nd_tc_query_get_tc_type,
		.queryGetCapabilities = nd_tc_query_get_capabilities,
		.queryGetHashType = nd_tc_query_get_hash_type,
		.queryGetFlags = nd_tc_query_get_flags,
		.queryGetConstraintCategory = nd_tc_query_get_constraint_category,
		.constructInvalid = nd_tc_construct_invalid,
		.checkRuntimeForUUID = nd_tc_check_runtime_for_uuid,
		.extractModule = nd_tc_extract_module,
		.getModule = nd_tc_get_module,
		.getUUID = nd_tc_get_uuid,
	},
	.OSEntitlements = {
		.version = OSENTITLEMENTS_INTERFACE_VERSION,
		.adjustContextWithMonitor = adjust_with_monitor,
		.adjustContextWithoutMonitor = adjust_without_monitor,
		.queryEntitlementBoolean = query_boolean,
		.queryEntitlementBooleanWithProc = query_boolean_proc,
		.queryEntitlementString = query_string,
		.queryEntitlementStringWithProc = query_string_proc,
		.copyEntitlementAsOSObject = copy_object,
		.copyEntitlementAsOSObjectWithProc = copy_object_proc,
	},
};

// Version 14: the last before the payload/manifest helpers kern_trustcache.c
// would call. Every other member stays NULL and is never reached.
static const img4_interface_t nd_img4 = {
	.i4if_version = 14,
};

static void
ndamfi_register(void)
{
	img4_interface_register(&nd_img4);
	amfi_interface_register(&nd_amfi);
	nd_amfi_policy_init();
}

STARTUP(EARLY_BOOT, STARTUP_RANK_LAST, ndamfi_register);
