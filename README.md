<!-- SPDX-License-Identifier: BSD-2-Clause -->
# NeoDarwin

**NeoDarwin is a revival of OpenDarwin.** It builds Apple's current open-source drops (xnu, dyld, Libc, libdispatch, launchd, the IOKit families, hfs, the `*_cmds` projects and more) into a standalone, self-hosting operating system for commodity hardware. Its usage bar is command-line FreeBSD: a FreeBSD administrator finds the programs, files and workflows they expect. It boots from standard firmware (UEFI + ACPI), runs on ARM64 first and AMD64/RISCV64 later, is built by one hermetic Clang/LLVM build graph, and ships as signed, upgradeable packages on OpenZFS. Third-party software comes from a ports tree whose builds are packages.

NeoDarwin is also a **foundation for other projects**. It keeps its own policy small so that downstreams can take it in different directions: a desktop workstation, a macOS-compatibility project (an "OpenMacOS"), or a server distribution. See [docs/architecture/downstream.md](docs/architecture/downstream.md).

Kernel: XNU (Mach + BSD + IOKit). Userland: Darwin, with FreeBSD where Apple's source is closed. New code: Swift 6+.

## Lineage and downstreams

| Project | Relationship |
|---|---|
| **OpenDarwin** (2002–2006) | the project NeoDarwin revives: Darwin as an independent, community-built operating system |
| **FreeBSD** | the usage bar (`freebsd-parity.md`), the ports model (`ports.md`), and source for drivers and for programs Apple doesn't publish |
| **Magi** (`../Magi`) | the reference downstream: a 90s-lineage WIMP desktop, Plan 9 namespaces, capability security for agents, application APIs from a platform study. It took over the desktop, namespace, audio and scheduling designs that began here |
| **VectraOS** (`../designs`) | the pilot that set the portability-seams discipline and content-addressed distribution with a transparency log |
| **plan-neo** (`/Users/jkane/Development/c/plan-neo`) | the pilot that designed the package key chain |

## Goals (charter 2026-09-27, rescoped 2026-09-29)

| # | Goal | Where it is designed |
|---|---|---|
| 1 | Reuse Apple's open stack wherever possible: *current* open source first, then earlier Apple open-source drops, then FreeBSD, then new code; patch, don't fork-and-forget | [docs/repository.md](docs/repository.md) §3 upstream policy and §3.1 reuse order |
| 2 | One modern, cohesive build system on the Clang/LLVM ecosystem | [docs/architecture/build-system.md](docs/architecture/build-system.md) |
| 3 | A package system so components install and upgrade after first release, kernel included | [docs/architecture/packaging.md](docs/architecture/packaging.md) |
| 4 | A ports tree built on the package manager: recipes in, signed packages out | [docs/architecture/ports.md](docs/architecture/ports.md) |
| 5 | Usage parity with command-line FreeBSD: base userland and administration, networking and remote access, ZFS boot environments, self-hosting | [docs/architecture/freebsd-parity.md](docs/architecture/freebsd-parity.md), [docs/architecture/self-hosting.md](docs/architecture/self-hosting.md) |
| 6 | IOKit drivers written with FreeBSD (derive) and Linux (reference only) as sources of truth | [docs/architecture/drivers.md](docs/architecture/drivers.md) |
| 7 | A modern filesystem: OpenZFS for root, system sets, packages and data | [docs/architecture/filesystems.md](docs/architecture/filesystems.md) |
| 8 | A console on the framebuffer, and a graphics foundation that downstreams build on | [docs/architecture/console.md](docs/architecture/console.md) |
| 9 | Portable beyond ARM64 to AMD64 and RISCV64 | [docs/architecture/multi-arch.md](docs/architecture/multi-arch.md) |
| 10 | A foundation for downstreams: stable interfaces, extension points, mechanism without policy | [docs/architecture/downstream.md](docs/architecture/downstream.md) |

The charter's earlier goals for a WIMP desktop, application APIs and Plan 9 style namespaces for agents moved to Magi on 2026-09-29.

**Implementation language:** Swift 6+ for all new components, Embedded or allocation-free Swift where performance is critical, C/C++ only where Swift would harm portability or performance or cannot express the concept. See [docs/architecture/language-policy.md](docs/architecture/language-policy.md).

## Document map

- [docs/architecture/00-overview.md](docs/architecture/00-overview.md): system layers, subsystem map, design principles, naming.
- [docs/architecture/language-policy.md](docs/architecture/language-policy.md): Swift-first language tiers, C/C++ fallback grounds, boundaries, CI gates.
- `docs/architecture/*.md`: one high-level design per subsystem (table above).
- [docs/kernel/arm64-sbsa-bringup.md](docs/kernel/arm64-sbsa-bringup.md): the ARM64 kernel bring-up design (loader, DT-ABI, SBSA board config, risks), grounded in `xnu-12377.1.9` with file:line citations.
- [docs/base/](docs/base/): libSystem from Apple source, and the interactive session over serial.
- [docs/repository.md](docs/repository.md): repository structure, GitHub → Forgejo portability, upstream vendoring and patch policy, CI.
- [roadmap/ROADMAP.md](roadmap/ROADMAP.md): phases and epics. [roadmap/backlog.yaml](roadmap/backlog.yaml) is the machine-readable backlog that agents and humans refine.

## Non-goals

These describe what NeoDarwin itself builds. They don't restrict downstreams (`downstream.md` §2).

- A desktop, window system or application toolkit. NeoDarwin provides the console and the graphics foundation; desktops come from downstreams or ports.
- Binary compatibility with macOS applications. Source compatibility with Darwin, POSIX and Mach APIs is a goal.
- Reimplementing closed Apple frameworks (AppKit, CoreGraphics, WindowServer, OpenDirectory).
- Running on Apple Silicon Macs. The loader and platform layer target UEFI/ACPI machines; Apple boot chains are out of scope.
- FreeBSD subsystems with no xnu counterpart: jails, bhyve, Capsicum and the Linux ABI (`freebsd-parity.md` §5).

## Licensing

Apple-derived code stays under APSL 2.0. New NeoDarwin code is BSD-2-Clause. FreeBSD-derived drivers and programs keep their BSD licences and attribution. OpenZFS is CDDL and ships as a kext. GPL code (Linux drivers) is **never** linked into the kernel or base libraries; it may ship only as ports. Linux sources are consulted as documentation of hardware behaviour, with a written reference log per driver (see drivers design §4).

## Reference trees in this workspace

- `../xnu`: the kernel checkout the bring-up design cites (xnu-12377.1.9).
- `../freebsd-src` (including `sys/contrib/openzfs`), `../linux`, `../9front`, `../fuchsia`, `../haiku`, `../seL4`: reference trees. Each design says what is derived and what is only consulted.
