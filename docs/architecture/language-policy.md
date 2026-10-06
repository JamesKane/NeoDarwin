<!-- SPDX-License-Identifier: BSD-2-Clause -->
# Implementation language policy

## 1. Decision (user, 2026-09-27)

**Swift 6 or newer is the implementation language for every new NeoDarwin component.** Where performance is critical the code is written in **Embedded (bare-metal) Swift** or in **allocation-free Swift**, with the compiler enforcing the absence of heap allocation. **C or C++ is the fallback**, used only where a Swift expression would harm portability, harm performance, or cannot be written. Apple is beginning to ship Swift inside its own OS components and the language's ownership model is maturing release by release; NeoDarwin tracks the newest stable toolchain and adopts ownership features as they stabilise rather than freezing on today's subset. No fourth language is introduced into first-party code: NeoDarwin's own components contain no Rust, Zig or Go, so first-party work has one toolchain and one debugger story.

**Scope (clarified 2026-09-27):** this policy governs software the project writes. It does not apply to vendored upstream code (XNU, OpenZFS, ACPICA, Mesa and the other `mirror-*` components), which is built in whatever language upstream uses. If an upstream component needs another compiler (for example, rustc for Mesa's NVK and Panfrost compilers and Rusticl), that compiler is pinned in the build as a vendored-component dependency; it is not a language NeoDarwin writes in.

## 2. Tiers

| Tier | Where | Rules | Enforcement |
|---|---|---|---|
| **T1 Hosted Swift** | services (`pkgd`, `netd`, the console server), `ndpkg`, `ndports`, first-party administration tools (`launchctl`, `service`, `sysrc`), host tools (`kcgen`, `dtdump`, `kcheck`, `ndimage`, `ndsign`, `xnu2bazel`, `parity`), dexts' control planes | Swift 6 language mode, strict concurrency (`-strict-concurrency=complete`), actors for shared state, `Sendable` everywhere, no `@unchecked` without a review note, no Foundation in libraries that ship in the base (swift-corelibs-foundation is a package, not a dependency of the system set) | compiler; `bazel test` runs with `-warnings-as-errors` |
| **T2 Allocation-free Swift** | hot paths inside T1 components: package-store hashing and verification, the console server's draw path, dext data paths | `~Copyable` and `~Escapable` types, `Span`/`RawSpan`/`MutableSpan` and `InlineArray` instead of `Array`/`String` on the path, no classes, existentials, closures that capture, or generic code the optimiser cannot specialise; explicit lifetimes; no locks on the hot path | `nd_swift_library(tier = "T2")` (rules/swift.bzl) adds a `<name>_t2` test that runs the T2 gate (`tools/t2check`): every public entry point carries `@_noLocks`, the stricter of the two performance annotations (no locks, and so no allocation, reference counting or metadata instantiation, any of which may lock); the hosted compile's performance diagnostics, always on and errors for annotated code, reject any of those reachable from an entry point; and, because the annotations are still underscored in the pinned toolchain, the whole module is compiled again in Embedded mode with `-no-allocations`, which also catches a stray allocation in an unannotated helper. A T2 module is a leaf (standard library only). `//tools/t2check:selftest` proves each violation fails CI |
| **T3 Embedded Swift** | `neoboot` (UEFI application), firmware-adjacent tools, secondary-core trampolines, and any kext logic that is not an IOKit class (parsers, tables, state machines), behind C entry points (`kext_swift`, proven by P0-10's trial, §3.1) | `-enable-experimental-feature Embedded` (or the stable spelling when it lands), `-no-allocations` where required, no Swift runtime, no metadata, no existentials or unspecialised generics; a C shim for ABIs Swift cannot spell (UEFI's Microsoft calling convention, IOKit's C++ vtables) | the Embedded toolchain rejects the disallowed features at compile time; link with `-nostdlib` proves no runtime dependency |
| **T4 C / C++ fallback** | upstream code (XNU, IOKit families, OpenZFS, ACPICA, HFS+, the Apple and FreeBSD command projects, FreeBSD-derived drivers); IOKit classes in kexts (C++); hand-written assembly and inline assembly (not expressible in Swift); intrinsics Swift lacks; UEFI and Mach-O structures where a C header is the specification | C23 / C++23 with the same clang; every fallback file carries `NeoDarwin-Language: <ground>: <reason>` in its first 20 lines, where ground is portability, performance or expressibility | review; `tools/lang-audit` lists every non-Swift first-party file with its justification, and CI fails on a file without one |

Portability, performance, expressibility: those are the only three grounds for T4, and "the team knows C better" is not one of them.

## 3. Boundaries between languages

- **The C ABI is the boundary.** Swift talks to C through module maps; C talks to Swift through `@_cdecl` entry points (or the stable spelling). No C++ is exposed across a boundary except inside a kext, where IOKit imposes it.
- **IOKit kexts** are C++ classes because `OSObject`, `IOService` and the kernel's linker expect them. The policy is: the class shell and IOKit plumbing in C++, the substance in Embedded Swift behind C entry points. P0-10's trial proved this shape on a non-critical kext (§3.1); `ndacpi` glue and a virtio driver are the first production candidates.
- **DriverKit-style dexts** run in userland where hosted Swift is available; the `NDDriverKit` runtime is Swift over the `.iig` C++ interfaces, so drivers are written in Swift by default (T1 control plane, T2 data path).
- **Upstream C stays C.** Apple's and FreeBSD's C programs and libraries are built as they are; Swift wrappers add ergonomics where a Swift caller needs them. Rewriting working C in Swift is not a goal.
- **Downstreams** (downstream.md) may adopt this policy; Magi does.
- **The kernel itself** stays C/C++ as Apple ships it; NeoDarwin's in-kernel additions (platform expert, GICv3, PSCI) are C++ IOKit classes; their non-class logic may follow the kext rule above.

### 3.1 Swift in kexts: the `kext_swift` verdict (P0-10, 2026-10-06)

**Verdict: feasible for kext logic behind C entry points, called from a C++ IOKit shell; not feasible for IOKit classes themselves.** Write parsers, tables, state machines, checksums and data-path arithmetic in Embedded Swift; keep every `OSObject`/`IOService` subclass, `kmod_info` and anything the kernel's C++ linker sees in C++.

The trial is `//kexts/swift_trial` (`NDSwiftTrial.kext`, not boot-critical): `glue.cpp` is an `IOService` that matches `IOResources`, and its `start()` calls `Trial.swift` through `@_cdecl` functions. It is linked only into `//kernel:sbsa_swift_trial_kc`, which only `//kernel:sbsa_swift_trial_test` boots. That test boots the collection on QEMU (cortex-a76) and requires these lines on serial: the Swift parser walks a well-formed table (returns 0) and rejects a corrupted one through typed throws (returns -2); then Swift calls kernel KPIs directly and returns 42.

What worked:
- **Toolchain.** The swift.org 6.3.2 Embedded Swift compiles for `arm64-apple-macos26.0` to a Mach-O object, with `-target-cpu cortex-a76 -no-allocations -Osize`, and `ld -kext` (the from-source ld64, `toolchains/ld64`) links it with the C++ object and libkmod's `_start`/`_stop`. kcgen links the bundle into a boot collection, and kcheck verifies it.
- **No runtime, no metadata.** Without classes or existentials, the linked kext imports only kernel exports: `IOLog`, the KPIs below, and the stack protector's `___stack_chk_guard` and `___stack_chk_fail`, which are Libkern exports. The compiler may also emit a `bzero` call, which Libkern exports too. Swift's stack protector works unchanged in the kernel.
- **Calling convention.** Kernel arm64 code uses plain AAPCS64. `@_cdecl` entry points and `@convention(c)` callbacks from Swift into C++ (`report(tag, value)`) need no shim. The kernel is arm64, not arm64e, so there is no pointer authentication to match.
- **Kernel KPIs from Swift.** The clang importer reads Kernel.framework's headers with `-Xcc -mkernel -Xcc -DKERNEL -Xcc -nostdinc` through a module map (`KernelKPI/`), and Swift calls `OSAddAtomic`, `IOSleep` and `clock_get_system_microtime` directly.
- **Stack.** Each Swift function in the trial uses a 64-byte frame. `Span`, `InlineArray`, typed throws and enums with payloads live in registers and on the stack.
- **ISA.** The kext passes the SBSA ISA audit (`//kexts/swift_trial:isa_audit`) with an empty baseline. Kernel float rules are no obstacle: clang's `-mkernel` on arm64 keeps `+neon`, and XNU's own C++ uses q registers for copies, so SIMD that Swift emits would be legal. The trial's Swift emits none.

What broke, or needs care:
- **IOKit classes cannot be written in Swift.** `OSObject`/`IOService` subclassing needs the C++ vtable layout, `OSMetaClass` registration (`OSDefineMetaClassAndStructors`), `-fapple-kext` vtable rules and the reserved-slot padding. Embedded Swift has no C++ class inheritance or virtual overrides, and Swift–C++ interop is not available in Embedded mode for this. So the C++ shell is permanent.
- **No `-mkernel` for Swift.** Its arm64 effects were checked one by one. Red zone: AArch64 LLVM never uses one. Unwind tables: unused. LR reserved for the register allocator: Swift doesn't apply it; harmless for leaf logic, but it means Swift isn't bit-for-bit kernel codegen. Builtins: Swift may emit `memset`/`memcpy`/`bzero` calls, and the kernel exports all three.
- **Linkage.** The Embedded stdlib defines weak runtime stubs (`swift_retain`, `swift_once`, …) in every object. A kext exports every global by default, so the bundle would carry weak definitions (`MH_WEAK_DEFINES`, `__weak_got`), and two Swift kexts would export the same names. kext.sh links with `-unexported_symbol '_swift_*' '__swift_*' '_$e*' -dead_strip`, which leaves only the `@_cdecl` entry points exported.
- **String literals allocate.** A `String` literal passed as `UnsafePointer<CChar>` bridges through heap storage, so `-no-allocations` rejects it. Use `StaticString.utf8Start`.
- **C variadics are not callable.** `IOLog` and `printf` are variadic, so logging goes through a C or C++ callback.
- **Traps.** A Swift trap is a `brk` in the kernel, which is a panic. Bounds and overflow checks stay on; hot loops use `&+` and checked indices deliberately.
- **Two toolchains.** The Swift side builds with swift.org 6.3.2, because Xcode ships no Embedded stdlib. The C++ side builds with Xcode clang. This lasts until the pinned toolchain (P0-02).

## 4. Ownership and performance idioms (for reviewers)

1. Data that crosses a process or thread boundary is a plain struct with fixed-width fields (the `Deskwin`/`Deskevent` model), never a class.
2. Buffers are `~Copyable` owners with `borrowing`/`consuming` parameters; views are `Span`s with lifetime dependencies. A function that needs to keep a view must take ownership explicitly.
3. Handles across boundaries are integer index-plus-generation values, never object references.
4. Batch APIs take `Span`s and counts; single-item conveniences wrap them.
5. Strings on hot paths are UTF-8 `Span<UInt8>`; `String` is for the edges.
6. Errors on hot paths are typed throws (`throws(E)`) so no existential is boxed.
7. Concurrency: actors at service granularity; hot paths and interrupt paths are single-owner and lock-free by construction, not by `Mutex`.
8. `unsafe` is confined to modules named `*Unsafe` with a documented invariant per API.

## 5. Toolchain implications (build-system design)

- One pinned toolchain built from `swiftlang/llvm-project` provides `swiftc`, clang, lld; Embedded mode is the same compiler with different flags, so T1–T4 share one toolchain and one debugger.
- Rules: `swift_library`/`swift_binary` (rules_swift) for T1/T2; a NeoDarwin `swift_embedded_binary` and `swift_embedded_library` for T3 with `-target` triples for the UEFI PE image (`aarch64-unknown-windows-msvc` for the PE container plus the freestanding Swift module) and for freestanding ELF/Mach-O; T3-in-kernel is an `nd_kext` with `embedded_swift = True` (rules/kext.bzl), whose build script compiles the Swift with the Embedded toolchain (kexts/swift_trial/kext.sh).
- CI gates (P0-10): strict concurrency and warnings as errors (`nd_swift_*`, rules/swift.bzl); the T2 gate for every `tier = "T2"` module (`<name>_t2`, tools/t2check); `-no-allocations` for every T3 module (rules/efi.bzl, rules/static_macho.bzl, the kext_swift build); `lang-audit` for T4 justifications (per-target `lang_audit_test`s, and `ci/test.sh`'s `--tree` audit of the whole tree, whose failure on an unjustified file `//tools/lang_audit:selftest` proves on a fixture tree).
- Toolchain cadence: bump to each stable Swift release within one milestone; language-mode migrations are their own epics.

## 6. Risks

| Risk | Mitigation |
|---|---|
| Embedded Swift on UEFI is young; the Embedded stdlib ships no PE/COFF target | resolved for AArch64 (P0-09): Swift targets `aarch64-none-none-elf`, clang retargets the bitcode to aarch64 COFF, `lld-link` produces the PE32+ image; AArch64 UEFI uses AAPCS64, so no calling-convention shim. x86-64 UEFI uses the Microsoft x64 convention and will need `@convention(c)` thunks or a C shim (Phase 6a) |
| Swift in kexts: no runtime, kernel calling conventions, `-mkernel` semantics | resolved by P0-10's trial (§3.1): feasible for logic behind C entry points, which `//kernel:sbsa_swift_trial_test` proves on QEMU; IOKit classes stay C++ |
| Performance annotations and `Span`/`InlineArray` availability differ across toolchain versions | tier rules name the intent; the pinned toolchain's spelling is recorded in `toolchains/README.md` and updated on each bump |
| Foundation and Dispatch dependencies creeping into base libraries | base-set libraries are built with no Foundation import; a Bazel aspect fails the build on `import Foundation` outside packages |
| Hiring and contributor familiarity | the C fallback is explicit and the boundaries are C ABI, so C contributors can work on drivers and upstream code without Swift |
