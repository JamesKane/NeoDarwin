<!-- SPDX-License-Identifier: BSD-2-Clause -->
# NeoDarwin roadmap

_Phases are ordered by dependency, not by calendar. `backlog.yaml` is the source of truth for epics, owners and status; this file is the narrative. Re-evaluate at the end of every phase and whenever a kernel-blueprint risk (R1 VM handoff, R2 GIC Group 0, R3 closed kexts) changes state._

## Status (parked 2026-10-07)

The repository is at `github.com/JamesKane/NeoDarwin`. The working rule is to close phases in order, on QEMU, while the board is busy.

| Phase | State |
|---|---|
| 0 | Done except **P0-05**, CI runners. The user deferred choosing a host; this Mac was proposed, and the fork-PR guard plan is in `docs/repository.md` §5. P0-01 (governance files, fragile-upstream mirrors, `ci/mirror_check.sh`), P0-02 (pinned Swift 6.4 / LLVM 23.1.3 / SDK 27.0 with `--config=pinned` and `ci/no_xcode.sh`), P0-04 (`qemu_test`, `//tests/qemu:smoke`), P0-06 (from-source ld64-957.1 links the kernel and kexts) and P0-10 (T2 `@_noLocks` gate, kext_swift verdict) are done. |
| 1 | Done on QEMU. **P1-11** (first boot on the Radxa Dragon Q8B) and the board exits of P1-12, P1-18 and P1-19 wait for board time (`docs/kernel/arm64-sbsa-bringup.md`, "Status"). |
| 2 | **In progress.** Done: P2-01 (`.ndpkg`, `ndsign`, kernel-checked trust-cache grants), P2-02 (`ndpkg` store, libsolv, nullfs activation), P2-03 (`ndpkg system` upgrade/confirm/rollback over ZFS boot environments, per-BE kernels on the ESP). **Next: P2-04** (signed remote repositories), then P2-05 and P2-10 (ports tree and importers, toward 100 signed ports), plus P2-06, P2-07 and P2-12 (builds). P2-08 and P2-11 need build hosts, like P0-05. |
| 3 | P3-01 and **P3-03** (root on ZFS, `ndbectl`) are done; P3-11 checkpoint 5 (zfs.kext in every boot) is done. The phase exit needs the Q8B. |
| 4 | **P4-21** (base userland) is at 96 of 113 tested rows passing FreeBSD's tests under the structural rule (`//base:freebsd_tests_exit_check`). 17 rows have 144 fixable cases left, plus `mdconfig`, `nvmecontrol` and `tzsetup` (`freebsd-parity.md` §2.1). P4-24 is done on QEMU, with its board exit pending. |

**Before any board runs `ndpkg system`:** the ESP rewrite isn't atomic (`docs/architecture/packaging.md` §6.1). It needs A/B ESPs, or P3-06's ZFS loader.

**Known load-sensitive tests** under the full QEMU set: the ZFS suite (FLAKY entries in `kexts/zfs/tests/expected.tsv`), `sbsa_net_intx_test` and `sbsa_pf_ntp_test`. Patch 0046's monotonic timebase costs about 10% on QEMU, until QEMU uses a monotonic Darwin clock (`docs/kernel/arm64-sbsa-bringup.md` §2.1.10).

**NeoDarwin 1.0** is phases 0 to 5 plus the installer and release (P7-03) on ARM64: a system with usage parity with command-line FreeBSD (`docs/architecture/freebsd-parity.md`) that builds itself. AMD64 (6a) follows in 1.x.

On 2026-09-29 the desktop, window system, toolkit, audio service, scheduling contract, namespaces and agent epics moved to **Magi** (`../Magi`), NeoDarwin's reference downstream. Their old ids (the P4 desktop epics, the P5 namespace epics, and P7-04 to P7-07) are recorded in Magi's backlog and are not reused here.

## Phase 0: Foundation
Repository and mirrors, Bazel module, hermetic LLVM/Swift toolchain, `xnu_kernel` phase-1 wrapper, QEMU harness, CI on self-hosted runners, `kcgen` skeleton, OpenZFS macOS-layer status check. **Exit:** `bazel build //kernel:sbsa` produces a kernel, and `bazel test //tests/qemu:smoke` boots an EFI stub and asserts on serial output.

