<!-- SPDX-License-Identifier: BSD-2-Clause -->
# QEMU `sbsa-ref`: TF-A `qemu_sbsa` + EDK2 SbsaQemu

**P1-09, P1-10.** `sbsa-ref` is QEMU's SBSA reference machine. Unlike `virt` it has no firmware tables of QEMU's own: TF-A runs at EL3 and EDK2's SbsaQemu platform (edk2-platforms) builds the ACPI tables from its own ASL, as a board vendor's firmware would. Its memory map is a server's rather than `virt`'s: DRAM starts at 1 TiB. It is the machine P1-09's and P1-10's exits name.

## Firmware: built from pinned source

`//third_party/qemu_firmware:sbsa_ref_flash` builds two images with `tools/efi/build_secure_firmware.sh --sbsa`, the script that builds the `virt,secure=on` image (`qemu-secure.md`), with the same toolchain (the swift.org toolchain's clang and `ld.lld`), host tools (`brew install make gnu-sed acpica openssl@3`) and reproducibility measures (fixed dates, a fixed build path with a pid lock, seeded stack cookies, `PYTHONHASHSEED=0`):

| Output | Contents |
|---|---|
| `sbsa_ref_flash0.fd` | Secure flash (pflash unit 0): TF-A `PLAT=qemu_sbsa` BL1 at 0, its FIP (BL2, BL31) at 0x12000 |
| `sbsa_ref_flash1.fd` | Non-secure flash (pflash unit 1): EDK2 SbsaQemu at 0, which BL31 enters in place at EL2, and its variable store at 3 MiB |

Both are padded to the machine's 256 MiB as sparse files. Pins (`MODULE.bazel`):

| Repository | Upstream | Licence |
|---|---|---|
| `@arm_tf_a` | TF-A `v2.15.0`, plus `third_party/qemu_firmware/patches/tf-a-qemu-sbsa-hold-pen.patch` (upstream 5c33fafc, below) | BSD-3-Clause |
| `@tianocore_edk2` and its submodules | `edk2-stable202608` (as for `virt,secure=on`) | BSD-2-Clause-Patent |
| `@edk2_platforms` | edk2-platforms `061beb4c` (2026-09-02), `Platform/Qemu` and `Silicon/Qemu` | BSD-2-Clause-Patent |

The edk2-platforms commit is the last before its platforms dropped the unified GIC driver (2026-09-09), which `edk2-stable202608` still has. **edk2-non-osi isn't needed**: SbsaQemu takes only TF-A's `bl1.bin` and `fip.bin` from it (`SbsaQemu.fdf`: `Platform/Qemu/Sbsa/{bl1,fip}.bin`), and the script puts the ones it has just built on `PACKAGES_PATH` in its place. TF-A is therefore built first. EDK2 takes a path that starts with the string `$WORKSPACE` as inside the workspace, so edk2-platforms is copied to `…/platforms`, not `…/edk2-platforms` next to `…/edk2`.

**The TF-A fix.** v2.15.0 moved the QEMU platforms' secondary-CPU hold pen to the common `plat_hold_pen` (a 64-byte slot per core with two magic tags and the entry point), in `plat_helpers.S`, which `qemu_sbsa` shares, but left `qemu_sbsa`'s `sbsa_pm.c` writing the old 8-byte GO flags. PSCI `CPU_ON` returned SUCCESS and the core spun in BL1 (QEMU monitor: PC 0x35d8 at EL3 on CPUs 1–3). Upstream fixed it after the release ("fix(qemu-sbsa): align hold pen implementation with qemu-virt"); the script applies that commit (`--tfa-patch`) to the sbsa build only, so the `virt,secure=on` image is unchanged.

**Reproducible.** With swift-6.3.2's clang: flash0 SHA-256 `ca0374c38eb5537c9383d4f69cf92ae17caaa110c52aafd430fef10aa0cbb9f9`, flash1 `e9555ae38efe05aa3e4ac31abc779b9ce0dd3f2effef272e677cc33ca6ba6740`, the same from a manual build and a Bazel one. The build takes about a minute and runs at `/tmp/neodarwin-qemu-sbsa-fw`, so it can run at the same time as the `virt,secure=on` one (`/tmp/neodarwin-qemu-secure-fw`, whose image is unchanged: `e9e0f4b9…`). `ND_SECURE_FW_KEEP=DIR` keeps the build trees and logs (also of a failed build) and makes EDK2's build verbose.

## The machine

| | `sbsa-ref` (QEMU 11.1, SbsaQemu's tables) |
|---|---|
| CPUs | 4 × `neoverse-n2` by default in the tests (the machine's default CPU); MPIDR Aff0 = index, 8 per Aff1 |
| DRAM | from 0x100_0000_0000 (1 TiB); 2 GiB in the tests. neoboot's window: 0x100_0000_0000–0x100_7e80_0000 |
| GIC | GICv3, GICD 0x40060000, GICR 0x40080000 (a 64 MiB range, one 128 KiB frame per CPU), ITS 0x44081000; two security states (`GICD_CTLR` 0x12 from Non-secure: DS=0), so the timer is on Group 1 |
| UART | PL011 at 0x60000000 (SPCR type 3), GSIV 33 |
| PSCI | SMC, as SbsaQemu's FADT says |
| UEFI | at **EL2** (`current EL 0x2`); PSCI `CPU_ON` enters secondaries at EL2 too |
| Counter | 1 GHz |
| Platform devices (DSDT) | AHCI `LNRO001E` at 0x60100000 (irq 42), XHCI `PNP0D10` at 0x60110000 (irq 43, `_CCA` 1); no AHCI driver in NeoDarwin yet, the XHCI is the console USB keyboard's since P1-18 (`usb-console.md`) |
| PCIe | ECAM 0xf0000000 (`_CBA`, MCFG), windows 0x80000000+0x70000000, 0x1_0000_0000+0xff_0000_0000, I/O at 0x7fff0000; `_PRT` through link devices `\_SB.PCI0.GSI0`–`GSI3` (GSIV 35–38) |
| On bus 0 | 00:00.0 host bridge 1b36:0008, 00:01.0 e1000e 8086:10d3, 00:02.0 bochs-display 1234:1111 (the GOP framebuffer, BAR 0 at 0x80000000); `-device` adds from 00:03.0 |
| IORT | root complex → SMMUv3 at 0x60050000 → ITS. The firmware doesn't enable the SMMU (`SMMU_CR0.SMMUEN` = 0), so QEMU passes DMA and MSIs through untranslated; `NeoDarwinPCIMSI` reads that and logs `(bypass)` |
| ACPI tables | FACP DSDT DBG2 MCFG SPCR IORT APIC SSDT PPTT GTDT BGRT, OEM `LINARO SBSAQEMU` (`boot/neoboot/testdata/qemu-sbsa-ref-smp4.acpidump`) |

SbsaQemu's console wraps lines at 80 columns, so neoboot's longer lines arrive in two pieces; tests match the part before the wrap.

## Harness

`tools/efi/qemu_efi_test.sh --machine sbsa-ref --firmware FLASH0 --firmware-ns FLASH1` runs `-M sbsa-ref` with flash0 read-only and a copy of flash1 (the guest writes its variables). The CPU defaults to `neoverse-n2` and RAM to 2G. The FAT ESP (QEMU's vvfat) goes on the machine's default block interface, the platform AHCI, from which SbsaQemu boots it; `--disk IMAGE --disk-device nvme,serial=nd0` and `--device` put devices on the PCIe root bus. `qemu_disk_reboot_test.sh` passes the options through to both boots.

## What NeoDarwin needed

| Finding | Fix |
|---|---|
| The first fetch after `start.s` turned the MMU on took an instruction abort, ESR 0x86000000: an address size fault at level 0. XNU's `TCR_EL1.IPS` is 40 bits (42 on some Apple SoCs); the kernel and its tables are above 2^40 | Patch 0030: IPS 48 bits on `GENERIC_ARM64_PLATFORM`, lowered at every TCR write to `ID_AA64MMFR0_EL1.PARange` (Cortex-A76 and the Q8B's cores have 40 bits), `get_tcr()` normalised. The rest of the kernel (physmap, pmap, VM) needed nothing for DRAM at 1 TiB; neoboot's window selection already took the largest conventional run wherever it is |
| With `-smp 4`, `CPU_ON` returned SUCCESS but no secondary came up; the kernel panicked "cpu 1 failed to boot for the first time" when the scheduler first needed one | Two causes. TF-A v2.15.0's `qemu_sbsa` hold-pen mismatch (above: upstream fix applied). Then, the secondaries entered the kernel's reset vector at EL2, since UEFI runs at EL2 and PSCI enters at the highest Non-secure EL, while `start.s` programs EL1 only: patch 0031 leaves EL2 at the top of `reset_vector` as neoboot does for the boot CPU |
| neoboot's EL2 exit didn't set `VPIDR_EL2`/`VMPIDR_EL2` (what EL1 reads as MIDR and MPIDR; UNKNOWN at reset) or `ICC_SRE_EL2` (EL1's GIC system registers). QEMU and EDK2 happened to leave usable values | `nd_enter_kernel` sets both from the real registers and ORs in `ICC_SRE_EL2.SRE|Enable`, as patch 0031 does for the secondaries |
| With four CPUs and several PCI devices, every device was `INTA unrouted`: the host bridge driver matched `\_SB.PCI0` and enumerated while the namespace walk had yet to publish PCI0's children `GSI0`–`GSI3`, the link devices its `_PRT` names. On `virt` the same race was lost less often | `NeoDarwinACPIPlatform` registers the service-plane nubs only after the whole walk (`acpi.md`) |
| `GTDT` platform timers, the SBSA watchdog and AHCI are described but unused | Nothing needed: NeoDarwin uses the virtual timer PPI and has no drivers for them (the XHCI has one since P1-18) |

Nothing was needed for GICD/GICR at 0x4006_0000 with a 64 MiB GICR range, the ITS, the PL011 at 0x60000000, PSCI over SMC or DS=0: the ACPI-driven paths of P1-04–P1-09 took the new addresses as they come.

## Tests

| Target | What it boots | Asserts |
|---|---|---|
| `//boot/neoboot:neoboot_sbsa_ref_qemu_test` (qemu) | neoboot alone, 4 CPUs | TF-A, the MADT's GIC and four GICR frames, the SPCR PL011, PSCI SMC, DS=0 |
| `//tools/dtdump:qemu_sbsa_ref_smp4_test` | — | the tree neoboot builds from the captured tables, byte for byte (DRAM at 1 TiB, 1 GHz, DS=0, loader at EL2) |
| `//kernel:sbsa_ref_boot_test` (manual) | the session from the HFS+ ramdisk | the DRAM window at 1 TiB, EL2, PSCI over SMC, 4 of 4 CPUs online with IPIs, the timer on Group 1, root's shell, `ncpu-4-4`, a timed `sleep 1` |
| `//kernel:sbsa_ref_pci_boot_test` (manual) | `pid1_root` with `PCI_DEVICES` (virtio-blk, NVMe, edu, two root ports) | the ACPI summary and nubs (COM0, AHC0, USB0, PCI0 and its links), the host bridge, e1000e, bochs-display, the added devices and bridges, INTx through the link devices (SPI 36, shared SPI 38), MSI and MSI-X through the ITS via the SMMUv3 in bypass, both virtio-blk disks and the NVMe namespace — **P1-09's exit** |
| `//kernel:sbsa_ref_usb_kbd_boot_test` (manual) | the session, a `usb-kbd` on the platform XHCI | `NeoDarwinXHCI` on `\_SB.USB0` (PNP0D10, GSIV 43), the keyboard's first event by the SPI; login and commands typed on the USB keyboard only (P1-18, `usb-console.md`) |
| `//kernel:sbsa_ref_nvme_boot_test` (manual) | the GPT `session_disk` on QEMU's NVMe controller, twice | EDK2 boots neoboot from the disk's ESP over NVMe, root on `disk0s2` by boot-uuid, four I/O queue pairs on MSI-X through the ITS; a file written in the first boot is read in the second — **P1-10's exit** |

## For the Radxa Dragon Q8B

- **Physical address size.** The Q8B's cores implement 40 bits; patch 0030 lowers the IPS to that at run time, so the kernel sets a size the CPU has rather than relying on the architecture's fallback.
- **UEFI at EL2.** If the Q8B's UEFI leaves the loader at EL2 (without Qualcomm's hypervisor), secondaries now come up too (patch 0031); with the hypervisor at EL2 the loader runs at EL1 and neither path is taken.
- **SMMU.** sbsa-ref's firmware leaves its SMMUv3 disabled, which QEMU treats as bypass; the Q8B's SMMU is firmware-reserved and assumed to bypass the NVMe stream as well (`storage.md`). An SMMU that translates or aborts is not handled.
- **Vendor ASL.** SbsaQemu's tables are hand-written like a vendor's: link devices under the host bridge (not `\_SB`), a `_UID` string, a `_PRT` whose first device has all four entries on pin 0. They exposed the registration race; the Q8B's `_PRT` has direct GSIVs.
