<!-- SPDX-License-Identifier: BSD-2-Clause -->
# Toolchains

**Current state (Phase 0):** host builds use the Xcode toolchain selected by `xcode-select` through `apple_support` and `rules_swift`. Verified with Xcode 27.0 (Apple Swift 6.4, clang 2100.3) on macOS 27 arm64.

**Target state (P0-02):** one pinned tarball per host OS, `neodarwin-toolchain-<ver>`, built from `swiftlang/llvm-project`: clang, lld (`ld64.lld`, `lld-link`), llvm binutils, `swiftc` with Embedded Swift, `compiler-rt`. Registered here as `cc_toolchain` and Swift toolchains and selected by `--platforms`. Until it lands, NeoDarwin target platforms in `//platforms` have no toolchain and do not resolve.

## Language-mode spelling for the pinned toolchain

`language-policy.md` states intent; the exact flags live here and change with toolchain bumps.

| Intent | Swift 6.4 spelling |
|---|---|
| Swift 6 language mode (strict concurrency on) | `-swift-version 6` |
| warnings are errors | `-warnings-as-errors` |
| Embedded Swift (T3) | `-enable-experimental-feature Embedded -wmo` |
| no heap allocation (T3) | `-no-allocations` |
