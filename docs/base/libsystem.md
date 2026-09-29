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
| `libsystem_asl` | `/usr/lib/system/libsystem_asl.dylib` | `syslog-404` |
| `libsystem_notify` | `/usr/lib/system/libsystem_notify.dylib` | `Libnotify-344.0.1` |
| `libdispatch` | `/usr/lib/system/libdispatch.dylib` | `libdispatch-1542.0.4` |
| `libmacho` | `/usr/lib/system/libmacho.dylib` | `cctools-1035.1.102` (a toolchain project, outside the release set; macOS 26's is 1040) |
| `libsystem_m` | `/usr/lib/system/libsystem_m.dylib` | FreeBSD `lib/msun` at the ndcrypto commit (Apple's Libm is unpublished since 2012) |
| `libcompiler_rt`, `libunwind`, `libc++abi`, `libc++.1` | `/usr/lib/system/…`, `/usr/lib/…` | Apple's LLVM fork, `swiftlang/llvm-project` `swift-6.2-RELEASE` (LLVM 19.1.5, Xcode 26's) |
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
| 2 | `libsystem_c` and `libc.a` (dyld), `libsystem_blocks`, libobjc with the C++ runtime and libunwind it needs (LLVM), the other open libraries Libc imports from, and the stand-ins | same | done |
| 3 | dyld and `libdyld` | same; dyld's closed headers replaced by stand-ins, corecrypto's digests by a small SHA implementation | done |
| 4 | `libSystem.B`; a hello world linked against it; an HFS+ image holding all of it | `//kernel:sbsa_dyld_boot_test` runs the hello world through dyld | done |

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
| libclosure's `runtime.cpp` needs libc++'s headers ahead of the C headers | the sysroot's `c++/v1` (LLVM 19, the libc++ NeoDarwin builds) goes first, with `-nostdinc++` |

### Checkpoint 2 continued: the other open libraries, libobjc and LLVM's runtimes

Built, each with an export test:

| Library | Exports | Apple's (macOS 27 SDK) | Gaps |
|---|---|---|---|
| `libmacho` | 90 | 90 | none |
| `libsystem_asl` | 219 | 219 | none |
| `libsystem_notify` | 23 | 23 | none |
| `libsystem_info` | 428 | 431 | three Open Directory entry points, left out on purpose |
| `libdispatch` | 411 | 413 | two newer than 1542 |
| `libobjc.A` (+ `libobjc-trampolines`) | 437 | 445 | eight newer than objc4-950 |
| `libsystem_m` | 335 | 444 | Apple's SIMD and matrix-inverse extras and `__exp10`; all of C99/C11 math and fenv |
| `libcompiler_rt` | 88 + 318 `$ld$hide` | 74 + 318 | none; LLVM 19's extra atomics |
| `libunwind` | 42 | 44 | `unw_strerror` (macOS 27), `unw_resume_with_frames_walked` (Apple-only) |
| `libc++abi` | 368 | 388 | Apple's typed `operator new`/`delete` extension |
| `libc++.1` | 1,899 | 1,973 | LLVM 20–21 additions (the SDK is newer than macOS 26.0) |

At this stage the stand-ins were: libcorecrypto (`ccrng`), libxpc (`bootstrap_parent`), libsystem_trace (the three log calls Libc uses) and libsystem_sandbox (`sandbox_check`, always allowed).

| Finding | Resolution |
|---|---|
| **objc4 doesn't build for plain arm64 on macOS.** Apple ships macOS libobjc as arm64e only, and non-ptrauth arm64 gets iOS's narrow isa class field, failing the address-space static assert | objc4 patch 0001 selects the 52-bit layout the arm64 simulator uses; it has never shipped, so it must be tested on the kernel. Patch 0002 hooks Rosetta off |
| Libinfo's macOS configuration looks users up through Open Directory (closed daemon, unpublished `odipc.h`) and libsystem_darwindirectory (closed) | both left out (`DS_AVAILABLE`, `DARWIN_DIRECTORY_AVAILABLE`); membership takes Apple's iOS path, and lookups use the cache, mDNS and file modules |
| libdispatch's `libdispatch.xcconfig` is published without settings since 1271; `internal.h` stops at an `__OPEN_SOURCE__` version check | the settings of 1173.100.2, the last full one; `DISPATCH_SEND_ACTIVITY_IN_MSGV=1` (macOS 13+). Patch 0001 declares the observer-hooks type before use |
| xnu publishes neither the work-interval instance API nor its libsystem_kernel implementation, which `os_workgroup_interval` uses | `base/libdispatch/sdk` declares it; `nd_work_interval_instance.c` implements it, hidden, on `work_interval_notify` |
| libxpc's `xpc/private.h` and launchd's `bootstrap_priv.h` aren't published; asl, notify and info use XPC pipes, event publishing and `bootstrap_look_up2` | `base/sdk` headers; bootstrap flag values are launchd-842's, since NeoDarwin ports that launchd |
| asl routes syslog(3) to os_log (`os_log_shim_enabled`, `os_log_with_args_4syslog`) | declared in `base/sdk`'s `os/log_private.h` |
| `CrashReporterClient.h` is a header over a static archive's `gCRAnnotations`; objc4 calls `sandbox_check` | `base/sdk` defines `gCRAnnotations` weak and hidden in `__DATA,__crash_info` (no archive); `sandbox/private.h` and the libsystem_sandbox stand-in |
| `___chkstk_darwin` isn't in compiler-rt | `base/llvm/src/nd_chkstk_darwin.c` branches to libpthread's published `thread_chkstk_darwin`, as Apple's libcompiler_rt links libsystem_pthread |
| Darwin's arm64 `fenv_t` is `{fpsr, fpcr}`; FreeBSD packs both into one word, and its classification constants differ | `base/libm/src/nd_fenv.c` implements Darwin's layout; classification compiles against Darwin's `math.h`. msun patch 0001 fixes `sinpi`/`cospi` signs where arm64's `fcvtzu` saturates |
| Source trees reach build actions as symlinks inside Bazel's sandbox | the sysroot stage copies with `cp -RL` and `find -L`, so it holds files |

### Checkpoint 2 closed: Libc's other libraries, copyfile, removefile, dnssd and the stand-ins

| Library | Exports | Apple's (macOS 27 SDK) | Gaps |
|---|---|---|---|
| `libsystem_darwin` | 75 | 76 | `os_lockdown_mode_enabled` (newer than Libc-1725) |
| `libsystem_collections` | 57 | 65 | the eight `os_set_128_ptr_*` (newer than Libc-1725) |
| `libcopyfile` | 11 | 11 | none |
| `libremovefile` | 14 | 14 | none |
| `libsystem_dnssd` | 46 | 61 | Apple-only client calls (`*Ex`, the `DNSServiceAttr*` setters, delegate connections, validation data) |

The stand-ins now cover every symbol the built libraries import from libxpc and libsystem_trace:
- **libxpc** (since P1-08 its `bootstrap_*`, `vproc_*` and `launch_*` calls are launchd-842's liblaunch, talking to a real launchd: `docs/base/session.md`): reference-counted XPC objects (null, bool, int64, uint64, string, date, uuid, array, dictionary) and the `bootstrap_*` calls. With no launchd until P1-08, no service can be looked up: `bootstrap_look_up2` answers `BOOTSTRAP_UNKNOWN_SERVICE`, pipes aren't created, and connections answer `XPC_ERROR_CONNECTION_INVALID`. The process has no entitlements and isn't sandboxed, and `xpc_create_from_plist` parses nothing, which `os_variant` and Libinfo take as no file.
- **libsystem_trace:** `os_log` to standard error. The stand-in decodes the argument buffer `__builtin_os_log_format` encodes (clang's `OSLogBufferLayout`), so messages come out as `os_log` would format them. Info and debug are off, as by default. The syslog shim is on, so syslog(3) and asl(3) print too. There are no activities.
- **Still outside NeoDarwin's builds:** libdyld (checkpoint 3), libSystem (checkpoint 4; libc++, libc++abi and libobjc link it), and libobjc's weak `swift_retain`/`swift_release` (`LC_LOAD_WEAK_DYLIB`, absent at runtime).

| Finding | Resolution |
|---|---|
| libsystem_darwin and libsystem_collections are Libc targets without per-file flags; collections builds with its own `collections.xcconfig` (hidden visibility) | `base/libdarwin` and `base/libcollections` replay each target |
| libdarwin's `variant.c` reads the undeclared `_os_xbs_chrooted`, `internal.h` includes libxpc's unpublished `os/transaction_private.h` unused, and `dirstat.c` includes the closed `apfs/apfs_fsctl.h` | libdarwin patches 0001–0003; `dirstatat_np` already takes the portable path |
| copyfile links libquarantine (closed) except on iOS, and includes xnu's `<Kernel/sys/decmpfs.h>` | copyfile patch 0001 (`COPYFILE_NO_QUARANTINE`) selects iOS's no-op calls; Kernel.framework goes on a framework path of the build's own |
| removefile's `REMOVEFILE_CLEAR_PURGEABLE` uses the closed APFS fsctl header | removefile patch 0001 (`REMOVEFILE_NO_APFS`): accepted and ignored, as on HFS+ |
| mDNSResponder-2881 publishes no `mDNSMacOSX/`, so there's no Xcode project and no Apple client stub. The tree is Apple's non-Apple configuration | the settings of 1310.140.1's libsystem_dnssd target and the published client library (`mDNSPosix` CLIENTLIBOBJS), with `MDNS_NO_STRICT=1`; the closed links are dropped |
| The published `dns_sd_private.h` lacks its private API section, and `kDNSServiceAttrAllowFailover` (Libinfo) exists only in Apple's stub | mDNSResponder patches 0001 (export the four private calls the sources define) and 0002 (the failover attribute, sent in the published TLV form). The sysroot stages both headers; Libinfo's header stand-in is gone |
| NeoDarwin has no mDNSResponder daemon (the macOS one isn't published) | DNS-SD calls fail to connect and Libinfo's mdns module gets no answer; a daemon built from `mDNSPosix` is later work |

### Checkpoint 3: dyld and libdyld

`//base:dyld_images` builds `/usr/lib/dyld` and `/usr/lib/system/libdyld.dylib` from dyld-1323.3. It replays the dyld, libdyld and libmach_o targets for plain arm64.
- **dyld** is an `MH_DYLINKER` with no undefined symbols, entered at `__dyld_start` through `LC_UNIXTHREAD`. It exports only what Apple's does and uses chained fixups. Its load commands match the host's dyld, less arm64e's `__AUTH_CONST`.
- **libdyld** exports 207 symbols against Apple's 228. The 29 it lacks are newer than 1323.3 (HWTrace, `macho_*`, the Rosetta subcache calls); 8 deprecated `NS*ObjectFileImage` calls that macOS 27 dropped are extra.

The libraries below libdyld in the build (kernel through libdispatch, and the stand-ins) link it through the SDK's `.tbd` stub, which carries the same install name; NeoDarwin's libdyld exports everything they import.

| Finding | Resolution |
|---|---|
| The published `dyld.h` uses `DYLD_EXCLAVEKIT_UNAVAILABLE` with its definition scrubbed; `DyldProcessConfig.cpp` needs the internal SDK's `<fcntl.h>` include and `PLATFORM_IOSMAC`; `LinkerOptimizationHints::valid()`'s body is scrubbed; `libdyld/utils.cpp` includes `Fixup.h`, which doesn't exist | dyld patches 0001–0004 |
| Xcode's header maps resolve quoted includes across the project's directories | `-iquote` on every source directory |
| dyld links closed static archives: corecrypto, libamfi, the sandbox archive, libclang_rt | `base/dyld/src` supplies each: an all-allow `amfi_check_dyld_policy_self` (`base/dyld/sdk/libamfi.h`), an allowing `sandbox_check`, and `___chkstk_darwin`. The digests follow the reuse order in `repository.md` §3.1: SHA-256 and the ccdigest framework come from xnu's own corecrypto subset (`osfmk/corecrypto`); SHA-1 and SHA-384 come from FreeBSD's through ndcrypto's adapters (`kernel/neodarwin/crypto`) |
| libdyld asks launchd whether it manages the process (`vproc_swap_integer`); launchd's `vproc_priv.h` isn't published | `base/sdk`'s `vproc_priv.h` has launchd-842's keys. The libxpc stand-in answers every key with an error, as launchd does for a process it doesn't know |
| LLVM 19's libc++ headers call `__libcpp_verbose_abort`, which Apple's Release libdyld doesn't need | a hidden definition in libdyld, so it doesn't link libc++ |
| TPRO, MTE and pointer authentication are arm64e-only | compiled out; `__TPRO_CONST` is protected with `vm_protect` instead |
| `PrebuiltLoader_version.h` is generated from a hash of record layouts | build.sh replays Apple's script with clang's `-fdump-record-layouts` |

The AMFI stand-in allows everything, so a restricted (setuid) process can use `DYLD_*` variables. That's acceptable until P1-15 brings code-signing policy.

### Checkpoint 4: libSystem.B, the runtime root and the first dynamic program

`//base:libsystem_b` builds `/usr/lib/libSystem.B.dylib` from Libsystem-1356. It compiles `init.c` and `CompatibilityHacks.c` and runs two of Apple's scripts:
- `linker_arguments.sh` decides the reexports and `init.c`'s `HAVE_*` switches. It reexports every library in Apple's list that NeoDarwin builds or stands in for: 27 of Apple's 39 (`libsystem/reexports.txt`, `//base:libsystem_reexports_test`). The 12 left out are closed and have no calls in NeoDarwin's configuration: cache, commonCrypto, keymgr, quarantine, containermanager, coreservices, darwindirectory, eligibility, networkextension, secinit, symptoms, trial.
- `create_dylib_symlinks.sh` makes `libSystem.dylib` and the BSD names (`libc`, `libm`, `libpthread`, `libdl` and so on).

Every initializer and fork hook `init.c` calls resolves to a NeoDarwin library or stand-in. Its exports are Apple's three.

`//base:root` (`tools/base/stage_root.sh`) merges every library's install tree into the runtime root, without the build-only `usr/local`. Bazel stores a tree's relative symbolic links as copies. The script turns a dylib stored under a name other than its install name back into a link to it (`libSystem.dylib`, `libc++.dylib`, `libobjc.dylib`, the BSD names).

`//tests/qemu/hello` is an Embedded Swift program linked against that root by Xcode's `ld` (`rules/darwin_executable.bzl`, `-syslibroot`). Its stdlib runtime calls (`putchar`, `posix_memalign`, `free`) bind to libSystem like any other import. `//images:hello_root` puts it on the HFS+ root as `/sbin/launchd`, next to the runtime root. `//kernel:sbsa_dyld_boot_test` boots it. dyld loads 32 images, and the program passes all five checks: stdio on the console, malloc, a pthread, dyld's `dlopen`/`dlsym`, and a function on a libdispatch global queue (the kernel's pthread workqueue).

| Finding | Resolution |
|---|---|
| `init.c` calls `_sanitizers_init` and `_libSC_info_fork_*` with no switch around them, and libmalloc upward-links libsystem_featureflags, which dyld must be able to load | stand-ins for libsystem_sanitizers, libsystem_configuration (until configd-1385 is built) and libsystem_featureflags. Their initializers and fork hooks have no state to set up or reset |
| libxpc, libsystem_trace and corecrypto have initializers and fork hooks too (`_libxpc_initializer`, `_libtrace_init`, `xpc_atfork_*`, `cc_atfork_*`) | added to those stand-ins |
| **When a main executable has weak definitions (any Embedded Swift program does), dyld binds every `operator new` and `delete` it may override, and fails the launch if one is missing. The list includes Apple's typed `operator new(size_t, std::__type_descriptor_t)`**, which LLVM's libc++abi lacks | `base/llvm/src/nd_typed_new_delete.cpp` implements all 20 of Apple's typed operators on libmalloc's `malloc_type_*` calls, so allocations keep their type IDs. libc++abi exports them and libc++ reexports them, as on macOS |
| Swift can't call `open(2)` (variadic), `stdout` (a macro) or libdispatch's C API (marked unavailable in favour of the Dispatch overlay, which Embedded Swift lacks) | three small functions in the test's justified C file |
| Libraries linked against the SDK resolve libSystem's reexports to the SDK's `.tbd` files | programs link with `-syslibroot` pointing at `//base:root` |
| The sysroot's libc++ and libunwind headers were absolute links into Bazel's execroot | `base/llvm/install_headers.sh` copies them dereferenced (`tar h`) |

To verify on the kernel as the userland grows:
- `fork` with the stand-ins' empty hooks;
- `dyld_get_program_sdk_version`;
- the plain-arm64 objc4 isa layout, once an Objective-C program runs;
- running with no shared cache: `__shared_region_check_np` currently fails cleanly and dyld loads every image from disk.

