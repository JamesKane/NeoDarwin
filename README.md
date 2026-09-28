<!-- SPDX-License-Identifier: BSD-2-Clause -->
# NeoDarwin

**NeoDarwin rebuilds OpenDarwin from Apple's current open-source drops (xnu, dyld, Libc, libdispatch, IOKit families, hfs, …) into a standalone, self-hosting operating system for commodity hardware.** It boots from standard firmware (UEFI + ACPI), runs on ARM64 first and AMD64/RISCV64 later, is built by one hermetic Clang/LLVM-based build graph, ships as signed, upgradeable packages on OpenZFS, and exposes system and application state through the filesystem in the Plan 9 tradition so that people *and agents* can drive it with the same tools.

Working title: **NeoDarwin**. Kernel: XNU (Mach + BSD + IOKit). Base userland: Darwin. New code: Swift 6+. Desktop: a 1990s-workstation lineage (NeXT, Amiga, BeOS, OPEN LOOK, IRIX), procedurally drawn and owned by the user.

## Lineage

NeoDarwin is the vision distilled from three pilot projects, now carried forward as one system on top of XNU and Apple's open source:

| Pilot | What NeoDarwin takes from it |
|---|---|
| **VectraOS** (`../designs`) | the portability-seams discipline, "no ambient authority" capabilities, the compositor / window-manager / shell split, content-addressed distribution with a transparency log |
| **plan-neo** (`/Users/jkane/Development/c/plan-neo`) | the window protocol over 9P, the `libstyle` theme engine and its version-2 configuration model (classes, cascade, binds, modes, rules), the application–agent protocol, the frame-budget rules, the package key chain, and the designs for the editor and debugger |
| **NuAqua** (`../NuAqua`) | the Swift 6 framework-free core: the Vesper renderer, the Typeface TrueType stack and the scanline path filler, moving into NeoDarwin modules with refactored namespaces |

The pilots remain where they are; NeoDarwin's documents are the go-forward text and do not depend on them.

## Goals (from the charter, 2026-09-27)

| # | Goal | Where it is designed |
|---|---|---|
| 1 | Reuse Apple's open stack wherever possible: *current* open source first, then earlier Apple open-source drops, then FreeBSD, then new code; patch, don't fork-and-forget | [docs/repository.md](docs/repository.md) §3 upstream policy and §3.1 reuse order |
| 2 | One modern, cohesive build system on the Clang/LLVM ecosystem | [docs/architecture/build-system.md](docs/architecture/build-system.md) |
| 3 | A package system so components install and upgrade after first release, kernel included | [docs/architecture/packaging.md](docs/architecture/packaging.md) |
| 4 | IOKit drivers written with FreeBSD (derive) and Linux (reference only) as sources of truth | [docs/architecture/drivers.md](docs/architecture/drivers.md) |
| 5 | A graphic console, then a 90s-lineage, user-themable WIMP desktop; application APIs decided by a landscape and demographic study | [docs/architecture/graphics-desktop.md](docs/architecture/graphics-desktop.md) |
| 6 | A modern filesystem: OpenZFS for root, system sets, packages and data | [docs/architecture/filesystems.md](docs/architecture/filesystems.md) |
| 7 | Portable beyond ARM64 to AMD64 and RISCV64 | [docs/architecture/multi-arch.md](docs/architecture/multi-arch.md) |
| 8 | Agent-era infrastructure: Plan 9 style namespaces, state as files, capability security | [docs/architecture/namespaces-agents.md](docs/architecture/namespaces-agents.md) |

**Implementation language:** Swift 6+ for all new components, Embedded or allocation-free Swift where performance is critical, C/C++ only where Swift would harm portability or performance or cannot express the concept. See [docs/architecture/language-policy.md](docs/architecture/language-policy.md).

## Document map

- [docs/architecture/00-overview.md](docs/architecture/00-overview.md) — system layers, subsystem map, design principles, naming.
- [docs/architecture/language-policy.md](docs/architecture/language-policy.md) — Swift-first language tiers, C/C++ fallback grounds, boundaries, CI gates.
- `docs/architecture/*.md` — one high-level design per subsystem (table above).
- [docs/kernel/arm64-sbsa-bringup.md](docs/kernel/arm64-sbsa-bringup.md) — the ARM64 kernel bring-up design (loader, DT-ABI, SBSA board config, risks). Grounded in `xnu-12377.1.9` with file:line citations.
- [docs/desktop/README.md](docs/desktop/README.md) — the desktop specifications: window protocol, theme engine, UI configuration, agent protocol, headers.
- [docs/repository.md](docs/repository.md) — repository structure, GitHub → Forgejo portability, upstream vendoring and patch policy, CI.
- [roadmap/ROADMAP.md](roadmap/ROADMAP.md) — phases and epics; [roadmap/backlog.yaml](roadmap/backlog.yaml) is the machine-readable backlog that agents and humans refine.

## Non-goals (for now)

- Binary compatibility with macOS applications. Source compatibility with Darwin/POSIX/Mach APIs is a goal; App Store or Cocoa compatibility is not.
- Running on Apple Silicon Macs. The loader and platform layer target UEFI/ACPI machines; Apple boot chains are out of scope.
- Reimplementing closed Apple frameworks (AppKit, CoreGraphics, WindowServer). NeoDarwin builds its own display and toolkit stack.

## Licensing

Apple-derived code stays under APSL 2.0. New NeoDarwin code is BSD-2-Clause. FreeBSD-derived drivers keep their BSD licences and attribution. OpenZFS is CDDL and ships as a kext. GPL code (Linux drivers) is **never** linked into the kernel or base libraries; it may ship only as separately packaged userland programs. Linux sources are consulted as documentation of hardware behaviour, with a written reference log per driver (see drivers design §4).

## Reference trees in this workspace

- `../xnu` — the kernel checkout the bring-up design cites (xnu-12377.1.9).
- `../freebsd-src` (including `sys/contrib/openzfs`), `../linux`, `../9front`, `../fuchsia`, `../haiku`, `../seL4` — reference trees. Each design says what is derived versus merely consulted.
