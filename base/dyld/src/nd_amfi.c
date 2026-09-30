// SPDX-License-Identifier: BSD-2-Clause
// NeoDarwin-Language: portability: a stand-in with the C interface of a closed Apple library.
//
// Stand-in for libamfi's amfi_check_dyld_policy_self(), which Apple's dyld
// links from a closed static archive. On Apple systems it asks the
// AppleMobileFileIntegrity policy (a MAC syscall) which dyld features the
// process may use. NeoDarwin's kernel policy (ndamfi, P1-15,
// docs/kernel/amfi-provider.md) decides what code may run: under
// enforcement the kernel refuses any executable or library no trust cache
// lists, whatever dyld is told. This stand-in decides the rest, from what
// the kernel reports, with AMFI's rule for restricted processes: a process
// is restricted when it is setuid or setgid (issetugid), has a __RESTRICT
// segment or CS_RESTRICT, or is entitled: csops reports an XML
// entitlements blob (CS_OPS_ENTITLEMENTS_BLOB, which the kernel answers only
// for a trusted, entitled binary) or a DER one (CS_OPS_DER_ENTITLEMENTS_BLOB,
// the signature's own, trusted or not). A restricted process gets no DYLD_* variables (paths, printing,
// insertion, custom shared caches, development and embedded variables); it
// keeps @-paths, the classic fallback paths and interposing by the
// libraries it links. Any other process may use everything, as AMFI allows
// an unrestricted process with SIP off.

#include <libamfi.h>
#include <stddef.h>
#include <unistd.h>

// <sys/codesign.h> (XNU's bsd/sys/codesign.h), not in the public SDK;
// libsystem_kernel.a, which dyld links, has the call.
int csops(pid_t pid, unsigned int ops, void *useraddr, size_t usersize);
#define CS_OPS_STATUS            0
#define CS_OPS_ENTITLEMENTS_BLOB 7
#define CS_OPS_DER_ENTITLEMENTS_BLOB 16
#define CS_RESTRICT              0x00000800

static int
restricted(uint64_t input_flags)
{
	if (issetugid() || (input_flags & AMFI_DYLD_INPUT_PROC_HAS_RESTRICT_SEG) != 0) {
		return 1;
	}
	uint32_t status = 0;
	if (csops(getpid(), CS_OPS_STATUS, &status, sizeof(status)) != 0 || (status & CS_RESTRICT) != 0) {
		return 1;
	}
	// A blob header's worth: csops copies nothing and succeeds when there is
	// no blob, and fails (ERANGE) when there is one, since it doesn't fit.
	// Any failure counts as restricted.
	static const unsigned ops[] = { CS_OPS_ENTITLEMENTS_BLOB, CS_OPS_DER_ENTITLEMENTS_BLOB };
	for (unsigned i = 0; i < sizeof(ops) / sizeof(ops[0]); i++) {
		uint32_t header[2] = { 0, 0 };
		if (csops(getpid(), ops[i], header, sizeof(header)) != 0 || header[1] != 0) {
			return 1;
		}
	}
	return 0;
}

int
amfi_check_dyld_policy_self(uint64_t input_flags, uint64_t *output_flags)
{
	if (restricted(input_flags)) {
		*output_flags = AMFI_DYLD_OUTPUT_ALLOW_AT_PATH | AMFI_DYLD_OUTPUT_ALLOW_FALLBACK_PATHS |
		    AMFI_DYLD_OUTPUT_ALLOW_LIBRARY_INTERPOSING;
		return 0;
	}
	*output_flags = AMFI_DYLD_OUTPUT_ALLOW_AT_PATH | AMFI_DYLD_OUTPUT_ALLOW_PATH_VARS |
	    AMFI_DYLD_OUTPUT_ALLOW_CUSTOM_SHARED_CACHE | AMFI_DYLD_OUTPUT_ALLOW_FALLBACK_PATHS |
	    AMFI_DYLD_OUTPUT_ALLOW_PRINT_VARS | AMFI_DYLD_OUTPUT_ALLOW_FAILED_LIBRARY_INSERTION |
	    AMFI_DYLD_OUTPUT_ALLOW_LIBRARY_INTERPOSING | AMFI_DYLD_OUTPUT_ALLOW_EMBEDDED_VARS |
	    AMFI_DYLD_OUTPUT_ALLOW_DEVELOPMENT_VARS | AMFI_DYLD_OUTPUT_ALLOW_LIBSYSTEM_OVERRIDE;
	return 0;
}
