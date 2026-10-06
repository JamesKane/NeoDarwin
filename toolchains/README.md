<!-- SPDX-License-Identifier: BSD-2-Clause -->
# Toolchains

**Current state (Phase 0):** host builds use the Xcode toolchain selected by `xcode-select` through `apple_support` and `rules_swift`. Verified with Xcode 27.0 (Apple Swift 6.4, clang 2100.3) on macOS 27 arm64.

**Target state (P0-02):** one pinned tarball per host OS, `neodarwin-toolchain-<ver>`, built from `swiftlang/llvm-project`: clang, lld (`ld64.lld`, `lld-link`), llvm binutils, `swiftc` with Embedded Swift, `compiler-rt`. Registered here as `cc_toolchain` and Swift toolchains and selected by `--platforms`. Until it lands, NeoDarwin target platforms in `//platforms` have no toolchain and do not resolve.

## Embedded Swift (language policy T3)

Xcode's toolchain ships no Embedded Swift standard library, so Embedded builds use a swift.org toolchain installed with swiftly (`swiftly install 6.3.2`). `//toolchains/embedded:repo.bzl` locates it at fetch time (newest `swift-6.3*` toolchain with the `aarch64-none-none-elf` Embedded stdlib and `lld-link`; override with `ND_EMBEDDED_TOOLCHAIN`). Verified with swift-6.3.2-RELEASE.

UEFI images are PE32+, but the Embedded stdlib ships only for ELF-style triples. `rules/efi.bzl` therefore builds in three steps:

| Step | Tool | Output |
|---|---|---|
| Swift for `aarch64-none-none-elf`, `-enable-experimental-feature Embedded -no-allocations`, stack protector disabled in the frontend | `swiftc` | LLVM bitcode |
| retarget to `aarch64-unknown-windows-msvc` | `clang` | COFF object |
| `/subsystem:efi_application /nodefaultlib` | `lld-link` | PE32+ EFI application |

This is sound on AArch64 because UEFI uses the standard AAPCS64 calling convention there, the same as the ELF target, so Swift calls firmware function pointers directly. It would not be on x86-64, where UEFI uses the Microsoft x64 convention. The Swift stack protector is disabled because the Windows backend implements it with MSVC `/GS` cookies (`__security_cookie`), which do not exist in firmware; compiler-emitted `memset`/`memcpy`/`memmove` come from a small `-fno-builtin` C file.

## Static Darwin executables

`rules/static_macho.bzl` builds programs that run before NeoDarwin has dyld or libSystem, such as the first PID 1 (`tests/qemu/pid1`). The Embedded stdlib also ships for `arm64-apple-macos`, so Swift and C compile directly for `arm64-apple-macos26.0` to Mach-O objects. There is no bitcode step: ELF-triple bitcode can't be lowered to Mach-O, because its sections are named ELF-style. Xcode's `ld` then links with `-static -dead_strip -adhoc_codesign`. The result is an `MH_EXECUTE` with an `LC_UNIXTHREAD` entry and a linker-signed ad hoc signature. The swift.org toolchain's `ld64.lld` can't do this: it doesn't implement `-static` (it warns and ignores it) and emits no `LC_UNIXTHREAD`. A small `-fno-builtin` C file supplies `memset`, `memcpy`, `memmove` and `bzero`.

## XNU builds

The XNU actions (`rules/xnu.bzl`) also use the host Xcode: its macOS SDK is the base of the build SDK and its clang compiles the kernel. They carry `requires-darwin` and `no-remote`. XNU's own warning list is tuned to Apple's internal compiler, so upstream code builds with `BUILD_WERROR=0`; NeoDarwin's own code keeps warnings as errors.

## Language-mode spelling for the pinned toolchain

`language-policy.md` states intent; the exact flags live here and change with toolchain bumps.

| Intent | Swift 6.4 spelling |
|---|---|
| Swift 6 language mode (strict concurrency on) | `-swift-version 6` |
| warnings are errors | `-warnings-as-errors` |
| Embedded Swift (T3) | `-enable-experimental-feature Embedded -wmo` (swift.org toolchain; Xcode has no Embedded stdlib) |
| no heap allocation (T3) | `-no-allocations` (rejects, among others, closures that capture mutable locals) |
| no stack protector (UEFI) | `-Xfrontend -disable-stack-protector` |
| T2 entry point: no lock, allocation, reference counting or metadata use | `@_noLocks` (`@_noAllocation` is weaker: it still allows reference counting). No flag is needed: the performance diagnostics run in every compile, `-Onone` included, and they are errors (for example `this code performs reference counting operations which can cause locking`, or `Using type 'X' can cause metadata allocation or locks`). The spelling is the same in Xcode's 6.4 and swift.org 6.3.2 |
| T2 module-wide second check | the T3 Embedded flags with `-no-allocations`, on the swift.org toolchain (`tools/t2check/t2check.sh`) |
| T3 in a kext (kext_swift) | `-target arm64-apple-macos26.0 -target-cpu cortex-a76` plus the Embedded flags; KPI headers through `-Xcc -nostdinc -Xcc -mkernel -Xcc -DKERNEL`; link with `ld -kext -dead_strip -unexported_symbol '_swift_*' -unexported_symbol '__swift_*' -unexported_symbol '_$e*'` (`kexts/swift_trial/kext.sh`) |
