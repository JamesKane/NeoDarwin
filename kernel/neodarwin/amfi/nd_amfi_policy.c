// SPDX-License-Identifier: BSD-2-Clause
// NeoDarwin-Language: portability: XNU's MAC policy interface and code-signing globals are C.
//
// ndamfi's code-signing policy (P1-15, docs/kernel/amfi-provider.md §4): what
// Apple's AMFI kext decides through its MAC policy, NeoDarwin decides here.
//
//  - Trust. When the kernel attaches a code signature to a vnode
//    (ubc_cs_blob_add, for an executable, dyld or a library), the policy
//    looks its cdhash up in the loaded trust caches. A listed binary, with
//    the hash type the cache names, is a platform binary (CS_PLATFORM_BINARY)
//    and gets an entitlements object, which nd_entitlements_os.cpp fills with
//    exactly the entitlements its signature carries. Anything else has none.
//  - Enforcement. Under enforcement, a signature that isn't listed is refused
//    (EPERM), so the kernel won't exec the binary (the exec fails past its
//    point of no return, so the process is killed: SIGKILL), map it for
//    dyld or accept it through F_ADDFILESIGS; with XNU's process enforcement on
//    (cs_process_enforcement_enable, which CONFIG_ENFORCE_SIGNED_CODE sets on
//    iOS), an exec without an accepted signature fails and a page without one
//    is never executed. Trusted processes also run CS_HARD | CS_KILL: an
//    invalid page is refused and kills them. A debugger can't lift that
//    (mpo_proc_check_run_cs_invalid) unless the target is entitled
//    get-task-allow.
//  - The default. Enforcement is on when neoboot supplied a static trust
//    cache (/chosen/memory-map TrustCache): every image NeoDarwin builds
//    carries one. The boot-arg nd_cs_enforcement=0 turns it off, =1 forces it
//    on without a trust cache (nothing then runs); XNU's
//    cs_enforcement_disable=1 (honoured on debug-enabled boots) also turns it
//    off. Without enforcement, listed binaries are still platform binaries
//    with their entitlements, and the rest run as before P1-15, ad hoc and
//    entitled to nothing.
//
// The decision is made at EARLY_BOOT, after cs_init() and before the
// trust caches load and XNU's code-signing globals become read-only.

#include <kern/startup.h>
#include <os/atomic_private.h>
#include <libkern/amfi/amfi.h>
#include <mach/kern_return.h>
#include <pexpert/pexpert.h>
#include <pexpert/device_tree.h>
#include <security/mac_policy.h>
#include <sys/codesign.h>
#include <sys/errno.h>
#include <sys/proc.h>
#include <sys/sysctl.h>
#include <sys/systm.h>
#include <sys/trust_caches.h>
#include <sys/vnode.h>
#include "nd_amfi_internal.h"

// XNU's code-signing switches (bsd/kern/kern_cs.c), SECURITY_READ_ONLY_LATE:
// writable until lockdown.
extern int cs_process_enforcement_enable;
extern int cs_system_enforcement_enable;

static int nd_enforcing;
static unsigned int nd_refused;

SYSCTL_DECL(_security_codesigning);
SYSCTL_NODE(_security_codesigning, OID_AUTO, neodarwin, CTLFLAG_RD, 0, "NeoDarwin code signing (ndamfi)");
SYSCTL_INT(_security_codesigning_neodarwin, OID_AUTO, enforcement, CTLFLAG_RD | CTLFLAG_LOCKED, &nd_enforcing, 0,
    "untrusted code is refused");
SYSCTL_UINT(_security_codesigning_neodarwin, OID_AUTO, refused, CTLFLAG_RD | CTLFLAG_LOCKED, &nd_refused, 0,
    "code signatures refused because no trust cache lists them");

static bool
static_trust_cache_supplied(void)
{
	DTEntry map;
	const DTTrustCacheRange *range = NULL;
	unsigned int size = 0;
	return SecureDTLookupEntry(NULL, "chosen/memory-map", &map) == kSuccess &&
	       SecureDTGetProperty(map, "TrustCache", (const void **)&range, &size) == kSuccess &&
	       size == sizeof(*range) && range->length != 0;
}

