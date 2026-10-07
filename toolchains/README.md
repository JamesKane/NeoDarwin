<!-- SPDX-License-Identifier: BSD-2-Clause -->
# Toolchains

**State (P0-02, 2026-10-06):** pinned prebuilt toolchains, by sha256, and a pinned macOS SDK (`upstream.lock`):

| | Release | Fetched by |
|---|---|---|
| Swift | swift.org **6.4.0** (`swift-6.4.0-RELEASE-osx.pkg`), the Swift of Xcode 27.0. Its Embedded stdlib covers `aarch64-none-none-elf` and `arm64-apple-macos`, and it ships `lld-link`, so one Swift serves hosted and Embedded builds | `nd_swift_toolchain` (`repos.bzl`): downloads the .pkg, expands it with `pkgutil --expand-full` (never installs it) into `@nd_swift//:swift-6.4.0-RELEASE.xctoolchain` |
| LLVM | llvm.org **23.1.3**, `LLVM-23.1.3-macOS-ARM64.tar.xz`: clang, `ld64.lld`, `lld-link`, llvm-ar, llvm-libtool-darwin, llvm-nm, llvm-objdump, llvm-install-name-tool, … Upstream lld accepts macOS objects; the swiftlang build's doesn't (`docs/architecture/build-system.md` §2.1.2) | `nd_llvm_toolchain`: `download_and_extract` into `@nd_llvm` |
| macOS SDK | **27.0**, the Command Line Tools' `MacOSX27.0.sdk`, pinned in place (it can't be redistributed) | `nd_macos_sdk`: finds `/Library/Developer/CommandLineTools/SDKs/MacOSX27.0.sdk` (or `ND_MACOS_SDK`; `xcrun --show-sdk-path` only as a fallback when that directory is missing), checks `SDKSettings.json`'s `Version`, then `sdk/sdk_hash.sh`'s content hash: the SHA-256 of a manifest of every file's SHA-256 and every symlink's target, sorted by path. A mismatch fails the fetch, naming the SDK found, the one wanted and how to get it. The repository is `local`, so the check runs again whenever Bazel refetches (each server start, about 15 s) |

**Selecting it.** `--config=pinned` (`.bazelrc`) registers `@nd_llvm//:cc_toolchain` and `@nd_swift//:swift_toolchain` ahead of apple_support's and rules_swift's Xcode toolchains, and sets `--macos_minimum_os=26.0`. Without it, hosted C and Swift still build with the selected Xcode (`apple_support`, `rules_swift`); so do the base, XNU and the kexts, which move in P2-12. Embedded Swift always uses the pinned toolchain.

- **cc_toolchain** (`llvm.BUILD.tpl`): rules_cc's Unix toolchain config for `arm64-apple-macos`, clang with `-isysroot` (and `--sysroot`) set to the pinned SDK, `llvm-libtool-darwin` for static libraries.
- **Swift toolchain** (`swift.BUILD.tpl`): rules_swift's generic `swift_toolchain` with `swift_tools`, so the driver and the toolchain's files are action inputs. `-sdk` is the cc_toolchain's sysroot. Its `os = "none"` adds no runtime link flags (rules_swift's are Linux's); the cc_toolchain adds `-L$SDK/usr/lib/swift` and `-rpath /usr/lib/swift`, the OS's Swift runtime. Two adaptations: `ld64_lld.sh` turns rules_swift's GNU `--defsym main=M_main` into Mach-O's `-alias _M_main _main`, and `patches/rules_swift_worker_no_xcrun.patch` stops rules_swift's worker from running an execroot-relative (hermetic) driver through `/usr/bin/xcrun`, which needs a developer directory.

### Linking hosted programs: ld64.lld, for now

Hosted programs link against the SDK's `.tbd` stubs (`libSystem.tbd` and the rest). The from-source ld64 (`ld64/`) is built without libtapi and refuses `.tbd` inputs, so it can't link even a hello world. libtapi, from `apple-oss-distributions/tapi`, builds only inside an LLVM tree, which belongs with moving the base off Xcode (P2-12). Until then the pinned cc_toolchain links with llvm.org's `ld64.lld` (`-fuse-ld=lld`), which reads `.tbd` v5 natively and signs arm64 output ad hoc. This is an interim: the decided linker for the base stays ld64 with libtapi (`docs/architecture/build-system.md`, "Toolchain"). Static links, which read no stubs, already use ld64: `static_macho` (below), the kernel and the kexts.

### No Xcode

`ci/no_xcode.sh` is the P0-02 exit check. It runs Bazel from `env -i` with `DEVELOPER_DIR` set to an empty directory (so `/usr/bin/xcrun` and every `/usr/bin` developer-tool shim fail), passes the same through `--repo_env`, `--action_env` and `--host_action_env`, and uses its own output base (`~/Library/Caches/bazel/neodarwin-no-xcode`), so every repository is fetched again in that environment (downloads come from the repository cache). Then:

1. `bazel test --config=pinned` builds and runs `//toolchains:hello_cc`, `:hello_swift` and `//tests/smoke:smoke`;
2. `--repo_env=ND_MACOS_SDK=<a fake 27.0 SDK>` fails at fetch with the SDK-mismatch error;
3. the same build without `--config=pinned` (the Xcode toolchain, through xcrun) fails.

The first run caught one real dependency on Xcode, rules_swift's worker running `swiftc` through xcrun, now patched out.

## Embedded Swift (language policy T3)

