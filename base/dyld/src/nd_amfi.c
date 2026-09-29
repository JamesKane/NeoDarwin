// SPDX-License-Identifier: BSD-2-Clause
// NeoDarwin-Language: portability: a stand-in with the C interface of a closed Apple library.
//
// Stand-in for libamfi's amfi_check_dyld_policy_self(), which Apple's dyld
// links from a closed static archive. On Apple systems it asks the
// AppleMobileFileIntegrity policy (a MAC syscall) which dyld features the
// process may use. NeoDarwin's kernel has no AMFI dyld policy, and without
// an answer dyld would refuse @-paths and every DYLD_* variable. So the
// stand-in allows everything, as AMFI does for an unrestricted process when
// System Integrity Protection is off; a NeoDarwin policy replaces it when
// restricted (setuid, entitled) processes need their environment filtered.

#include <libamfi.h>

int
amfi_check_dyld_policy_self(uint64_t input_flags, uint64_t *output_flags)
{
	(void)input_flags;
	*output_flags = AMFI_DYLD_OUTPUT_ALLOW_AT_PATH | AMFI_DYLD_OUTPUT_ALLOW_PATH_VARS |
	    AMFI_DYLD_OUTPUT_ALLOW_CUSTOM_SHARED_CACHE | AMFI_DYLD_OUTPUT_ALLOW_FALLBACK_PATHS |
	    AMFI_DYLD_OUTPUT_ALLOW_PRINT_VARS | AMFI_DYLD_OUTPUT_ALLOW_FAILED_LIBRARY_INSERTION |
	    AMFI_DYLD_OUTPUT_ALLOW_LIBRARY_INTERPOSING | AMFI_DYLD_OUTPUT_ALLOW_EMBEDDED_VARS |
	    AMFI_DYLD_OUTPUT_ALLOW_DEVELOPMENT_VARS | AMFI_DYLD_OUTPUT_ALLOW_LIBSYSTEM_OVERRIDE;
	return 0;
}
