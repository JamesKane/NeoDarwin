<!-- SPDX-License-Identifier: BSD-2-Clause -->
# boot/neoboot

The UEFI loader (`BOOTAA64.EFI`), in Embedded Swift (language policy T3). Design: `docs/kernel/arm64-sbsa-bringup.md` §2.1.

**Current stage (P1-03):** the loader boots the kernel. `efi_main` (`Sources/Main.swift`) does the following:
- reads `\NeoDarwin\kernelcache` from the ESP, plus the command line from `\NeoDarwin\boot.cfg` and the ramdisk from `\NeoDarwin\ramdisk` if present;
- reads the time of day from UEFI's `GetTime()`, for the kernel's clock (`/chosen/neodarwin,utc-seconds`);
- takes the largest hole-free run of RAM the kernel may own, and places the flat collection at the lowest free address congruent to its link address modulo 32 MiB (slide 0);
- writes an Apple-format device tree after it (hand-written for QEMU `virt` until P1-04), then the ramdisk, which it publishes as `/chosen/memory-map/RAMDisk` and roots on with `rd=md0` unless the command line names a root, then `boot_args`;
- cleans the caches, exits boot services, and enters `_start` at EL1 with the MMU off, dropping from EL2 first if the firmware ran there.

It applies no fixups; the kernel does. Design: `docs/kernel/arm64-sbsa-bringup.md` §2.1.

| Path | Contents |
|---|---|
| `Sources/` | Swift; builds with `-no-allocations` |
| `UEFI/` | the UEFI 2.10 structures neoboot uses, as a C header and module map |
| `runtime/mem.c` | `memset`, `memcpy`, `memmove`, which the compiler emits calls to |
| `runtime/arm64.c` | system-register reads, cache cleaning, and the MMU-off/EL2-to-EL1 entry into the kernel (inline assembly; T4 with justification) |

| Target | What it does |
|---|---|
| `bazel build //boot/neoboot` | `bazel-bin/boot/neoboot/BOOTAA64.EFI`, a PE32+ AArch64 EFI application |
| `bazel test //boot/neoboot:neoboot_qemu_test` | boots it on QEMU `virt` with EDK2 and an ESP with no kernel, and checks that it says so; needs `brew install qemu` |
| `bazel test //kernel:sbsa_boot_test` | boots the real kernel collection with the PID 1 ramdisk to userland (kernel CI job) |

How the image is built, and why it is sound on AArch64, is in `toolchains/README.md`.
