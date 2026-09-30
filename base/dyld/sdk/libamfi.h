/*
 * SPDX-License-Identifier: BSD-2-Clause
 * NeoDarwin-Language: expressibility: a C header standing in for one from Apple's internal SDK.
 *
 * Stands in for the internal SDK's <libamfi.h>, the user-side interface of
 * AppleMobileFileIntegrity (closed). dyld asks it once at launch which
 * dyld features the process may use: @-paths, DYLD_* variables, custom
 * shared caches, interposing. The flag names and output values are the ones
 * dyld's own sources spell out for builds without the header
 * (dyld/DyldProcessConfig.cpp); the two input values Apple doesn't publish
 * are NeoDarwin's, and only the stand-in reads them.
 *
 * The implementation (base/dyld/src/nd_amfi.c, linked into dyld) applies
 * AMFI's rule for restricted processes (setugid, __RESTRICT, CS_RESTRICT,
 * or entitled: no DYLD_* variables) and allows the rest everything, as AMFI
 * does for an unrestricted process on a system with SIP off (P1-15).
 */
#ifndef _LIBAMFI_H_
#define _LIBAMFI_H_

#include <stdint.h>
#include <sys/cdefs.h>

__BEGIN_DECLS

enum amfi_dyld_policy_input_flag_set {
	AMFI_DYLD_INPUT_PROC_IN_SIMULATOR     = (1 << 0),
	AMFI_DYLD_INPUT_PROC_HAS_RESTRICT_SEG = (1 << 1),
	AMFI_DYLD_INPUT_PROC_IS_ENCRYPTED     = (1 << 2),
};

enum amfi_dyld_policy_output_flag_set {
	AMFI_DYLD_OUTPUT_ALLOW_AT_PATH                  = (1 << 0),
	AMFI_DYLD_OUTPUT_ALLOW_PATH_VARS                = (1 << 1),
	AMFI_DYLD_OUTPUT_ALLOW_CUSTOM_SHARED_CACHE      = (1 << 2),
	AMFI_DYLD_OUTPUT_ALLOW_FALLBACK_PATHS           = (1 << 3),
	AMFI_DYLD_OUTPUT_ALLOW_PRINT_VARS               = (1 << 4),
	AMFI_DYLD_OUTPUT_ALLOW_FAILED_LIBRARY_INSERTION = (1 << 5),
	AMFI_DYLD_OUTPUT_ALLOW_LIBRARY_INTERPOSING      = (1 << 6),
	AMFI_DYLD_OUTPUT_ALLOW_EMBEDDED_VARS            = (1 << 7),
	AMFI_DYLD_OUTPUT_ALLOW_DEVELOPMENT_VARS         = (1 << 8),
	AMFI_DYLD_OUTPUT_ALLOW_LIBSYSTEM_OVERRIDE       = (1 << 9),
};

// Sets *output_flags to the amfi_dyld_policy_output_flag_set bits the calling
// process may use, given amfi_dyld_policy_input_flag_set bits describing it.
// Returns 0, or nonzero if the policy can't be read (dyld then allows nothing).
int amfi_check_dyld_policy_self(uint64_t input_flags, uint64_t *output_flags);

__END_DECLS

#endif /* _LIBAMFI_H_ */
