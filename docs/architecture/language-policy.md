<!-- SPDX-License-Identifier: BSD-2-Clause -->
# Implementation language policy

## 1. Decision (user, 2026-09-27)

**Swift 6 or newer is the implementation language for every new NeoDarwin component.** Where performance is critical the code is written in **Embedded (bare-metal) Swift** or in **allocation-free Swift**, with the compiler enforcing the absence of heap allocation. **C or C++ is the fallback**, used only where a Swift expression would harm portability, harm performance, or cannot be written. Apple is beginning to ship Swift inside its own OS components and the language's ownership model is maturing release by release; NeoDarwin tracks the newest stable toolchain and adopts ownership features as they stabilise rather than freezing on today's subset. No fourth language is introduced: there is no Rust, Zig or Go in the tree, so the build has one toolchain and one debugger story.

## 2. Tiers

| Tier | Where | Rules | Enforcement |
|---|---|---|---|
| **T1 Hosted Swift** | services (`nsd`, `keyd`, `pkgd`, `auditd`, `netd`), `ndpkg`, host tools (`kcgen`, `dtdump`, `kcheck`, `ndimage`, `ndsign`, `xnu2bazel`, `pkgsrc2nd`), the toolkit and desktop clients, `libns`, dexts' control planes | Swift 6 language mode, strict concurrency (`-strict-concurrency=complete`), actors for shared state, `Sendable` everywhere, no `@unchecked` without a review note, no Foundation in libraries that ship in the base (swift-corelibs-foundation is a package, not a dependency of the system set) | compiler; `bazel test` runs with `-warnings-as-errors` |
| **T2 Allocation-free Swift** | hot paths inside T1 components: `wsys` composition and damage, `inputd` event path, 9P request handling in `nsd` and `libns`, package-store hashing and verification, dext data paths | `~Copyable` and `~Escapable` types, `Span`/`RawSpan`/`MutableSpan` and `InlineArray` instead of `Array`/`String` on the path, no classes, existentials, closures that capture, or generic code the optimiser cannot specialise; explicit lifetimes; no locks on the frame path | the module is built with the performance diagnostics enabled and its entry points carry the no-allocation and no-lock annotations; CI fails on a diagnosed allocation. Until those annotations are fully stable in the pinned toolchain, the module is additionally compiled in Embedded mode with `-no-allocations` as a second check, which is stricter and catches the same thing |
| **T3 Embedded Swift** | `neoboot` (UEFI application), firmware-adjacent tools, secondary-core trampolines, and any kext logic that is not an IOKit class (parsers, tables, state machines) once `kext_swift` is proven | `-enable-experimental-feature Embedded` (or the stable spelling when it lands), `-no-allocations` where required, no Swift runtime, no metadata, no existentials or unspecialised generics; a C shim for ABIs Swift cannot spell (UEFI's Microsoft calling convention, IOKit's C++ vtables) | the Embedded toolchain rejects the disallowed features at compile time; link with `-nostdlib` proves no runtime dependency |
| **T4 C / C++ fallback** | upstream code (XNU, IOKit families, OpenZFS, ACPICA, HFS+, `libstyle`, `libagent`, `lib9p`, FreeBSD-derived drivers); IOKit classes in kexts (C++); hand-written assembly and inline assembly (not expressible in Swift); intrinsics Swift lacks; UEFI and Mach-O structures where a C header is the specification | C23 / C++23 with the same clang; every fallback file carries `NeoDarwin-Language: <ground>: <reason>` in its first 20 lines, where ground is portability, performance or expressibility | review; `tools/lang-audit` lists every non-Swift first-party file with its justification, and CI fails on a file without one |

Portability, performance, expressibility: those are the only three grounds for T4, and "the team knows C better" is not one of them.

## 3. Boundaries between languages

