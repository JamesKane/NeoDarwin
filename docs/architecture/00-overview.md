<!-- SPDX-License-Identifier: BSD-2-Clause -->
# NeoDarwin system overview

## 1. Layers

```
┌──────────────────────────────────────────────────────────────────────────┐
│  Desktop & apps      desktop chrome · native toolkit (API via study) ·    │
│                      Wayland clients                                       │
├──────────────────────────────────────────────────────────────────────────┤
│  Platform services   wsys (window system) · inputd · nsd · keyd · pkgd    │
│                      launchd · syslog · netd · ndpkg                       │
├──────────────────────────────────────────────────────────────────────────┤
│  Base libraries      libSystem (Libc, libpthread, libplatform, libmalloc) │
│                      libdispatch · dyld · swift-corelibs-foundation ·      │
│                      libns (9P state) · libwayland · libnd (system ABI)     │
├──────────────────────────────────────────────────────────────────────────┤
│  Kernel              XNU: Mach (osfmk) · BSD · IOKit · TrustedBSD MAC     │
│                      + NeoDarwin platform expert, GICv3, PSCI, ACPICA,     │
│                        p9fs, fuse, OpenZFS, framebuffer console                │
├──────────────────────────────────────────────────────────────────────────┤
│  Boot                neoboot (UEFI app) · kernel collection (kcgen)        │
├──────────────────────────────────────────────────────────────────────────┤
│  Firmware / HW       UEFI + ACPI · ARM64 (SBSA) → AMD64 → RISCV64          │
└──────────────────────────────────────────────────────────────────────────┘
```

Everything above the kernel is a **package** (see packaging design). The kernel plus its boot-critical kexts is the **kernel collection**, which is itself a package with a dedicated install path (A/B boot slots).

## 2. Subsystem map

| Subsystem | Purpose | Language | Design doc |
|---|---|---|---|
| `neoboot` | UEFI loader: ACPI static tables → Apple device tree, flat Mach-O fileset loading (the kernel applies its own fixups), boot_args | Embedded Swift (T3); UEFI structures from a C header module; AArch64 UEFI uses AAPCS64, so no calling-convention shim | kernel/arm64-sbsa-bringup.md §2.1, language-policy.md |
| kernel platform layer | `SBSA` board config, GICv3, PSCI IOPMGR, platform expert compiled into the kernel | C / C++ (IOKit; T4 by expressibility) | kernel/arm64-sbsa-bringup.md §2.3 |
| `ndacpi.kext` | ACPICA runtime, `IOACPIPlatformDevice` nubs, PCIe ECAM, MSI | ACPICA in C (upstream); glue C++; first `kext_swift` trial | kernel/arm64-sbsa-bringup.md §2.2, drivers.md |
| driver families | virtio, NVMe, AHCI, XHCI/USB, network, SD/MMC, GPIO/I2C/SPI, framebuffer | kexts: C++ shells, FreeBSD-derived C; dexts: Swift (T1 control, T2 data path) | drivers.md |
| `zfs.kext` (OpenZFS), `nd9p`, `ndfuse` kexts | in-kernel VFS plug-ins via `vfs_fsadd`; ZFS boot environments back system updates | C | filesystems.md |
| `ndbuild` | Bazel module, hermetic LLVM/Swift toolchain, custom rules (`xnu_kernel`, `kext`, `kext_collection`, `swift_embedded_binary`, `mig_library`, `system_image`, `nd_package`) | Starlark; tools in Swift | build-system.md |
| `ndpkg` / `pkgd` | package format, repositories, solver, A/B system sets, rollback | Swift 6 | packaging.md |
| `nsd`, `libns`, `keyd` | per-process namespaces over 9P, app-state-as-files library, capability tokens | Swift 6 (+ C shim) | namespaces-agents.md |
| `ndsandbox.kext` | open MAC policy: namespace confinement, entitlement manifests, audit | C | namespaces-agents.md §5 |
| `wsys`, `inputd` | window system (compositor, WM policy, shell chrome as separate modules) speaking the NeoDarwin window protocol + Wayland core; input routing | Swift 6 (T2 on the frame and event paths); `libstyle` in C | graphics-desktop.md |
| desktop chrome + toolkit | screen bar, menus, dock, column viewer, prefs, terminal; theme engine; toolkit API set by the P4-05 study; renderer core from the NuAqua pilot | Swift 6 | graphics-desktop.md, ../desktop/README.md |
| `kcgen`, `dtdump`, `kcheck`, `ndimage`, `lang-audit` | host tools: kernel collection linker, DT dump/verify, fixup checker, image builder, language-policy audit | Swift 6 | build-system.md §5 |

