<!-- SPDX-License-Identifier: BSD-2-Clause -->
# NeoDarwin system overview

## 1. Layers

```
┌──────────────────────────────────────────────────────────────────────────┐
│  Downstreams         Magi (desktop, namespaces, agents) · others         │
╞══════════════════════════════════════════════════════════════════════════╡
│  Ports               ports tree → ndports → .ndpkg (kind = port)         │
├──────────────────────────────────────────────────────────────────────────┤
│  Base userland       Darwin *_cmds · FreeBSD where Apple is closed ·     │
│                      launchd + service/sysrc · OpenSSH · pf · mandoc ·   │
│                      ndpkg · pkgd · console server                       │
├──────────────────────────────────────────────────────────────────────────┤
│  Base libraries      libSystem (Libc, libpthread, libplatform, libmalloc)│
│                      libdispatch · dyld · libxo · libutil · OpenPAM ·    │
│                      swift-corelibs-foundation (package)                 │
├──────────────────────────────────────────────────────────────────────────┤
│  Kernel              XNU: Mach (osfmk) · BSD · IOKit · TrustedBSD MAC    │
│                      + NeoDarwin platform expert, GICv3, PSCI, ACPICA,   │
│                        fuse, OpenZFS, framebuffer                        │
├──────────────────────────────────────────────────────────────────────────┤
│  Boot                neoboot (UEFI app) · kernel collection (kcgen)      │
├──────────────────────────────────────────────────────────────────────────┤
│  Firmware / HW       UEFI + ACPI · ARM64 (SBSA) → AMD64 → RISCV64        │
└──────────────────────────────────────────────────────────────────────────┘
```

Everything above the kernel is a **package** (see the packaging design). The kernel plus its boot-critical kexts is the **kernel collection**, which is itself a package with a dedicated install path (a boot environment per system set). Everything above the double line is outside NeoDarwin and plugs in through the extension points in `downstream.md`.

## 2. Subsystem map

| Subsystem | Purpose | Language | Design doc |
|---|---|---|---|
| `neoboot` | UEFI loader: ACPI static tables → Apple device tree, flat Mach-O fileset loading (the kernel applies its own fixups), boot_args | Embedded Swift (T3); UEFI structures from a C header module; AArch64 UEFI uses AAPCS64, so no calling-convention shim | kernel/arm64-sbsa-bringup.md §2.1, language-policy.md |
| kernel platform layer | `SBSA` board config, GICv3, PSCI IOPMGR, platform expert compiled into the kernel | C / C++ (IOKit; T4 by expressibility) | kernel/arm64-sbsa-bringup.md §2.3 |
| `ndacpi.kext` | ACPICA runtime, `IOACPIPlatformDevice` nubs, PCIe ECAM, MSI | ACPICA in C (upstream); glue C++; first `kext_swift` trial | kernel/arm64-sbsa-bringup.md §2.2, drivers.md |
| driver families | virtio, NVMe, AHCI, XHCI/USB, network, SD/MMC, GPIO/I2C/SPI, framebuffer, audio | kexts: C++ shells, FreeBSD-derived C; dexts: Swift (T1 control, T2 data path) | drivers.md |
| `zfs.kext` (OpenZFS), `ndfuse`, `msdosfs` | in-kernel VFS plug-ins via `vfs_fsadd`; ZFS boot environments back system updates | C | filesystems.md |
| base userland | Apple's command projects, FreeBSD programs where Apple's are closed, FreeBSD-named administration front ends over launchd | C upstream; new tools in Swift | freebsd-parity.md, ../base/ |
| console | `ndfb`, virtual terminals, the hand-over to a display server | C++ kext shell; the console server in Swift | console.md |
| `ndbuild` | Bazel module, hermetic LLVM/Swift toolchain, custom rules (`xnu_kernel`, `kext`, `kext_collection`, `swift_embedded_binary`, `mig_library`, `system_image`, `nd_package`) | Starlark; tools in Swift | build-system.md |
| `ndpkg` / `pkgd` | package format, repositories, solver, system sets as boot environments, rollback | Swift 6 | packaging.md |
| `ndports` | the ports tree: recipes, clean-room builds, bulk builds, importers | Swift 6 | ports.md |
| `kcgen`, `dtdump`, `kcheck`, `ndimage`, `lang-audit`, `parity` | host tools: kernel collection linker, DT dump/verify, fixup checker, image builder, language-policy audit, parity inventory | Swift 6 | build-system.md §5, freebsd-parity.md §2 |