Embedded builds use the pinned swift.org toolchain: `@nd_embedded_swift` (`embedded/repo.bzl`) exports `@nd_swift`'s toolchain root as `EMBEDDED_TOOLCHAIN` after checking it has the `aarch64-none-none-elf` and `arm64-apple-macos` Embedded stdlibs and `lld-link`. `ND_EMBEDDED_TOOLCHAIN=<an .xctoolchain>` overrides it, for trying another Swift. swiftly is no longer needed. Swift 6.4.0 replaced swift.org 6.3.2 (installed by swiftly) here on 2026-10-06.

UEFI images are PE32+, but the Embedded stdlib ships only for ELF-style triples. `rules/efi.bzl` therefore builds in three steps:

| Step | Tool | Output |
|---|---|---|
| Swift for `aarch64-none-none-elf`, `-enable-experimental-feature Embedded -no-allocations`, stack protector disabled in the frontend | `swiftc` | LLVM IR (textual: 6.4's bitcode writer crashes on an unnamed private function neoboot produces, "Unexpected anonymous function when writing summary") |
| retarget to `aarch64-unknown-windows-msvc` | `clang` | COFF object |
| `/subsystem:efi_application /nodefaultlib` | `lld-link` | PE32+ EFI application |

This is sound on AArch64 because UEFI uses the standard AAPCS64 calling convention there, the same as the ELF target, so Swift calls firmware function pointers directly. It would not be on x86-64, where UEFI uses the Microsoft x64 convention. The Swift stack protector is disabled because the Windows backend implements it with MSVC `/GS` cookies (`__security_cookie`), which do not exist in firmware; compiler-emitted `memset`/`memcpy`/`memmove` come from a small `-fno-builtin` C file.

## Static Darwin executables

`rules/static_macho.bzl` builds programs that run before NeoDarwin has dyld or libSystem, such as the first PID 1 (`tests/qemu/pid1`). The Embedded stdlib also ships for `arm64-apple-macos`, so Swift and C compile directly for `arm64-apple-macos26.0` to Mach-O objects. There is no IR step: ELF-triple IR can't be lowered to Mach-O, because its sections are named ELF-style. The from-source ld64 (`ld64/`) then links with `-static -dead_strip -adhoc_codesign`; a static link reads no `.tbd` stubs, so ld64 needs no libtapi for it. The result is an `MH_EXECUTE` with an `LC_UNIXTHREAD` entry and a linker-signed ad hoc signature. No `ld64.lld` can do this: it doesn't implement `-static` (it warns and ignores it) and emits no `LC_UNIXTHREAD`. A small `-fno-builtin` C file supplies `memset`, `memcpy`, `memmove` and `bzero`.

## XNU builds

The XNU actions (`rules/xnu.bzl`) also use the host Xcode: its macOS SDK is the base of the build SDK and its clang compiles the kernel. They carry `requires-darwin` and `no-remote`. XNU's own warning list is tuned to Apple's internal compiler, so upstream code builds with `BUILD_WERROR=0`; NeoDarwin's own code keeps warnings as errors.

Apple's open-source ld64 (ld64-957.1), built from source by `//toolchains/ld64`, links the kernel and the kexts (P0-06; `ld64/README.md`). It is a host tool compiled with Xcode's clang, without libtapi, libLTO or bitcode support. `--//rules:kernel_linker=xcode` links them with Xcode's `ld` instead. `--//rules:kernel_linker=lld` links the kernel with an upstream `ld64.lld` (`--//rules:ld64_lld`, default Homebrew's `lld@22`). That was the first P0-06 experiment, and its output isn't a kernel kcgen accepts: `docs/architecture/build-system.md` §2.1.2 lists the gaps. The swift.org toolchain's `ld64.lld` can't link the kernel at all. Its swiftlang build rejects every object whose `LC_BUILD_VERSION` names macOS ("This version of lld does not support linking for platform macOS"). llvm.org's `ld64.lld` (`@nd_llvm`) accepts them, but can't link a kernel kcgen accepts. The base userland still links with Xcode's `ld` (P2-12).

## Language-mode spelling for the pinned toolchain

`language-policy.md` states intent; the exact flags live here and change with toolchain bumps.

| Intent | Swift 6.4 spelling (Xcode 27.0 and swift.org 6.4.0) |
|---|---|
| Swift 6 language mode (strict concurrency on) | `-swift-version 6` |
| warnings are errors | `-warnings-as-errors` |
| Embedded Swift (T3) | `-enable-experimental-feature Embedded -wmo` (swift.org toolchain; Xcode has no Embedded stdlib) |
| no heap allocation (T3) | `-no-allocations` (rejects, among others, closures that capture mutable locals) |
| no stack protector (UEFI) | `-Xfrontend -disable-stack-protector` |
| T2 entry point: no lock, allocation, reference counting or metadata use | `@_noLocks` (`@_noAllocation` is weaker: it still allows reference counting). No flag is needed: the performance diagnostics run in every compile, `-Onone` included, and they are errors (for example `this code performs reference counting operations which can cause locking`, or `Using type 'X' can cause metadata allocation or locks`). The spelling is the same in Xcode's 6.4 and swift.org 6.4.0 |
| T2 module-wide second check | the T3 Embedded flags with `-no-allocations`, on the swift.org toolchain (`tools/t2check/t2check.sh`; its hosted compile also uses the pinned swiftc and SDK) |
| T3 in a kext (kext_swift) | `-target arm64-apple-macos26.0 -target-cpu cortex-a76` plus the Embedded flags; KPI headers through `-Xcc -nostdinc -Xcc -mkernel -Xcc -DKERNEL`; link with `ld -kext -dead_strip -unexported_symbol '_swift_*' -unexported_symbol '__swift_*' -unexported_symbol '_$e*'` (`kexts/swift_trial/kext.sh`) |
