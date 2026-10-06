<!-- SPDX-License-Identifier: BSD-2-Clause -->
# ld64 from source

Apple's open-source ld64 links the NeoDarwin kernel and kexts (P0-06; `docs/architecture/build-system.md` §2.1). ld64.lld can't: it has no `-static` kernel, `-segment_order`, split-seg info or `-kext`. Userland and UEFI don't use this linker.

| | |
|---|---|
| Source | `apple-oss-distributions/ld64` tag `ld64-957.1` (APSL-2.0), the newest Apple publishes; `@apple_ld64` in `MODULE.bazel`, pinned in `upstream.lock` |
| Target | `//toolchains/ld64` (`rules/ld64.bzl`), a host tool, used in the exec configuration. The output is `ld`, and `VERSION.txt` in the `version` output group |
| Build | `build.sh` replays `ld64.xcodeproj`'s `ld` target (product `ld-classic`, Release): the target's 41 sources, three of them replaced (below), in C++20, `-O2 -DNDEBUG -DBUILDING_LD=1 -DLD_VERS="ld64-957.1"`. Its script phases are replayed as well: `src/create_configure` writes `configure.h` with Apple's architecture list and no riscv32, as in Xcode's toolchain, and `compile_stubs` becomes `compile_stubs.h`. Apple-generic versioning provides `ld_classicVersionString`. Phase 1 uses the host Xcode's clang and the public macOS SDK; P0-02 switches to the pinned toolchain. `SOURCE_DATE_EPOCH=0` fixes `-v`'s `BUILD` line |
| Smoke test | `//toolchains/ld64:ld64_smoke_test` checks three things: `-v` prints `PROJECT:ld64-957.1`, a static arm64 program links with `LC_UNIXTHREAD`, and a `.tbd` input is refused |
| Selected by | `--//rules:kernel_linker=ld64` (the default) for `xnu_kernel` (`rules/xnu.bzl`: XNU's `LD` becomes `$(KC++) --ld-path=…/ld -nostdlib`) and `nd_kext` (`rules/kext.bzl`: `ND_KEXT_LD`). `=xcode` selects the host Xcode's `ld` |

## What's left out, and why

| Dependency | Upstream use | Here |
|---|---|---|
| libtapi | reads `.tbd` text stubs | **Left out.** The kernel and kexts link no SDK stubs: XNU links `-nostdlib` with static archives, and kexts link `-kext` with no libraries. `compat/tapi/tapi.h` declares API version 1.0, which compiles ld64's inlined-framework code out. `nd_stubs.cpp` replaces `textstub_dylib_file.cpp` and refuses `.tbd` inputs with "built without libtapi". Apple publishes `tapi` (`apple-oss-distributions/tapi`), but it builds only inside an LLVM tree. P0-02 can add it if userland ever links with ld64 |
| libLTO | LTO, through `lto_file.cpp` | **Left out.** The kernel isn't LTO-built (`BUILD_LTO=0`). `nd_stubs.cpp` replaces `lto_file.cpp`: bitcode inputs fail, and `-lto_library`, which clang always passes, is accepted and ignored |
| libxar | bitcode bundles (`bitcode_bundle.cpp`) | **Left out**, because bitcode is gone from Apple's toolchains. `-bitcode_bundle` fails |
| libswiftDemangle | demangling in diagnostics | Left out (`configure.h` has no `DEMANGLE_SWIFT`) |
| internal-SDK headers | `corecrypto/`, `CommonCrypto/CommonDigestSPI.h`, `CrashReporterClient.h`, `os/lock_private.h`, `mach-o/dyld_priv.h`, `System/machine/cpu_capabilities.h`, `CoreAnalytics/` | `compat/` supplies the few declarations ld64 uses. The digests run over the public `CC_SHA1`/`CC_SHA256`, and the lock uses `os_unfair_lock` |

The `mach-o/*.h` headers come from the macOS SDK. cctools-port (tpoechtrager) was consulted for the list of private dependencies, and none of its code is used.

## Rebuild by hand

```sh
toolchains/ld64/build.sh OUT "$(bazel info output_base)/external/+http_archive+apple_ld64" 957.1
OUT/bin/ld -v      # @(#)PROGRAM:ld  PROJECT:ld64-957.1
```
