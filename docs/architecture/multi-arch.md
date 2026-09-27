<!-- SPDX-License-Identifier: BSD-2-Clause -->
# Multi-architecture design: ARM64 → AMD64 → RISCV64

## 1. Seams

Borrowing the VectraOS "portability seams" discipline (`../designs/platform.md`), every architecture binds the same list. Arch-specific code lives *only* behind these seams.

| Seam | ARM64 (SBSA) | AMD64 (UEFI PC) | RISCV64 (UEFI + ACPI, RVA23) |
|---|---|---|---|
| Loader | `neoboot` (aarch64 PE), Apple DT synthesised from ACPI, arm64 `boot_args` | `neoboot` (x86_64 PE), fills the x86 `boot_args` with the EFI memory map (`pexpert/pexpert/i386/boot.h:154-163`); ACPI tables passed by pointer as `boot.efi` does | `neoboot` (riscv64 PE), new `boot_args` mirroring arm64's, DT synthesised from ACPI (MADT RINTC/IMSIC/APLIC, RHCT) |
| Firmware description | ACPI (tier 1 in loader, ACPICA kext) | ACPI (ACPICA kext replaces closed AppleACPIPlatform) | ACPI |
| Interrupt controller | GICv3 in kernel + `GICv3InterruptController` | x2APIC/IOAPIC already in tree (`osfmk/i386`); IOKit `IOInterruptController` for IOAPIC needed (closed today) | PLIC + AIA (APLIC/IMSIC): new |
| Timer | ARM generic timer (in tree) | TSC + HPET/LAPIC timer (in tree) | SBI timer / Sstc: new |
| CPU start | PSCI via `IOPMGR` | INIT/SIPI in tree (`osfmk/i386/mp.c`) | SBI HSM: new |
| Page tables | 16K/4K VMSA (in tree) | 4-level 4K (in tree) | Sv48/Sv39: new pmap |
| Cache/coherency | coherent by board flag | coherent | per board (`Zicbom`) |
| Platform expert | `NeoDarwinPlatformExpert` (in kernel) | `NeoDarwinPCPlatformExpert` (ACPI-driven) | shared with ARM64 where DT-shaped |
| Console | PL011/16550 pexpert | 16550/`PC serial` in tree | 16550, SBI console |
| Toolchain | clang `aarch64`, `ld64.lld` | clang `x86_64`, `ld64.lld` | clang `riscv64` + **new Mach-O `CPU_TYPE_RISCV64`** in LLVM, lld, dyld, `kcgen` |

## 2. AMD64 plan (Phase 6a)

XNU x86_64 is in tree and boots on PCs today with third-party loaders. What NeoDarwin adds: `neoboot` x86 mode (the `boot_args` and memory-map contract is documented and stable), the ACPICA kext (already built for ARM64), an open `IOInterruptController` for IOAPIC/MSI, an EFI runtime NVRAM shim or the same in-memory NVRAM as ARM64, and the driver families which are architecture-neutral. Effort is dominated by drivers and by the closed-kext gap, not by the kernel.

## 3. RISCV64 plan (Phase 6b, long horizon)

XNU has no RISC-V code. This is a full new kernel architecture: `osfmk/riscv64` (locore, trap, context switch, pmap for Sv48), `pexpert/riscv64`, `libkern` and `libsyscall` arch files, dyld and compiler-rt support, and the toolchain gap above. Prerequisites that make it tractable: the ARM64 platform layer already separates loader/DT/platform-expert from the ISA; the RISC-V ACPI ecosystem (RVA23, SBSA-like "BRS" profile) is stabilising; reference kernels in the workspace (`../linux`, `../fuchsia`, `../seL4`, `../freebsd-src`) all have RISC-V ports to consult. Sequence: toolchain (Mach-O CPU type, lld, dyld) → user-mode bring-up under a Linux host (`dyld` + libSystem on a RISC-V Linux box via a syscall shim) → kernel `locore`/pmap under QEMU `virt` → platform expert reuse.

## 4. Rules to keep the door open now

1. No `#if __arm64__` outside `osfmk/arm*`, `pexpert/arm*`, `bsd/dev/arm64`, the arch directories of base libraries, and `platforms/`. New shared code uses seam interfaces.
2. The DT-ABI (`docs/kernel/arm64-sbsa-bringup.md` §2.2) is written arch-neutrally: MPIDR becomes "hart id", GIC becomes "interrupt controller node" with a `compatible` string.
3. Build: every target declares `target_compatible_with`; CI builds the host tools for all three targets even before they run.
4. Package `arch` field and Mach-O fat/universal support are in the manifest schema from v1.