static void
decide_enforcement(void)
{
	int arg = 0;
	bool forced = PE_parse_boot_argn("nd_cs_enforcement", &arg, sizeof(arg));
	bool supplied = static_trust_cache_supplied();
	const char *why;
	if (!cs_system_enforcement_enable) {
		nd_enforcing = 0;
		why = "cs_enforcement_disable";
	} else if (forced) {
		nd_enforcing = arg != 0 ? 1 : 0;
		why = arg ? "nd_cs_enforcement=1" : "nd_cs_enforcement=0";
	} else {
		nd_enforcing = supplied ? 1 : 0;
		why = supplied ? "a static trust cache from the loader" : "no static trust cache";
	}
	if (nd_enforcing) {
		cs_process_enforcement_enable = 1;
	}
	printf("ndamfi: code-signing enforcement %s (%s): %s\n", nd_enforcing ? "on" : "off", why,
	    nd_enforcing ? "code no trust cache lists is refused" : "untrusted code runs without entitlements");
}

// -- the MAC policy ------------------------------------------------------------------

static bool
listed(struct cs_blob *blob)
{
	const uint8_t *cdhash = csblob_get_cdhash(blob);
	TrustCacheQueryToken_t token;
	uint8_t hash_type = 0;
	if (cdhash == NULL || query_trust_cache(kTCQueryTypeAll, cdhash, &token) != KERN_SUCCESS) {
		return false;
	}
	return amfi->TrustCache.queryGetHashType(&token, &hash_type).error == kTCReturnSuccess &&
	       hash_type == csblob_get_hashtype(blob);
}

static int
nd_vnode_check_signature(struct vnode *vp, struct label *label, cpu_type_t cpu_type, struct cs_blob *cs_blob,
    unsigned int *cs_flags, unsigned int *signer_type, int flags, unsigned int platform,
    char **fatal_failure_desc, size_t *fatal_failure_desc_len)
{
	(void)label; (void)cpu_type; (void)signer_type; (void)flags; (void)platform;
	(void)fatal_failure_desc; (void)fatal_failure_desc_len;

	// CS_SIGNED marks a signature the policy accepted, as AMFI's does: XNU
	// looks a process's signature up (csproc_get_blob) only when it is set,
	// for its entitlements, platform status and csops.
	if (listed(cs_blob)) {
		*cs_flags |= CS_SIGNED | CS_PLATFORM_BINARY;
		if (nd_enforcing) {
			*cs_flags |= CS_HARD | CS_KILL;
		}
		// A new signature gets its entitlements object; a revalidated one
		// (ubc_cs_blob_revalidate) keeps the one it has.
		if (csblob_os_entitlements_get(cs_blob) == NULL) {
			void *osent = nd_osent_create();
			if (osent == NULL) {
				return ENOMEM;
			}
			csblob_os_entitlements_set(cs_blob, osent);  // retains
			nd_osent_release(osent);
		}
		return 0;
	}
	*cs_flags &= ~CS_PLATFORM_BINARY;
	if (!nd_enforcing) {
		*cs_flags |= CS_SIGNED;
		return 0;
	}
	os_atomic_inc(&nd_refused, relaxed);
	const uint8_t *cd = csblob_get_cdhash(cs_blob);
	const char *name = vnode_getname(vp);
	printf("ndamfi: refused %s (cdhash %02x%02x%02x%02x%02x%02x%02x%02x...): not in a trust cache\n",
	    name ? name : "?", cd[0], cd[1], cd[2], cd[3], cd[4], cd[5], cd[6], cd[7]);
	if (name) {
		vnode_putname(name);
	}
	return EPERM;
}

static int
nd_proc_check_run_cs_invalid(struct proc *p)
{
	if (!nd_enforcing) {
		return 0;
	}
	return nd_osent_query_bool_proc(p, "get-task-allow") == KERN_SUCCESS ? 0 : EPERM;
}

static const struct mac_policy_ops nd_policy_ops = {
	.mpo_vnode_check_signature = nd_vnode_check_signature,
	.mpo_proc_check_run_cs_invalid = nd_proc_check_run_cs_invalid,
};

static struct mac_policy_conf nd_policy_conf = {
	.mpc_name = "ndamfi",
	.mpc_fullname = "NeoDarwin code-signing policy (trust caches and entitlements)",
	.mpc_ops = &nd_policy_ops,
	.mpc_loadtime_flags = 0,  // cannot be unloaded
};

static mac_policy_handle_t nd_policy_handle;

void
nd_amfi_policy_init(void)
{
	decide_enforcement();
	int error = mac_policy_register(&nd_policy_conf, &nd_policy_handle, NULL);
	if (error != 0) {
		panic("ndamfi: cannot register the code-signing MAC policy: %d", error);
	}
}