## 3. Design principles

These bind every subsystem design. Each is stated as a rule with the failure it prevents. One rule about the documents themselves: they are NeoDarwin's own go-forward text; the pilots they were distilled from (VectraOS, plan-neo, NuAqua) are credited in provenance lines only.

1. **Current upstream, thin patches.** Apple components are vendored unmodified with a patch series on top (`repository.md` §3). A patch that cannot be rebased on the next Apple drop within a day is a design smell; move the behaviour into a NeoDarwin-owned file instead.
2. **Data is the interface.** Wherever a subsystem exposes state, it exposes it as plain, versioned data (a file tree, a struct-of-arrays buffer, a manifest), never only as an object graph behind method calls. This is what makes the system scriptable by agents and testable by diff. (Data-Oriented Design lesson: separate data layout from behaviour; Plan 9 lesson: everything is a file.)
3. **Objects for behaviour, not for storage.** Object-oriented interfaces (IOKit classes, Swift protocols) define *what a component can do*. They do not own the hot-path data layout. Deep inheritance is forbidden in new code: prefer composition and protocol conformance; no more than one concrete base class between a leaf and the framework root. (OOD lesson from IOKit's own history: `IOService` subclass chains became untestable.)
4. **Batch by default.** Every hot interface (interrupt completion, packet and block I/O, input events, compositor damage, 9P reads) takes and returns arrays with explicit counts. Single-item APIs are conveniences built on the batch API, never the other way round.
5. **No ambient authority.** A process holds capabilities (Mach ports, 9P attach tokens, file descriptors) that were explicitly handed to it. Namespaces, not global paths, decide what a process can see. (VectraOS manifesto, applied within POSIX constraints.)
6. **Explicit lifetimes, explicit ownership.** No hidden allocation in hot paths; buffers are handed over with a stated owner. Swift 6+ is the language for new code: `~Copyable`/`~Escapable` types, `Span`, strict concurrency, and compiler-enforced allocation-free modules where performance is critical; C/C++ only on the three fallback grounds in `language-policy.md`.
7. **One build graph, one toolchain, reproducible.** Every artifact from firmware app to desktop package comes out of the same Bazel invocation with the same pinned LLVM/Swift toolchain, byte-for-byte reproducible.
8. **Everything is a package, packages are data.** The kernel collection, the system image and applications all use the same manifest format and signing scheme; upgrade and rollback are the same operation at every layer.
9. **Architecture is a seam, not a fork.** Arch-specific code lives only behind the named seams in `multi-arch.md`. A new ISA adds files; it does not edit shared logic.
10. **Reference, don't copy.** Linux is documentation. FreeBSD is source we may derive from with attribution. Every derived or referenced driver keeps a provenance log.

## 4. Naming

- Daemons end in `d` and live in `/System/Library/Daemons`; their control trees appear under `/n/sys/<name>`.
- Kernel extensions are prefixed `nd` (`ndacpi.kext`, `nd9p.kext`, `ndsandbox.kext`); upstream kexts keep their names (`zfs.kext`, `hfs.kext`).
- Host tools are lower-case single words (`kcgen`, `ndpkg`, `dtdump`).
- The namespace root is `/n` (Plan 9 convention). System control lives at `/n/sys`, per-application state at `/n/app/<bundle-id>`.

## 5. Reading order

overview → build-system → packaging → kernel/arm64-sbsa-bringup → drivers → filesystems → graphics-desktop → namespaces-agents → multi-arch → repository → roadmap.
