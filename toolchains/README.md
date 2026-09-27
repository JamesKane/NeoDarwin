<!-- SPDX-License-Identifier: BSD-2-Clause -->
# Toolchains

**Current state (Phase 0):** host builds use the Xcode toolchain selected by `xcode-select` through `apple_support` and `rules_swift`. Verified with Xcode 27.0 (Apple Swift 6.4, clang 2100.3) on macOS 27 arm64.

**Target state (P0-02):** one pinned tarball per host OS, `neodarwin-toolchain-<ver>`, built from `swiftlang/llvm-project`: clang, lld (`ld64.lld`, `lld-link`), llvm binutils, `swiftc` with Embedded Swift, `compiler-rt`. Registered here as `cc_toolchain` and Swift toolchains and selected by `--platforms`. Until it lands, NeoDarwin target platforms in `//platforms` have no toolchain and do not resolve.

## XNU builds

The XNU actions (`rules/xnu.bzl`) also use the host Xcode: its macOS SDK is the base of the build SDK and its clang compiles the kernel. They carry `requires-darwin` and `no-remote`. XNU's own warning list is tuned to Apple's internal compiler, so upstream code builds with `BUILD_WERROR=0`; NeoDarwin's own code keeps warnings as errors.

## Language-mode spelling for the pinned toolchain

`language-policy.md` states intent; the exact flags live here and change with toolchain bumps.

| Intent | Swift 6.4 spelling |
|---|---|
| Swift 6 language mode (strict concurrency on) | `-swift-version 6` |
| warnings are errors | `-warnings-as-errors` |
| Embedded Swift (T3) | `-enable-experimental-feature Embedded -wmo` |
| no heap allocation (T3) | `-no-allocations` |
