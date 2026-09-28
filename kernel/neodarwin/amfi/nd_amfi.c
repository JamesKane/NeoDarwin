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
//  - amfi entitlement members: default deny. Queries return KERN_DENIED and
//    copies KERN_NOT_FOUND, and a code blob's entitlement context is accepted
//    as empty, so a program runs with no entitlements. A CoreEntitlements
//    implementation replaces this before anything needs one. CoreEntitlements
//    itself is left unset: only the PPL pmap path, not built for SBSA,
//    reaches it.
//  - img4if: registered with a version below 15, so kern_trustcache.c
//    declines Image4 objects itself; no Image4 function is ever called.

#include <kern/startup.h>
#include <libkern/amfi/amfi.h>
#include <libkern/img4/interface.h>
#include "nd_trustcache.h"

// -- entitlements: default deny -------------------------------------------------

static void
ent_invalidate(void *osentitlements)
{
	(void)osentitlements;
}

static void *
ent_as_dict(void *osentitlements)
{
	(void)osentitlements;
	return NULL;
}

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
	(void)osentitlements;
	*blob = NULL;
	return false;
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

static kern_return_t
adjust_without_monitor(void *os_entitlements, struct cs_blob *blob)
{
	(void)os_entitlements; (void)blob;
	return KERN_SUCCESS;  // an empty entitlement context
}

static kern_return_t
query_boolean(const void *os_entitlements, const char *name)
{
	(void)os_entitlements; (void)name;
	return KERN_DENIED;
}

static kern_return_t
query_boolean_proc(const proc_t proc, const char *name)
{
	(void)proc; (void)name;
	return KERN_DENIED;
}

static kern_return_t
query_string(const void *os_entitlements, const char *name, const char *value)
{
	(void)os_entitlements; (void)name; (void)value;
	return KERN_DENIED;
}

static kern_return_t
query_string_proc(const proc_t proc, const char *name, const char *value)
{
	(void)proc; (void)name; (void)value;
	return KERN_DENIED;
}

static kern_return_t
copy_object(const void *os_entitlements, const char *name, void **object)
{
	(void)os_entitlements; (void)name;
	*object = NULL;
	return KERN_NOT_FOUND;
}

static kern_return_t
copy_object_proc(const proc_t proc, const char *name, void **object)
{
	(void)proc; (void)name;
	*object = NULL;
	return KERN_NOT_FOUND;
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
		.loadModule = nd_tc_load_module,
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
}

STARTUP(EARLY_BOOT, STARTUP_RANK_LAST, ndamfi_register);
