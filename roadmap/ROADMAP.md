<!-- SPDX-License-Identifier: BSD-2-Clause -->
# NeoDarwin roadmap

_Phases are ordered by dependency, not by calendar. `backlog.yaml` is the source of truth for epics, owners and status; this file is the narrative. Re-evaluate at the end of every phase and whenever a kernel-blueprint risk (R1 VM handoff, R2 GIC Group 0, R3 closed kexts) changes state._

## Phase 0 — Foundation
Repository and mirrors, Bazel module, hermetic LLVM/Swift toolchain, `xnu_kernel` phase-1 wrapper, QEMU harness, CI on self-hosted runners, `kcgen` skeleton, OpenZFS macOS-layer status check. **Exit:** `bazel build //kernel:sbsa` produces a kernel and `bazel test //tests/qemu:smoke` boots an EFI stub and asserts on serial output.

## Phase 1 — Kernel bring-up on ARM64 (kernel blueprint M0–M6)
`neoboot`, DT-ABI, `SBSA` board config, GICv3 with the Group 1 timer option, PSCI SMP, in-kernel platform expert, mockfs PID 1, HFS+ ramdisk root, launchd, serial shell, ACPICA kext, PCIe, virtio-blk and NVMe, OrangePi 6 Plus. **Exit:** interactive shell over serial on QEMU `virt`, `sbsa-ref` and the CD8180; root from NVMe.

## Phase 2 — Base system, packaging and updates
`ndpkg` format and store, signed repositories, system sets as ZFS boot environments (lands after P3-03), `xnu_kernel` phase-2 native build, base-library BUILD ports (dyld, Libc, libdispatch, Foundation), pkgsrc bridge. **Exit:** a machine installed from a release image upgrades its kernel collection and a userland package from a repository, reboots into the new boot environment, and rolls back on induced failure.

## Phase 3 — Storage, OpenZFS, drivers breadth
`zfs.kext` on NeoDarwin, root on ZFS with boot environments, `msdosfs`, `ndfuse`, `nd9p`, AHCI, XHCI plus USB core and HID/mass-storage dexts, Ethernet dexts, SD/MMC, IOSerialFamily tty, DriverKit runtime (`NDDriverKit`), `neoboot` ZFS reader. **Exit:** root on ZFS with a working `ndpkg system rollback`; USB keyboard and Ethernet work on the CD8180; a dext crash does not panic the kernel.

## Phase 4 — Graphic console and desktop
`ndfb` framebuffer over IOGraphics, `inputd`, `wsys` (the NeoDarwin window protocol plus Wayland core, software compositor on Vesper, workspaces), the **platform API and source-corpus study** before any public API is frozen, the chrome (screen bar, NeXT menus, dock, window frames), the theme engine with user-editable themes, the column file viewer over `/n`, the MUI-style prefs panel, the graphic terminal and login. A later modernisation study evaluates what alternative Linux desktops are converging on. **Exit:** log in on the framebuffer console with the `neon` scheme, switch to a user-authored scheme, open the terminal and the viewer, run a Wayland client alongside.

## Phase 5 — Namespaces, state as files, agent-era security
`libns`, `nsd`, `keyd`, `ndsandbox`, system daemons under `/n/sys`, toolkit auto-export of window and state trees, ZFS snapshots around agent sessions, agent grant flow, audit. **Exit:** an agent process with a scoped token lists windows, drives an app through `ctl`, upgrades a package via `/n/sys/pkg/ctl`, and every action is reconstructible from `/n/sys/audit` and undoable from a snapshot.

## Phase 6 — Multi-architecture
6a AMD64: `neoboot` x86 mode, IOAPIC controller, ACPICA reuse, driver reuse; boots on a UEFI PC and in QEMU `q35`. 6b RISCV64: toolchain (Mach-O CPU type, lld, dyld), user-mode bring-up, `osfmk/riscv64` and pmap, QEMU `virt`. **Exit:** the same repository builds and boots three ISAs in CI.

## Phase 7 — Acceleration and polish
GPU kernel drivers with Mesa userland where licences allow, Vulkan for clients, audio, Wi-Fi, power management (idle states, suspend), installer, documentation site, first public release.

## Parallelism
Phase 2 packaging and Phase 5 `libns`/`nsd` are userland-only and start during Phase 1 on macOS hosts. Phase 3's `zfs.kext` port can begin as soon as the M4 kernel boots with a block device. Phase 4's renderer (Vesper, from the NuAqua pilot) already runs on macOS and continues there while its namespaces are refactored; the chrome study exists as an HTML prototype and drives the renderer's feature list. Phase 6a can start once Phase 1's ACPICA kext exists.

## Re-evaluation checklist (run at each phase boundary)
1. Did any kernel blueprint risk change state? Update the DT-ABI and this file.
2. Did a new upstream drop (Apple, OpenZFS) land? Run `upstream-bump`; count patches over budget.
3. Which epics were split, merged or dropped? Reflect in `backlog.yaml` with `superseded_by`.
4. Are exit tests still automated? Any manual exit test becomes a P0 epic.
