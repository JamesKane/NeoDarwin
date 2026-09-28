<!-- SPDX-License-Identifier: BSD-2-Clause -->
# boot/neoboot

The UEFI loader (`BOOTAA64.EFI`), in Embedded Swift (language policy T3). Design: `docs/kernel/arm64-sbsa-bringup.md` §2.1.

**Current stage (P0-09):** a toolchain proof. It prints through the firmware console and directly to the PL011 with volatile MMIO, then powers off through UEFI Runtime Services. The loader proper, which loads the kernel collection, synthesises the device tree from ACPI and hands off `boot_args`, grows from here in P1-03 and P1-04.

| Path | Contents |
|---|---|
| `Sources/` | Swift; builds with `-no-allocations` |
| `UEFI/` | the UEFI 2.10 structures neoboot uses, as a C header and module map |
| `runtime/mem.c` | `memset`, `memcpy`, `memmove`, which the compiler emits calls to |

| Target | What it does |
|---|---|
| `bazel build //boot/neoboot` | `bazel-bin/boot/neoboot/BOOTAA64.EFI`, a PE32+ AArch64 EFI application |
| `bazel test //boot/neoboot:neoboot_qemu_test` | boots it on QEMU `virt` with EDK2 and checks the serial log; needs `brew install qemu` |

How the image is built, and why it is sound on AArch64, is in `toolchains/README.md`.
