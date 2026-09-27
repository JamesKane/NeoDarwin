<!-- SPDX-License-Identifier: BSD-2-Clause -->
# boot/neoboot

The UEFI loader (`BOOTAA64.EFI`): ACPI static tables to the Apple-format device tree, Mach-O fileset loading, chained fixups, `boot_args`. Embedded Swift with a C shim for the firmware ABI. Design: `docs/kernel/arm64-sbsa-bringup.md` §2.1. Epics P0-09, P1-03, P1-04.