- **The C ABI is the boundary.** Swift talks to C through module maps; C talks to Swift through `@_cdecl` entry points (or the stable spelling). No C++ is exposed across a boundary except inside a kext, where IOKit imposes it.
- **IOKit kexts** are C++ classes because `OSObject`, `IOService` and the kernel's linker expect them. The policy is: the class shell and IOKit plumbing in C++, the substance in Embedded Swift behind C entry points, proven first on `ndacpi` glue and a virtio driver (epic P0-10). If the kernel runtime constraints defeat this (no Swift runtime, kernel calling convention, `-mkernel`), kexts stay C++ and the decision is recorded.
- **DriverKit-style dexts** run in userland where hosted Swift is available; the `NDDriverKit` runtime is Swift over the `.iig` C++ interfaces, so drivers are written in Swift by default (T1 control plane, T2 data path).
- **The C reference libraries** (`libstyle`, `libagent`, `lib9p`, from the plan-neo pilot) stay in C; Swift wrappers (`libns`) add the ergonomics. Rewriting working C in Swift is not a goal.
- **The kernel itself** stays C/C++ as Apple ships it; NeoDarwin's in-kernel additions (platform expert, GICv3, PSCI) are C++ IOKit classes until `kext_swift` is proven, then follow the kext rule above.

## 4. Ownership and performance idioms (for reviewers)

1. Data that crosses a process or thread boundary is a plain struct with fixed-width fields (the `Deskwin`/`Deskevent` model), never a class.
2. Buffers are `~Copyable` owners with `borrowing`/`consuming` parameters; views are `Span`s with lifetime dependencies. A function that needs to keep a view must take ownership explicitly.
3. Handles across boundaries are integer index-plus-generation values, never object references.
4. Batch APIs take `Span`s and counts; single-item conveniences wrap them.
5. Strings on hot paths are UTF-8 `Span<UInt8>`; `String` is for the edges.
6. Errors on hot paths are typed throws (`throws(E)`) so no existential is boxed.
7. Concurrency: actors at service granularity; the frame path and interrupt paths are single-owner and lock-free by construction, not by `Mutex`.
8. `unsafe` is confined to modules named `*Unsafe` with a documented invariant per API.

## 5. Toolchain implications (build-system design)

- One pinned toolchain built from `swiftlang/llvm-project` provides `swiftc`, clang, lld; Embedded mode is the same compiler with different flags, so T1–T4 share one toolchain and one debugger.
- Rules: `swift_library`/`swift_binary` (rules_swift) for T1/T2; a NeoDarwin `swift_embedded_binary` and `swift_embedded_library` for T3 with `-target` triples for the UEFI PE image (`aarch64-unknown-windows-msvc` for the PE container plus the freestanding Swift module) and for freestanding ELF/Mach-O; `kext_swift` experimental rule for T3-in-kernel.
- CI gates: strict concurrency, warnings as errors, the no-allocation diagnostics for every T2 module, `-no-allocations` for every T3 module, `lang-audit` for T4 justifications.
- Toolchain cadence: bump to each stable Swift release within one milestone; language-mode migrations are their own epics.

## 6. Risks

| Risk | Mitigation |
|---|---|
| Embedded Swift on UEFI (PE/COFF, Microsoft ABI for firmware callbacks) is young | C shim for the firmware ABI; `neoboot` keeps a C build target as the fallback until the Swift build boots the kernel on QEMU and hardware (P0-09 exit test) |
| Swift in kexts: no runtime, kernel calling conventions, `-mkernel` semantics | prove on one non-critical kext first (P0-10); kexts remain C++ if it fails |
| Performance annotations and `Span`/`InlineArray` availability differ across toolchain versions | tier rules name the intent; the pinned toolchain's spelling is recorded in `toolchains/README.md` and updated on each bump |
| Foundation and Dispatch dependencies creeping into base libraries | base-set libraries are built with no Foundation import; a Bazel aspect fails the build on `import Foundation` outside packages |
| Hiring and contributor familiarity | the C fallback is explicit and the boundaries are C ABI, so C contributors can work on drivers and upstream code without Swift |
