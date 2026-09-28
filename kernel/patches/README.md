<!-- SPDX-License-Identifier: BSD-2-Clause -->
# XNU patch series

Ordered patches applied to the pristine `xnu-12377.1.9` archive inside the build sandbox (`docs/repository.md` §3). A target lists the patches it applies in its `patches` attribute; nothing here edits a checkout. Applying 0001 to 0013 in order to the pristine archive must reproduce the tree the SBSA kernel is built from; each patch carries a rationale and a `Rebase-risk:` line.

| Patch | What it does | Used by |
|---|---|---|
| `0001-config-build-arm64-machine-code-and-pmap-from-source.patch` | enables `nos_arm_asm`/`nos_arm_pmap`, so the in-tree ARM64 machine code and pmap build instead of coming from Apple's closed per-SoC archive | `sbsa_release` |
| `0002-sbsa-board-config.patch` | adds `ARM64_BOARD_CONFIG_SBSA`: `SBSA.h`, `generic_arm64_common.h` (no `APPLE_ARM64_ARCH_FAMILY`), MakeInc and pexpert wiring | `sbsa_release` |
| `0003-osfmk-generic-arm64-guards.patch` | `GENERIC_ARM64_PLATFORM` guards in pmap, VM init, CPU exit, Apple CPU headers; empty tunables; unpublished `amcc_rorgn` sources behind a never-enabled option | `sbsa_release` |
| `0004-config-sbsa-exports.patch` | SBSA exports no Tightbeam or Apple-SoC-only symbols; all export consumers read a filtered `EXPORTS_DIR` | `sbsa_release` |
| `0005-bsd-build-ndamfi.patch` | adds ndamfi (`kernel/neodarwin/amfi`, overlaid at `bsd/ndamfi`) to bsd's file list: NeoDarwin's libTrustCache and the AMFI and Image4 interface providers (formerly the libTrustCache runtime alone, inside the patch) | `sbsa_release` |
| `0006-sbsa-no-apple-implementation-registers.patch` | SBSA builds no Apple performance-counter driver (`SOC_IS_GENERIC_ARM` in `doconf`, empty `CPU_COUNTERS_BASE`) and no AWL register writes | `sbsa_release` |
| `0007-kalloc-enforce-per-size-class-zone-limit.patch` | caps each kalloc_type size class at the 31 zones its stack array holds; upstream only asserts it, so a RELEASE kernel overran the array and panicked in `zone_set_sig_eq` (found at first boot, P1-03) | `sbsa_release` |
| `0008-libkern-build-ndcrypto.patch` | adds ndcrypto (`kernel/neodarwin/crypto`, overlaid at `libkern/ndcrypto` with the pinned FreeBSD sources) to libkern's file list, with its include roots and FreeBSD prelude | `sbsa_release` |
| `0009-iokit-build-neodarwin-platform.patch` | adds NeoDarwin's platform expert, GICv3 interrupt controller and PSCI power manager (`kernel/neodarwin/platform`, overlaid at `iokit/ndplatform`) to the arm64 IOKit file list, and a built-in personality matching the device tree root `NeoDarwin,sbsa` | `sbsa_release` |
| `0010-libkern-build-libpthread-kern.patch` | builds Apple's libpthread-539 `kern/` (APSL, overlaid at `libkern/ndpthread/libpthread`) and NeoDarwin's start glue (`kernel/neodarwin/pthread`) into libkern, with the kext's search paths, a compat `TargetConditionals.h` and libpthread's `current_uthread()` renamed | `sbsa_release` |
| `0011-mockfs-sbsa-root.patch` | builds mockfs into the SBSA kernel (a `FILESYS_ROOT` set under `SOC_IS_GENERIC_ARM`) and repairs its arm64 rot: nodes allocated as the pointer typedef, and the memory device mapped from a 32-bit, 4 KiB page count instead of `mdevgetrange()`'s address | `sbsa_release` |
| `0012-iokit-publish-hidden-media-without-iomedia.patch` | `publishHiddenMedia()` skips its walk when no IOMedia class is loaded; it dereferenced the NULL metaclass once IOFindBSDRoot chose md0, before any storage family exists (P1-07) | `sbsa_release` |
| `0013-mach-loader-static-pid1-on-sbsa.patch` | for `GENERIC_ARM64_PLATFORM`, process 1 may be a static executable, which RELEASE kernels otherwise refuse; the first PID 1 runs before the system has dyld (P1-07) | `sbsa_release` |
