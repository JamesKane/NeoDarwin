// SPDX-License-Identifier: BSD-2-Clause
// NeoDarwin-Language: expressibility: C header consumed by XNU's pexpert/arm64/boot.h; Apple's iBoot header is not published.
//
// NeoDarwin owns the loader (neoboot), so the loader-to-kernel command-line
// length is NeoDarwin's ABI choice, recorded in docs/kernel/arm64-sbsa-bringup.md
// §2.1. 1024 matches the value XNU itself uses for simulator targets.
#ifndef ND_IBOOT_BOOT_ARGS_ABI_H
#define ND_IBOOT_BOOT_ARGS_ABI_H

#define IBOOT_MAX_ENV_VAR_DATA_SIZE 1024

#endif
