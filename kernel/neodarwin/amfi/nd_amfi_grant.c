// SPDX-License-Identifier: BSD-2-Clause
// NeoDarwin-Language: portability: XNU's trust-cache hook and sysctl handlers are C kernel interfaces.
//
// Run-time trust caches (P2-01, docs/kernel/amfi-provider.md §4.5,
// docs/architecture/packaging.md §4): the grant verifier ndamfi registers with
// nd_tc_set_grant_verifier(), the package roots it trusts, and the user entry
// point that loads a module with its grant.
//
//  - Roots. The boot-arg nd_pkg_root=HEX[,HEX...] (at most ND_PKG_ROOTS
//    Ed25519 public keys, 64 lower-case hex digits each). None is compiled
//    in: without the boot-arg, or with a malformed one, every grant is
//    refused. A root passed this way is as trustworthy as the boot chain
//    that passed it (boot.cfg on the ESP; amfi-provider.md §4.6).
//  - The verifier. A load of type kTCTypeLTRS ("ltrs") whose manifest is an
//    ndsign grant, checked by nd_ndsign_verify_tc_grant() against the roots
//    at the calendar time. Any other type is refused. Ed25519's verification
//    (OpenSSH's ref10) keeps about 10 KiB of locals, too much on top of a
//    syscall's frames in a 16 KiB kernel stack, so the check runs on a
//    kernel thread of its own while the loader waits.
//  - The entry point. XNU has none: on Apple systems AMFI's user client
//    calls load_trust_cache_with_type(). Here a write-only sysctl,
//    security.codesigning.neodarwin.load_trust_cache, takes
//    `u32 module_len, u32 grant_len, module, grant` (little-endian lengths,
//    the bytes back to back) from root. load_trust_cache_with_type() checks
//    the caller's com.apple.private.pmap.load-trust-cache =
//    neodarwin.trust-cache.load entitlement itself. Errors: EPERM (not
//    entitled), EAUTH (the grant or module refused), EEXIST (already
//    loaded), EINVAL (malformed request).
//  - security.codesigning.neodarwin.pkg_roots reads the trusted roots back
//    (hex, comma-separated), so userland verifies packages against the keys
//    the kernel trusts.

#include <kern/clock.h>
#include <kern/kalloc.h>
#include <kern/locks.h>
#include <kern/sched_prim.h>
#include <kern/thread.h>
#include <libkern/section_keywords.h>
#include <pexpert/pexpert.h>
#include <sys/errno.h>
#include <sys/sysctl.h>
#include <sys/systm.h>
#include <sys/trust_caches.h>
#include "nd_amfi_internal.h"
#include "nd_ndsign.h"
#include "nd_trustcache.h"

#define ND_PKG_ROOTS 4
#define ND_PKG_ROOT_HEX 64
#define ND_TC_MAX_MODULE (1024 * 1024)

static SECURITY_READ_ONLY_LATE(uint8_t) nd_pkg_roots[ND_PKG_ROOTS * 32];
static SECURITY_READ_ONLY_LATE(unsigned int) nd_pkg_nroots;
static char nd_pkg_roots_hex[ND_PKG_ROOTS * (ND_PKG_ROOT_HEX + 1)];

// -- the roots: nd_pkg_root=HEX[,HEX...] ------------------------------------------

static void
read_roots(void)
{
	char arg[ND_PKG_ROOTS * (ND_PKG_ROOT_HEX + 1) + 2] = { 0 };
	if (!PE_parse_boot_arg_str("nd_pkg_root", arg, sizeof(arg))) {
		printf("ndamfi: no package root (nd_pkg_root): run-time trust caches are refused\n");
		return;
	}
	uint8_t roots[ND_PKG_ROOTS * 32];
	unsigned int n = 0;
	size_t at = 0, len = strnlen(arg, sizeof(arg));
	while (at <= len) {
		size_t end = at;
		while (end < len && arg[end] != ',') {
			end++;
		}
		if (n == ND_PKG_ROOTS || !nd_ndsign_unhex(roots + 32 * n, 32, arg + at, end - at)) {
			printf("ndamfi: nd_pkg_root is malformed (at most %d keys of %d lower-case hex digits, comma-separated): "
			    "run-time trust caches are refused\n", ND_PKG_ROOTS, ND_PKG_ROOT_HEX);
			return;
		}
		n++;
		at = end + 1;
	}
	memcpy(nd_pkg_roots, roots, sizeof(roots));
	nd_pkg_nroots = n;
	for (unsigned int i = 0; i < n; i++) {
		char *out = nd_pkg_roots_hex + i * (ND_PKG_ROOT_HEX + 1);
		nd_ndsign_hex(out, nd_pkg_roots + 32 * i, 32);
		if (i + 1 < n) {
			out[ND_PKG_ROOT_HEX] = ',';
		}
	}
	printf("ndamfi: %u package root%s from nd_pkg_root: %s\n", n, n == 1 ? "" : "s", nd_pkg_roots_hex);
}

// -- the verifier ------------------------------------------------------------------

