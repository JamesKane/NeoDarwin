<!-- SPDX-License-Identifier: BSD-2-Clause -->
# kernel

XNU, built from Apple's published `xnu-12377.1.9` (macOS 26.0 release set) by the phase-1 wrapper rules in `rules/xnu.bzl`. Pins: `upstream.lock` and `MODULE.bazel`.

| Target | What it produces | Time |
|---|---|---|
| `//kernel:build_sdk` | NeoDarwin's additions to the host macOS SDK: `availability.pl` (AvailabilityVersions), the kernel firehose header (libdispatch), and the shims in `sdk/` | seconds |
| `//kernel:headers` | `make installhdrs`: `Kernel.framework` and `usr/local` headers | ~15 s |
| `//kernel:firehose_kernel` | `libfirehose_kernel.a`, built from libdispatch source | seconds |
| `//kernel:sbsa_release` | **`kernel.release.sbsa`**: NeoDarwin's generic Arm kernel, patches 0001–0005, plain `arm64`, BTI off; also the unstripped image and a build report | ~9.5 min |
| `//kernel:sbsa_kernel_test` | the kernel is a Mach-O 64-bit `arm64` executable with no undefined symbols (manual; kernel CI job) | seconds after the build |
| `//kernel:sbsa_sysreg_audit` | lists implementation-defined system-register accesses in the kernel; fails if any new one appears (manual; kernel CI job) | seconds after the build |
| `//kernel:vmapple_release_gaps` | Apple's public VMAPPLE config with no patches; the report of symbols its link lacks | ~9 min |
| `//kernel:vmapple_link_gap_ratchet` | fails if that gap grows (manual; kernel CI job) | seconds after the build |

## What the build established

**The SBSA kernel builds and links from public sources alone** (2026-09-27): `kernel.release.sbsa`, a 12.4 MB Mach-O `arm64` executable with no undefined symbols, from Apple's `xnu-12377.1.9` archive plus NeoDarwin's five patches and `sdk/` shims, without Apple's Kernel Debug Kit. It has not been booted: the loader (P1-03) does not exist yet.

The route there, measured on Apple's own public VMAPPLE configuration: every source compiles, but the link lacks 395 symbols (`link_gaps/vmapple_release.txt`), which Apple's build takes from a closed per-SoC archive in the KDK. They closed as follows.

| Gap | Symbols | How it closed |
|---|---|---|
| ARM64 machine code and pmap in the tree but excluded by the public config (`nos_arm_asm`, `nos_arm_pmap`) | about 237 | 0001 builds them; 0002 and 0003 supply the board config and guards they need |
| Required only by export lists: Tightbeam (111), Apple IOMMUs and `IOUnifiedAddressTranslator` (38), PPL monitor queries (2) | 150 at the end | 0004: SBSA exports none of them |
| Apple memory-controller read-only regions (`amcc_rorgn*`, sources unpublished) | 6 | 0003: not built for SBSA; SBSA has no `KERNEL_INTEGRITY_*` hardware |
| Unpublished Apple helpers called from shared code (HID-controlled cache clean, monitor enable) | 2 | 0003: architectural equivalents on generic hardware |
| libTrustCache runtime | 2 | 0005 |
| `libfirehose_kernel` built for the wrong architecture | 5 | a second firehose build for `arm64` |

## What is still unsafe to run

Linking is not the same as running. `sysreg_audit/sbsa_release.txt` lists every access to an implementation-defined system register (`S3_*_C15_*`), all of which fault on a generic core. There are 41, in three places: the performance-counter driver (`mt_*`, 35, Apple PMCs, reached at CPU bring-up), its hook in `sleh_fiq` (2), and AWL bookkeeping (`awl_*`, 4). P1-02 drives the list to zero, by disabling the counters on SBSA or driving the architectural PMUv3 instead. No `hvc` instructions remain: the Apple-hypervisor paths compiled away with `APPLEVIRTUALPLATFORM`.

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
