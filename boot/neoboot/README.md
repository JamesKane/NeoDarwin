<!-- SPDX-License-Identifier: BSD-2-Clause -->
# boot/neoboot

The UEFI loader (`BOOTAA64.EFI`), in Embedded Swift (language policy T3). Design: `docs/kernel/arm64-sbsa-bringup.md` §2.1.

**Current stage (P1-04):** the loader boots the kernel on a machine it knows only from ACPI. `efi_main` (`Sources/Main.swift`) does the following:
- finds the RSDP in the UEFI configuration table and reads the MADT, GTDT, SPCR and FADT, printing the CPUs, GIC, timer and UART it found. It stops with a reason if the tables are missing, broken, or describe something the kernel can't run on;
- reads `GICD_CTLR` to choose the timer's GIC group (P1-05): Group 1, an IRQ, when the GIC has two security states (`DS` = 0) and Group 0 is Secure; Group 0, a FIQ, otherwise. `timer-group=1` in `boot.cfg` forces Group 1;
- reads `\NeoDarwin\kernelcache` from the ESP, plus the command line from `\NeoDarwin\boot.cfg` and the ramdisk from `\NeoDarwin\ramdisk` if present. With `dump-acpi` in `boot.cfg` it first prints every ACPI table in Linux `acpidump` format;
- reads the time of day from UEFI's `GetTime()`, for the kernel's clock (`/chosen/neodarwin,utc-seconds`);
- finds a Graphics Output Protocol with a linear 32-bit framebuffer, preferring the one on the console, keeps the firmware's mode, and passes it in `boot_args.Video` for the kernel's framebuffer console (`Sources/GOP.swift`, bring-up doc §2.1.7). `gop=off` in `boot.cfg` passes none. Without a GOP, or with a BltOnly or bit-mask one, the console is serial only;
- chooses the kernel's console: the SPCR UART if the kernel drives it (a PL011, SBSA Generic UART, or since P1-12 the Radxa Dragon Q8B's Qualcomm GENI UART, SPCR types 0x11 and 0x13, `"qcom,geni-debug-uart"`), else the framebuffer alone, leaving the UART out of the tree (DT-ABI v1.1). `uart=off` in `boot.cfg` treats the SPCR UART as absent. Without a GOP, a UART the kernel can't drive (the 16550 family, for now) is refused. After `ExitBootServices` neoboot writes only to a PL011 that is the kernel's console, never to a GENI UART (`docs/kernel/serial.md`);
- takes the largest hole-free run of RAM the kernel may own, minus the framebuffer if it lies there, and places the flat collection at the lowest free address congruent to its link address modulo 32 MiB (slide 0);
- writes the Apple-format device tree after it, synthesised from ACPI to DT-ABI v1 (`docs/kernel/dt-abi.md`), and checks it against the ABI;
- copies the ACPI tables next to the tree, with their pointers rewritten, and publishes them as `/chosen/memory-map/ACPITables`;
- places the ramdisk, publishes it as `/chosen/memory-map/RAMDisk`, and roots on it with `rd=md0` unless the command line names a root. Without a ramdisk it reads the GPT of the disk it was loaded from and passes the unique GUID of its first HFS+ partition as `/chosen boot-uuid`; `boot-uuid=<UUID>` in `boot.cfg` names the root instead (P1-10, `docs/kernel/storage.md`);
- appends `cpus=1` on a multiprocessor until SMP works (P1-06), then writes `boot_args`;
- cleans the caches, exits boot services, and enters `_start` at EL1 with the MMU off, dropping from EL2 first if the firmware ran there.

It applies no fixups; the kernel does. Design: `docs/kernel/arm64-sbsa-bringup.md` §2.1.

| Path | Contents |
|---|---|
| `Sources/` | Swift; builds with `-no-allocations` |
| `Sources/Portable/` | the ACPI parser, tree synthesis, tree writer and DT-ABI check. They import nothing from UEFI, so `//tools/dtdump` compiles the same files for the host |
| `testdata/` | QEMU `virt` ACPI tables captured with `dump-acpi` (1, 4 and 18 CPUs, and with TF-A), QEMU `sbsa-ref`'s (SbsaQemu, 4 CPUs), edited copies (a truncated MADT, an Aff3 MPIDR, the Q8B's GENI SPCR), the Radxa Dragon Q8B's own tables, and the `boot.cfg` files the tests use |
| `UEFI/` | the UEFI 2.10 structures neoboot uses, as a C header and module map |
| `runtime/mem.c` | `memset`, `memcpy`, `memmove` and `__chkstk`, which the compiler emits calls to |
| `runtime/arm64.c` | system-register reads, cache cleaning, and the MMU-off/EL2-to-EL1 entry into the kernel (inline assembly; T4 with justification) |

| Target | What it does |
|---|---|
| `bazel build //boot/neoboot` | `bazel-bin/boot/neoboot/BOOTAA64.EFI`, a PE32+ AArch64 EFI application |
| `bazel test //boot/neoboot:neoboot_qemu_test` | boots it on QEMU `virt` with EDK2 and an ESP with no kernel, and checks what it read from ACPI and that it reports the missing kernel; needs `brew install qemu`. `neoboot_smp4_qemu_test` does the same with four CPUs, `neoboot_dump_acpi_qemu_test` checks `dump-acpi`, and `neoboot_timer_group1_qemu_test` checks `timer-group=1`, `neoboot_gop_qemu_test` finds QEMU's `ramfb` framebuffer, and `neoboot_uart_off_qemu_test` and `neoboot_uart_off_no_gop_qemu_test` check `uart=off` with and without one |
| `bazel test //tools/dtdump:all` | the same parser and synthesis on the host, against the captured tables |
| `bazel test //kernel:sbsa_boot_test` | boots the real kernel collection with the PID 1 ramdisk to userland (kernel CI job) |
| `bazel test //kernel:sbsa_disk_boot_test` | boots `//images:session_disk`, a GPT disk image with neoboot on its ESP, from virtio-blk: the root by boot-uuid, twice |
| `bazel test //kernel:sbsa_fb_console_boot_test` | the getty session with `ramfb`: checks the shell's output on the screen, read back from a screendump |
| `bazel test //kernel:sbsa_fb_only_boot_test` | the same with `uart=off`: the framebuffer is the kernel's only console, and getty's prompt must appear on the screen |

How the image is built, and why it is sound on AArch64, is in `toolchains/README.md`.