## 3. Design principles

These bind every subsystem design. Each is stated as a rule with the failure it prevents. One rule applies to the documents themselves: they are NeoDarwin's own go-forward text. The pilots they were distilled from are credited in provenance lines only.

1. **Current upstream, thin patches.** Apple components are vendored unmodified with a patch series on top (`repository.md` §3). A patch that cannot be rebased onto the next Apple drop within a day is a design smell; move the behaviour into a NeoDarwin-owned file instead.
2. **Data is the interface.** Wherever a subsystem exposes state, it exposes it as plain, versioned data: a configuration file, a `sysctl`, a manifest, a `--libxo json` report or a struct-of-arrays buffer, never only as an object graph behind method calls. This makes the system scriptable by people and agents, and testable by diff.
3. **Objects for behaviour, not for storage.** Object-oriented interfaces (IOKit classes, Swift protocols) define *what a component can do*. They do not own the hot-path data layout. Deep inheritance is forbidden in new code: prefer composition and protocol conformance, with no more than one concrete base class between a leaf and the framework root. (IOKit's own history is the lesson: `IOService` subclass chains became untestable.)
4. **Batch by default.** Every hot interface (interrupt completion, packet and block I/O, input events, console damage) takes and returns arrays with explicit counts. Single-item APIs are conveniences built on the batch API, never the other way round.
5. **FreeBSD's hands, Darwin's insides.** A FreeBSD administrator's commands, flags and files work. Darwin's own mechanisms (launchd, IOKit, the Mach-O toolchain) stay underneath, with front ends where the names differ (`freebsd-parity.md` §3).
6. **Mechanism here, policy downstream.** NeoDarwin provides mechanisms any downstream can use (kernel facilities, exported KPIs, the MAC framework, packages). It does not adopt one downstream's desktop, security model or application API (`downstream.md` §2).
7. **Explicit lifetimes, explicit ownership.** No hidden allocation on hot paths; buffers are handed over with a stated owner. Swift 6+ is the language for new code: `~Copyable`/`~Escapable` types, `Span`, strict concurrency, and compiler-enforced allocation-free modules where performance is critical. C/C++ is used only on the three fallback grounds in `language-policy.md`.
8. **One build graph, one toolchain, reproducible.** Every artifact from the firmware application to a ported program comes out of the same pinned LLVM/Swift toolchain, byte-for-byte reproducible, and NeoDarwin can run that build itself (`self-hosting.md`).
9. **Everything is a package, packages are data.** The kernel collection, the system image, ports and downstream software all use the same manifest format and signing scheme. Upgrade and rollback are the same operation at every layer.
10. **Architecture is a seam, not a fork.** Arch-specific code lives only behind the named seams in `multi-arch.md`. A new ISA adds files; it does not edit shared logic.
11. **Reference, don't copy.** Linux is documentation. FreeBSD is source we may derive from with attribution. Every derived or referenced driver keeps a provenance log.

## 4. Naming

- Daemons end in `d` and live in `/System/Library/Daemons`. FreeBSD-named front ends (`service`, `sysrc`) live where FreeBSD puts them.
- Kernel extensions are prefixed `nd` (`ndacpi.kext`, `ndfb.kext`). Upstream kexts keep their names (`zfs.kext`, `hfs.kext`).
- Host tools are single lower-case words (`kcgen`, `ndpkg`, `ndports`, `dtdump`).
- The ports tree is `/usr/ports`; packages live under `/System/Packages`.

## 5. Reading order

overview → build-system → packaging → ports → kernel/arm64-sbsa-bringup → drivers → filesystems → freebsd-parity → console → self-hosting → multi-arch → downstream → repository → roadmap.
