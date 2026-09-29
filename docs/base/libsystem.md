<!-- SPDX-License-Identifier: BSD-2-Clause -->
# libSystem from Apple source (P1-08b)

**Goal.** A dynamically linked arm64 hello world runs through dyld from the HFS+ root (`docs/kernel/arm64-sbsa-bringup.md` §2.1.4). Every image it needs (dyld, libdyld, libSystem.B and the `libsystem_*` libraries under it) is built from Apple's published source. The only exceptions are small stand-ins for components Apple doesn't publish.

This document is the plan and the record. Findings go into the tables as each checkpoint lands.

## 1. Components and pins

Every component comes from the macOS 26.0 release set (`distribution-macOS` tag `macos-260`), the same set as the kernel (`kernel/upstream.lock`), so private interfaces agree across images.

| Image | Install name | Project |
|---|---|---|
| `libsystem_kernel` (+ `libsystem_kernel.a` for dyld) | `/usr/lib/system/libsystem_kernel.dylib` | `xnu-12377.1.9` `libsyscall/` |
| `libsystem_platform` (+ dyld archive) | `/usr/lib/system/libsystem_platform.dylib` | `libplatform-359.1.2` |
| `libsystem_pthread` (+ `libpthread_dyld.a`) | `/usr/lib/system/libsystem_pthread.dylib` | `libpthread-539` |
| `libsystem_malloc` | `/usr/lib/system/libsystem_malloc.dylib` | `libmalloc-792.1.1` |
| `libsystem_c` (+ `libc_dyld.a`) | `/usr/lib/system/libsystem_c.dylib` | `Libc-1725.0.11` |
| `libsystem_blocks` | `/usr/lib/system/libsystem_blocks.dylib` | `libclosure-96` |
| `libobjc.A` | `/usr/lib/libobjc.A.dylib` | `objc4` (macOS 26.0 set), which libclosure links upward |
| `libsystem_info` | `/usr/lib/system/libsystem_info.dylib` | `Libinfo-600` |
| dyld, `libdyld` | `/usr/lib/dyld`, `/usr/lib/system/libdyld.dylib` | `dyld-1323.3` |
| `libSystem.B` | `/usr/lib/libSystem.B.dylib` | `Libsystem-1356` |

**Not published.** libSystem's initializer and Libc call into components Apple doesn't publish: libxpc, libsystem_trace (`os_log`), corecrypto, libsanitizers and libsystem_featureflags. `libcompiler_rt`, which libmalloc links, comes from LLVM's compiler-rt builtins rather than an Apple project. NeoDarwin builds a stand-in for each, with Apple's install name and only the symbols the open libraries use. The SDK's `.tbd` files record the real export lists. Each stand-in says what it replaces and what it does instead: no-op initializers, `os_log` that drops messages, feature flags that return their defaults. Replacing a stand-in with a real implementation is later work, done interface by interface.

## 2. How it builds

- **ABI.** Plain `arm64`, like the kernel: no arm64e pointer authentication.
- **Toolchain.** Phase 1 uses the host's Xcode `clang`, `ld`, `mig`, `dtrace` and `perl` against the public macOS SDK, as the kernel build does. The Xcode projects need Apple's internal SDK and `BSD.xcconfig`, so no `xcodebuild` is used.
- **Build scripts.** Each project has a script under `base/<project>/` that replays its Xcode target. The script uses the target's source list, defines and link flags, taken from the `.pbxproj` and xcconfigs and recorded in the script.
- **Headers.** They're staged once, in `//base:sysroot`, before any library builds, because the projects include each other's private headers (libplatform ↔ libpthread). The tree holds:
  - xnu's installed headers (`//kernel:headers`);
  - each project's installed private headers;
  - NeoDarwin's shims for internal-SDK headers (`base/sdk`).
- **Outputs.** Each library builds against the sysroot and the libraries it links. It produces an install tree (`usr/lib/system/…`, `usr/local/lib/dyld/…`), and the image rule merges the trees onto the root filesystem.
- **Pins.** Upstream archives are pinned in `MODULE.bazel` and `base/upstream.lock`. Changes to upstream files are numbered patches under `base/<project>/patches/` (`repository.md` §3).

## 3. Checkpoints

| # | Deliverable | Check | Status |
|---|---|---|---|
| 1 | `//base:sysroot`; `libsystem_kernel`, `libsystem_platform`, `libsystem_pthread`, `libsystem_malloc` with their dyld archives | each links with no undefined symbols outside its declared dependencies; `//base:*_exports_test` records each export list | done |
| 2 | `libsystem_c` and `libc.a` (dyld), `libsystem_blocks`, libobjc with the C++ runtime and libunwind it needs (LLVM), the other open libraries Libc imports from, and the stand-ins | same | doing: Libc and blocks done |
| 3 | dyld and `libdyld` | same; dyld's closed headers replaced by stand-ins, corecrypto's digests by a small SHA implementation | todo |
| 4 | `libSystem.B`; a hello world linked against it; an HFS+ image holding all of it | `//kernel:sbsa_boot_test` runs the hello world through dyld | todo |

## 4. Findings

### Checkpoint 1: the sysroot and the four kernel-facing libraries

`//base:sysroot` stages 7,600 headers:
- xnu's installed headers;
- Libsyscall's, installed by Apple's own `mach_install_mig.sh`;
- Libc's, installed by Apple's own `headers.sh`;
- libpthread's, libplatform's, libmalloc's and dyld's private headers;
- corecrypto's interface headers, which xnu publishes;
- dyld's version tables, generated from AvailabilityVersions-155;
- `base/sdk`'s shims.