## Phase 1: Kernel bring-up on ARM64 (kernel blueprint M0–M6)
`neoboot`, DT-ABI, `SBSA` board config, GICv3 with the Group 1 timer option, PSCI SMP, in-kernel platform expert, mockfs PID 1, HFS+ ramdisk root, libSystem from Apple source, launchd, serial shell, ACPICA kext, PCIe, virtio-blk and NVMe, the Radxa Dragon Q8B (Qualcomm SC8280XP: Armv8.2 baseline, GENI UART console). **Exit:** interactive shell over serial on QEMU `virt`, `sbsa-ref` and the Q8B; root from NVMe.

## Phase 2: Base system, packaging, ports and updates
`ndpkg` format and store, signed repositories, system sets as ZFS boot environments (lands after P3-03), `xnu_kernel` phase-2 native build, base libraries on Bazel, the **ports tree** (recipe format and `ndports`, P2-05; importers from FreeBSD ports and pkgsrc, P2-10; bulk builder and ports repository, P2-11), reproducible builds. **Exit:** a machine installed from a release image upgrades its kernel collection and a userland package from a repository, reboots into the new boot environment, and rolls back on induced failure; 100 ports build from recipes into signed packages.

## Phase 3: Storage, OpenZFS, drivers breadth
`zfs.kext` on NeoDarwin, root on ZFS with boot environments, `msdosfs`, `ndfuse`, AHCI, XHCI plus USB core and HID/mass-storage dexts, Ethernet dexts, SD/MMC, IOSerialFamily tty, DriverKit runtime (`NDDriverKit`), `neoboot` ZFS reader, ZFS native encryption. **Exit:** root on ZFS with a working `ndpkg system rollback`; USB keyboard and Ethernet work on the Q8B; a dext crash does not panic the kernel.

## Phase 4: FreeBSD command-line parity
The parity inventory against FreeBSD's base (P4-20), the rest of the base userland (P4-21), accounts and PAM (P4-22), `service`/`sysrc` over launchd with cron, `at`, periodic and newsyslog (P4-23), networking userland with DHCP, DNS, NTP and `pf` (P4-24), OpenSSH (P4-25), NFS (P4-28), DTrace with `dtruss` (P4-29), disk quotas (P4-30), printing with CUPS (P4-31), UEFI variables (P4-32), the framebuffer (`ndfb`, P4-01) and virtual terminals (P4-26), and the Handbook workflow suite as a CI test (P4-27). **Exit:** the workflow suite in `freebsd-parity.md` §4 passes on QEMU and the Q8B, and the parity inventory has no `todo` rows.

## Phase 5: Self-hosting
The native toolchain and `lldb` on NeoDarwin (P5-10), Bazel through an OpenJDK port (P5-11), and NeoDarwin building NeoDarwin byte-identically to CI (P5-12). **Exit:** a system set built on the board with the remote cache off matches CI's artifact and boots.

## Phase 6: Multi-architecture
6a AMD64: `neoboot` x86 mode, IOAPIC controller, ACPICA reuse, driver reuse; boots on a UEFI PC and in QEMU `q35`. 6b RISCV64: toolchain (Mach-O CPU type, lld, dyld), user-mode bring-up, `osfmk/riscv64` and pmap, QEMU `virt`. **Exit:** the same repository builds and boots three ISAs in CI.

## Phase 7: Hardware breadth and release
GPU kernel drivers with Mesa userland where licences allow (P7-01), power management (P7-02), audio hardware drivers (P7-08), Wi-Fi, the text installer, release images, the documentation site and the 1.0 release (P7-03). P7-03 is on the 1.0 path; the rest of the phase is not.

## Parallelism
Phase 2's packaging and ports tooling is userland-only and starts during Phase 1 on macOS hosts. The ports recipe format and importers can be exercised on macOS before NeoDarwin runs them. Phase 3's `zfs.kext` port can begin as soon as the M4 kernel boots with a block device. Phase 4's inventory (P4-20) needs only the FreeBSD tree and can start now; P4-21 continues the command projects of P1-08. Phase 6a can start once Phase 1's ACPICA kext exists. Magi's host-side work (renderer, theme engine, namespaces) runs independently and waits on NeoDarwin only where its backlog says `neodarwin:<id>`.

## Re-evaluation checklist (run at each phase boundary)
1. Did any kernel blueprint risk change state? Update the DT-ABI and this file.
2. Did a new upstream drop (Apple, OpenZFS, FreeBSD stable) land? Run `upstream-bump`; count patches over budget; regenerate the parity inventory.
3. Which epics were split, merged or dropped? Reflect this in `backlog.yaml`.
4. Are exit tests still automated? Any manual exit test becomes a P0 epic.
5. Did a downstream file a mechanism proposal (`downstream.md` §5)? Accept it into the backlog or record the refusal.
