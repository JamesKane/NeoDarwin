<!-- SPDX-License-Identifier: BSD-2-Clause -->
# kernel

XNU, built from Apple's published `xnu-12377.1.9` (macOS 26.0 release set) by the phase-1 wrapper rules in `rules/xnu.bzl`. Pins: `upstream.lock` and `MODULE.bazel`.

| Target | What it produces | Time |
|---|---|---|
| `//kernel:build_sdk` | NeoDarwin's additions to the host macOS SDK: `availability.pl` (AvailabilityVersions), the kernel firehose header (libdispatch), and the shims in `sdk/` | seconds |
| `//kernel:headers` | `make installhdrs`: `Kernel.framework` and `usr/local` headers | ~15 s |
| `//kernel:firehose_kernel` | `libfirehose_kernel.a`, built from libdispatch source | seconds |
| `//kernel:vmapple_release_gaps` | full VMAPPLE RELEASE build; the report of symbols the link still lacks | ~9 min |
| `//kernel:vmapple_link_gap_ratchet` | fails if the link gap grows (manual; kernel CI job) | seconds after the build |

## What the build established

Apple's open-source XNU compiles completely from public sources plus NeoDarwin's shims, without Apple's Kernel Debug Kit. It does not link: `link_gaps/vmapple_release.txt` lists the 395 missing symbols.

| Gap | Symbols | Path to zero |
|---|---|---|
| ARM64 machine code and pmap in the tree but excluded by the public config (`nos_arm_asm`, `nos_arm_pmap`) | 230 + about 7 | `patches/0001`, then SBSA board-config guards (P1-01) |
| Required only by export lists: Tightbeam (111), `IOUnifiedAddressTranslator` (19) and others | 157 total, overlapping the rows above and below | drop the exclaves and Apple-IOMMU export lists for NeoDarwin (P1-02) |
| Apple SoC IOMMUs (DART, SART, UAT), kernel-integrity regions (`rorgn_*`), NVMe PPL | 26 | not built for SBSA (P1-02) |
| libTrustCache runtime (`trustCacheInitializeRuntime`, `TCTypeConfig`) | 2 | NeoDarwin stub alongside `sdk/usr/local/include/TrustCache/API.h` |

## `sdk/`: NeoDarwin shims

Headers XNU includes from Apple's internal SDK, written by NeoDarwin from XNU's own use of the names. Each carries a `NeoDarwin-Language` justification and a note on what it stands in for: `iBoot/boot_args_abi.h`, `TrustCache/API.h`, `CoreEntitlements/V2/{API,Kernel}.h`, `CodeSignature/Entitlements.h`, `AppleFeatures.h`.

## Build settings that differ from Apple's

| Setting | Value | Why |
|---|---|---|
| `RC_DARWIN_KERNEL_VERSION` | `25.0.0` | Apple's build derives it from an internal SDK; this is the macOS 26.0 Darwin version |
| `BUILD_WERROR` | `0` | XNU's warning list is tuned to Apple's internal compiler; Xcode 27's clang raises new `-Weverything` warnings (mainly `-Wreserved-identifier`). Revisit with the pinned toolchain |
| `BUILD_LTO` | `0` | faster iteration while bringing up; revisit for release |
| SDK variables | passed explicitly | `xcrun` cannot resolve an out-of-tree SDK, so `SDKROOT_RESOLVED`, `SDKVERSION`, `PLATFORM`, `PLATFORMPATH` are set by `tools/xnu/common.sh` |
| `LDFLAGS_KERNEL_SDK` | the action's own library directory | links the from-source `libfirehose_kernel.a` |