The four libraries build in about 15 seconds. They link against each other; dependencies not yet built (libdyld, libmacho, libcompiler_rt, and the upward links to Libc, blocks, corecrypto and featureflags) link through the host SDK's `.tbd` stubs, which carry Apple's install names. Against the macOS 27 SDK's export lists:

| Library | Exports | Missing from Apple's list |
|---|---|---|
| `libsystem_kernel` | 1,539 | 79, nearly all newer than xnu-12377 (`pipe2`, `dup3`, the exclaves family, new spawn attributes) |
| `libsystem_platform` | 179 | `__sme_*` (SME string variants, called only from SME code) and two Swift/scripting hooks |
| `libsystem_pthread` | 207 | a legacy-mode switch and an AMX hint |
| `libsystem_malloc` | 115 | three test-only hooks |

| Finding | Resolution |
|---|---|
| xnu installs the private variants of shared headers into `System.framework/PrivateHeaders` | the sysroot's search order is PrivateHeaders, then `usr/local/include`, then `usr/include`, as in Apple's builds |
| Current clang makes `-Wint-conversion` an error | a warning, as in Apple's toolchain for this release (Libsyscall's xcconfig turns it off) |
| Apple's AvailabilityVersions-155 headers, private and public together, break under the host's newer clang (arity changes; `bridgeos` availability, which only Apple's internal clang accepts) | the host SDK's public availability headers, `base/sdk`'s `AvailabilityInternalPrivate.h` (SPI annotations expand to nothing), and the host SDK's public dyld headers. The dyld version tables are still generated from AvailabilityVersions-155 |
| `os/alloc_once_private.h` (slot numbers of `_os_alloc_once_table`), `os/feature_private.h`, `AppleFeatures/AppleFeatures.h`, and `os/thread_self_restrict.h` (published empty) aren't available | `base/sdk` shims. The alloc-once keys only have to be distinct across libraries NeoDarwin builds; feature flags answer with their defaults; generic Arm has no per-thread RWX switching |
| libplatform ships no Xcode project | its structure comes from its xcconfigs: eight sub-archives, `-all_load` into the dylib |
| libplatform's arm64 string assembly isn't published (`ffs`, `ffsl`, `fls`, `flsl`) | `base/libplatform/src/nd_bitops.c` |
| `_os_xbs_chrooted` is defined in libsystem_kernel but declared in no published header | libpthread patch 0001 |
| libmalloc's `nano_zone.h` stops with `#error` on arm64 (the v1 layout, which arm64 doesn't use) | libmalloc patch 0001 |
| libmalloc's `resolver_internal.h` is a stub, so nano v2 compiled to nothing | libmalloc patch 0002: an unresolved build defines both variant selectors |

### Checkpoint 2 so far: Libc and blocks

`libsystem_c` (1,325 exports; Apple's macOS 27 list has 1,323, and the 15 it lacks are newer APIs such as `scandirat` and the `$NFTS` fts variants) and `libsystem_blocks` (all 19 of Apple's exports) build. Libc's own imports list the rest of checkpoint 2:
- **Open, to build:** libdyld (12 symbols; checkpoint 3), libsystem_info (13), libsystem_asl (11), libdispatch (5), libsystem_notify (5), libsystem_m (4), libmacho (1), libcompiler_rt (1).
- **Closed, to stand in for:** libsystem_trace (3), corecrypto (2), libxpc (1).

libclosure is built in its macOS form, Objective-C included: `data.m`, upward `-lobjc`, `-lunwind`. So NeoDarwin builds Apple's objc4 as well, with the C++ runtime and libunwind it needs from LLVM. Upstream code in Objective-C sits within the language policy, which keeps a fourth language out of NeoDarwin's own code only.

| Finding | Resolution |
|---|---|
| Libc's archives are Xcode targets whose files carry per-file `COMPILER_FLAGS`: symbol aliases (`LIBC_ALIAS_*`), `-include gen/__dirent.h`, the db interface, uuid's `uuid-config.h`, the xprintf search path | `base/libc/sources/*.txt` record each target's files with their flags; the build compiles each flag group with its own flags |
| Variant archives compile the same file list with variant macros; on arm64 only Cancelable and DarwinExtsn have content | the xcconfigs' per-variant name lists pick the files; Legacy, Inode32, Pre1050 (i386/x86_64 only) and DarwinExtsn_Cancelable (no list) are left out |
| `<System/sys/fsctl.h>` is a framework include, but xnu installs System.framework without its `Versions/Current` and `PrivateHeaders` links, and a partial IOKit.framework that must not hide the SDK's | the sysroot adds the links and a framework directory holding only System.framework (`usr/local/frameworks`) |
| Libc's `os/assumes.c`, `arc4random.c` and `vfprintf.c` use libsystem_trace's private log interface (the log pack, `os_log_send_and_compose`) | `base/sdk`'s `os/log_private.h`; the libsystem_trace stand-in will implement the same calls |
| `membershipPriv.h` and the other Libinfo headers | Libinfo-600 is pinned and its `install_files.sh` runs in the sysroot stage |
| libclosure's `runtime.cpp` needs libc++'s headers ahead of the C headers | the SDK's `c++/v1` goes first for that build |