struct grant_check {
	const uint8_t *module, *grant;
	size_t module_len, grant_len;
	uint64_t now;
	nd_ndsign_error_t result;
	bool done;
};

static LCK_GRP_DECLARE(nd_grant_lck_grp, "ndamfi.grant");
static LCK_MTX_DECLARE(nd_grant_lock, &nd_grant_lck_grp);

static void
grant_check_thread(void *arg, __unused wait_result_t wr)
{
	struct grant_check *c = arg;
	nd_ndsign_error_t r = nd_ndsign_verify_tc_grant(c->grant, c->grant_len, c->module, c->module_len, "ltrs",
	    nd_pkg_roots, nd_pkg_nroots, c->now);
	lck_mtx_lock(&nd_grant_lock);
	c->result = r;
	c->done = true;
	thread_wakeup(c);
	lck_mtx_unlock(&nd_grant_lock);
}

static bool
verify_grant(TCType_t type, const uint8_t *payload, size_t payloadSize, const uint8_t *manifest, size_t manifestSize)
{
	if (type != kTCTypeLTRS) {
		printf("ndamfi: trust-cache grant refused: type %u is not ltrs\n", type);
		return false;
	}
	if (nd_pkg_nroots == 0) {
		printf("ndamfi: trust-cache grant refused: no package root (nd_pkg_root)\n");
		return false;
	}
	clock_sec_t secs = 0;
	clock_usec_t usecs = 0;
	clock_get_calendar_microtime(&secs, &usecs);
	struct grant_check c = {
		.module = payload, .module_len = payloadSize, .grant = manifest, .grant_len = manifestSize,
		.now = (uint64_t)secs, .result = ND_NDSIGN_MALFORMED, .done = false,
	};
	thread_t thread = THREAD_NULL;
	if (kernel_thread_start(grant_check_thread, &c, &thread) != KERN_SUCCESS) {
		printf("ndamfi: trust-cache grant refused: no thread to check it on\n");
		return false;
	}
	thread_deallocate(thread);
	lck_mtx_lock(&nd_grant_lock);
	while (!c.done) {
		lck_mtx_sleep(&nd_grant_lock, LCK_SLEEP_DEFAULT, &c, THREAD_UNINT);
	}
	lck_mtx_unlock(&nd_grant_lock);
	if (c.result != ND_NDSIGN_OK) {
		printf("ndamfi: trust-cache grant refused: %s\n", nd_ndsign_error_name(c.result));
		return false;
	}
	printf("ndamfi: trust-cache grant accepted (%lu-byte module)\n", (unsigned long)payloadSize);
	return true;
}

// -- the entry point -------------------------------------------------------------------

SYSCTL_DECL(_security_codesigning_neodarwin);

static uint32_t
le32(const uint8_t *p)
{
	return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}

static int
sysctl_load_trust_cache SYSCTL_HANDLER_ARGS
{
#pragma unused(oidp, arg1, arg2)
	if (req->newptr == USER_ADDR_NULL) {
		return req->oldptr == USER_ADDR_NULL ? 0 : EPERM;  // write-only
	}
	size_t len = req->newlen;
	if (len < 8 || len > 8 + ND_TC_MAX_MODULE + ND_NDSIGN_MAX_BUNDLE) {
		return EINVAL;
	}
	uint8_t *buf = kalloc_data(len, Z_WAITOK);
	if (buf == NULL) {
		return ENOMEM;
	}
	int error = SYSCTL_IN(req, buf, len);
	if (error == 0) {
		size_t module_len = le32(buf), grant_len = le32(buf + 4);
		if (module_len == 0 || grant_len == 0 || module_len > ND_TC_MAX_MODULE ||
		    grant_len > ND_NDSIGN_MAX_BUNDLE || 8 + module_len + grant_len != len) {
			error = EINVAL;
		} else {
			kern_return_t kr = load_trust_cache_with_type(kTCTypeLTRS, buf + 8, module_len,
			    buf + 8 + module_len, grant_len, NULL, 0);
			switch (kr) {
			case KERN_SUCCESS: error = 0; break;
			case KERN_DENIED: error = EPERM; break;
			case KERN_ALREADY_IN_SET: error = EEXIST; break;
			case KERN_INVALID_ARGUMENT: error = EINVAL; break;
			case KERN_FAILURE: error = EAUTH; break;
			default: error = EIO; break;
			}
		}
	}
	kfree_data(buf, len);
	return error;
}

SYSCTL_PROC(_security_codesigning_neodarwin, OID_AUTO, load_trust_cache,
    CTLTYPE_OPAQUE | CTLFLAG_WR | CTLFLAG_LOCKED, NULL, 0, sysctl_load_trust_cache, "S",
    "load a run-time trust cache: u32 module_len, u32 grant_len, module, grant (root, entitled)");

SYSCTL_STRING(_security_codesigning_neodarwin, OID_AUTO, pkg_roots, CTLFLAG_RD | CTLFLAG_LOCKED,
    nd_pkg_roots_hex, 0, "package roots trusted for run-time trust caches (nd_pkg_root)");

void
nd_amfi_grant_init(void)
{
	read_roots();
	nd_tc_set_grant_verifier(verify_grant);
}
