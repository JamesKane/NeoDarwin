# Kernel bring-up: XNU on ARM64 SBSA boards (UEFI + ACPI)

> **Scope.** This document is the *kernel bring-up* design for the ARM64 lane of NeoDarwin. The project-wide charter, build system, packaging, drivers, graphics, filesystems, namespaces and multi-arch designs live under `docs/architecture/`; the phased plan lives in `roadmap/`. Milestones M0–M7 below map onto roadmap Phase 1 (`P1-*` epics).

_XNU (xnu-12377.1.9, macOS 26 era) as a standalone OS on generic ARM64 SBCs via UEFI + ACPI (SBSA/SBBR)._
_Drafted 2026-09-27 against `/Users/jkane/Projects/OS/xnu` (git f6217f8). Every claim below cites a file in that tree._

---

## 0. Executive summary (read this first)

1. **The starting point is closer than older XNU drops suggest.** xnu-12377's `VMAPPLE` board config (Apple's Virtualization.framework guest target) ships in-tree:
   - a **GICv3 redistributor/distributor bring-up** with the virtual-timer PPI routed as a Group 0 FIQ (`pexpert/arm/pe_fiq.c:71-172`, register map in `pexpert/pexpert/arm64/VMAPPLE.h`);
   - a **PL011 UART** console driver matched by `compatible = "arm,pl011"` (`pexpert/arm/pe_serial.c:646-732, 810`);
   - a build that uses a **generic ISA** (`-march=armv8.5a+sme2`, `makedefs/MakeInc.def:87`) rather than `-mcpu=apple-*`;
   - an in-kernel SMP/interrupt glue layer, **AppleARMSMP**, compiled into the kernel proper (`iokit/Kernel/arm/AppleARMSMP.cpp`) that talks to a platform "IOPMGR" and a child `IOInterruptController` through public IOKit classes.
   **Decision: the `SBSA` board config is forked from `VMAPPLE`, not written from zero.**

2. **XNU has zero ACPI code on any architecture.** `osfmk/i386/acpi.h` is a sleep/wake shim; table parsing on Intel Macs lives in the closed `AppleACPIPlatform.kext`. "Extract x86 XNU ACPI logic" is therefore not an option. **Decision: two-tier ACPI.** The UEFI loader consumes the *static* tables (MADT, GTDT, SPCR, MCFG, DBG2, memory map) and emits the **Apple-format device tree the kernel already expects**; a later in-kernel **ACPICA** kext handles the *dynamic* namespace (DSDT/SSDT, `_CRS`, `_PRT`, `_DSD`) for PCIe and peripherals.

3. **The kernel's boot contract is narrow and fully known.** `boot_args` (`pexpert/pexpert/arm64/boot.h:66-80`) describes exactly one contiguous DRAM window, a pointer to an Apple-format DT, a command line and an optional framebuffer. Entry is `start_first_cpu` with `x0 = &boot_args` (physical), EL1, MMU off, interrupts masked (`osfmk/arm64/start.s:462-520`). Chained fixups are pre-applied by the booter in the non-SPTM build (only `osfmk/arm64/sptm/arm_init_sptm.c` self-slides). The loader must do what iBoot does.

4. **Three things will break first, and each has a designed answer** (§5): the *single-window DRAM + fixup handoff*, the *GICv3 Group 0 FIQ assumption* (works under Apple's hypervisor and under QEMU, fails on real hardware with TrustZone because Group 0 is Secure), and the *closed-kext / kernelcache-tooling gap* (solved by compiling the platform expert into the kernel image for the first milestones, exactly as AppleARMSMP already is).

5. **Terminal OS = M4** (HFS+ ramdisk root, open launchd, shell on the PL011 console). Graphics groundwork is the UEFI GOP framebuffer passed through `Boot_Video`, which the kernel already consumes (`pexpert/arm/pe_init.c:425-436`).

---

## 1. High-level architecture and boot flow

```
UEFI firmware (SEC → PEI → DXE → BDS)                                   [firmware]
  │  loads ESP:\EFI\BOOT\BOOTAA64.EFI
  ▼
neoboot (UEFI application, "UEFI Loader Agent")                         [loader, EL2 or EL1, MMU on]
  1. Locate RSDP via EFI configuration table; parse XSDT → MADT, GTDT, SPCR/DBG2, MCFG, IORT (static tables only)
  2. Read ESP:\NeoDarwin\kernelcache (MH_FILESET), ESP:\NeoDarwin\ramdisk.img, ESP:\NeoDarwin\boot.cfg
  3. GetMemoryMap → choose the largest hole-free EfiConventional run ≥ 512 MiB → this is [physBase, physBase+memSize)
  4. AllocatePages inside that run: [kernelcache | Apple-DT | ramdisk | boot_args], kernel base 2 MiB-aligned (L2 boundary)
  5. Apply LC_DYLD_CHAINED_FIXUPS with slide = load_vaddr − VM_KERNEL_LINK_ADDRESS (KASLR chosen here)
  6. Synthesise Apple-format DT from ACPI (+ CNTFRQ_EL0, MPIDR list, RSDP address, memory-map)
  7. Fill boot_args {Revision=2, Version=2, virtBase, physBase, memSize, topOfKernelData, Video(from GOP), deviceTreeP, CommandLine, bootFlags, memSizeActual}
  8. ExitBootServices; mask DAIF; if CurrentEL==2: program HCR_EL2/CNTHCTL_EL2/CNTVOFF_EL2=0/CPTR_EL2, ERET to EL1
  9. Clean D-cache to PoC for all loaded regions, invalidate I-cache, SCTLR_EL1.M=0 (MMU off), TLBI
 10. br start_first_cpu  (x0 = physical address of boot_args)
  ▼
XNU start.s → start_first_cpu                                            [kernel, EL1, MMU off → on]
  • builds V=P + KVA bootstrap tables, TCR/MAIR, TTBR1, enables MMU                (osfmk/arm64/start.s:462-720)
  ▼
arm_init(boot_args*)                                                    (osfmk/arm/arm_init.c:343)
  • PE_init_platform(FALSE): SecureDTInit, pe_identify_machine (cpus/timebase-frequency, /arm-io ranges)
  • serial: /defaults/serial-device → node compatible "arm,pl011" → pl011_uart_setup   (pe_serial.c:711, 837)
  • arm_vm_init: physmap over [physBase, physBase+memSize), frees above topOfKernelData    (arm_vm_init.c:1815-1965)
  • /chosen dram-base/dram-size, random-seed                             (arm_init.c:545-559, pe_gen.c:178)
  • ml_parse_cpu_topology (/cpus/cpu@N: reg=MPIDR, cluster-type, …)     (machine_routines.c:1125-1330)
  • PE_init_platform(TRUE) → pe_arm_init_interrupts → ml_init_timebase; pe_init_fiq (GICR wake, PPI27 grp0)
  • machine_startup → kernel_bootstrap (zones, scheduler, IPC, VM)
  ▼
IOKit                                                                   (pexpert/arm/pe_init.c:251-380)
  • IODeviceTreeAlloc: DT → IORegistry (IODTPlane)
  • IOPlatformExpertDevice root nub → matches **NeoDarwinPlatformExpert** (IODTPlatformExpert subclass, compiled in)
  • NeoDarwinPlatformExpert publishes: GICv3InterruptController (child of PassthruInterruptController),
    PSCIPowerManager (IOPMGR), memdev ramdisk from /chosen/memory-map/RAMDisk
  • AppleARMSMP::cpu_boot_thread → ml_processor_register per CPU → PE_cpu_start_internal → IOPMGR::enableCPUCore
      → PSCI CPU_ON(mpidr, LowResetVectorBase phys) → secondary enters start.s reset vector → start_cpu → arm_init_cpu → pe_init_fiq
  • (M5+) NeoDarwinACPIPlatform kext: ACPICA namespace → IOACPIPlatformDevice nubs → IOPCIFamily (MCFG ECAM) → storage
  ▼
BSD                                                                     (bsd/kern/bsd_init.c:485)
  • IOFindBSDRoot: RAMDisk → md0 (IOKitBSDInit.cpp:766-826) → mountroot: mockfs (M3) → HFS+ (M4) → real volume (M5)
  • load_init_program → /sbin/launchd                                    (bsd/kern/kern_exec.c:7344, 7406)
  ▼
launchd → getty on /dev/console (kernel serial console, PL011) → sh      [terminal OS reached]
```

### Address-space picture the loader must produce

```
physBase ──────────────────────────────────────────────────────────── physBase+memSize
│ pad (≥2 MiB, L2-aligned) │ kernelcache (fileset) │ DT │ ramdisk │ boot_args │ ← topOfKernelData │ free … │
   virtBase maps physBase 1:1 offset; kernel KVA = VM_KERNEL_LINK_ADDRESS + slide
```
The kernel creates a linear "physmap" of exactly `[physBase, physBase+memSize)` (`arm_vm_init.c:1820-1869`) and hands everything above `topOfKernelData` to the VM. Memory outside the window is invisible to XNU. `memSizeActual` may report the machine total for `hw.memsize`.

---

## 2. Subsystem migration and bridge strategy

### 2.1 UEFI loader → kernel handoff ("neoboot")

**Format:** A UEFI application (`BOOTAA64.EFI`) written in **Embedded Swift** with a C shim for the UEFI calling convention (language policy T3); a C build of the same loader is kept as the fallback until the Swift build boots the kernel on QEMU and hardware. It needs no runtime services after `ExitBootServices` and never returns.

**What it loads:** an `MH_FILESET` kernelcache. Rationale: on arm64 `OSKext` disables kext requests to userland (`libkern/c++/OSKext.cpp:354-356`) and safe-boot handling assumes the booter loaded a kext collection (`OSKext.cpp:1015-1023`). For milestones M0–M4 the fileset contains only the kernel because the platform expert is compiled in (§2.3); real kexts appear at M5.

**Loading rules (mirrors iBoot):**
| Step | Rule | Why (source) |
|---|---|---|
| Placement | Load all fileset segments contiguously preserving link-time relative layout; base 2 MiB aligned | `start.s:553-575` derives kernel base from `_mh_execute_header` and assumes an L2-boundary base |
| Fixups | Walk `LC_DYLD_CHAINED_FIXUPS` and rebase every pointer by `slide` | Non-SPTM kernel does not self-slide (`kernel_collection_slide` only in `sptm/arm_init_sptm.c:289`) |
| KASLR | `slide` = random multiple of 16 KiB inside the chosen window | `vm_kernel_slide` is computed from the mach header position (`arm_vm_init.c:105`) |
| boot_args | `Revision=2 Version=2`; `virtBase = VM_KERNEL_LINK_ADDRESS + slide − (kernel_phys − physBase)`; pointers (`deviceTreeP`) are given in **virtual** terms (kernel uses them after MMU on via `PE_state.deviceTreeHead`, `pe_init.c:423`) | `boot.h:61-64`, `pe_init.c:412-444` |
| Video | If GOP present: `v_baseAddr, v_rowBytes, v_width, v_height, v_depth=32, v_display=1`; else `v_display=0` (text console) | `pe_init.c:425-436, 536-548` |
| CommandLine | `boot.cfg` contents, e.g. `serial=3 debug=0x14e rd=md0 -v cs_enforcement_disable=1` | `PE_boot_args()` in `pe_bootargs.c` |
| EL | Enter kernel at **EL1**. If firmware hands off at EL2: `HCR_EL2 = RW`, `CNTHCTL_EL2 = EL1PCTEN|EL1PCEN`, `CNTVOFF_EL2 = 0`, `CPTR_EL2` no FP trap, `SCTLR_EL1 = RES1`, `SPSR_EL2 = EL1h + DAIF`, `ERET` | `start.s` never inspects `CurrentEL` and programs only `*_EL1` registers |
| Caches | Clean to PoC every byte the kernel will read with MMU off (image, DT, boot_args, ramdisk), invalidate I-cache, then disable MMU | `start.s` reads boot_args before enabling MMU |

**Memory-map policy:** XNU cannot express holes. The loader selects the largest run of `EfiConventionalMemory` (plus `EfiBootServicesCode/Data` and `EfiLoaderData`, which are reclaimable after `ExitBootServices`) with no non-conventional descriptor inside it. `EfiACPIReclaim`, `EfiACPIMemoryNVS`, `EfiRuntimeServices*` and `EfiReserved` ranges must be *outside* the window; if firmware places one mid-DRAM the loader picks the larger side and logs the loss. ACPI table pages are copied into the window (into the DT-adjacent area, recorded in `/chosen/memory-map` as `ACPITables`) so the kernel-side ACPICA kext can reach them through the physmap.

**Secondary CPU entry:** PSCI `CPU_ON` starts a core with MMU off at the caller's EL with `x0 = context`. XNU's reset vector (`LowResetVectorBase`, `start.s:106-203`) ignores `x0` and identifies itself by `MPIDR_EL1`, matching against `CpuDataEntries` populated from `/cpus`. So the loader has no per-CPU work; it only guarantees that the kernel is at EL1 and that `SMC` from EL1 reaches EL3 (`HCR_EL2.TSC = 0`).

### 2.2 ACPI → kernel: the two-tier strategy

**Tier 1 (loader, static tables → Apple DT).** The kernel's early boot reads a fixed set of DT nodes through `SecureDT*` (`pexpert/gen/device_tree.c`). The DT binary format is Apple's, not FDT: nodes are `{u32 nProperties; u32 nChildren; props[]; children[]}` with each property `{char name[32]; u32 length; u8 value[length] (4-byte aligned)}` (`pexpert/pexpert/device_tree.h:74-90`). The loader emits this directly.

Required node/property contract (the "DT-ABI"), derived from the source reads:

| Node | Property | Source of value | Consumer |
|---|---|---|---|
| `/` | `name="device-tree"`, `model`, `target-type`, `compatible` | SPCR/DBG2 OEM ids, FADT OEMID, hardcoded `"NeoDarwin,sbsa"` | `pe_init.c:451-470`; IODTPlatformExpert `probe` name-match (`IOPlatformExpert.cpp:1615-1627`) |
| `/chosen` | `dram-base`, `dram-size` (u64) | = `physBase`, `memSize` | `arm_init.c:545-559` (panics if absent) |
| `/chosen` | `random-seed` (≥64 B) | EFI_RNG_PROTOCOL or RNDR/CNTPCT mix | `pexpert/gen/pe_gen.c:145-178` → PRNG seed |
| `/chosen` | `debug-enabled` (u32), `osenvironment`, `boot-uuid` | cfg | `pe_init.c:471-520`, `IOKitBSDInit.cpp:428` |
| `/chosen` | `acpi-rsdp` (u64), `acpi-tables` (u64 addr,len) | RSDP | **new**: NeoDarwinACPIPlatform kext (Tier 2) |
| `/chosen/memory-map` | `RAMDisk` (u64 addr,len), `ACPITables`, `BootArgs` | loader allocation | `IOKitBSDInit.cpp:766-826` → `md0` |
| `/cpus/cpu@N` | `reg` (u32 = MPIDR Aff1:Aff0), `state="running"`, `timebase-frequency` (u32/u64), `cluster-type` ('P'/'E'/none), `die-id`, `l2-cache-size`, `interrupts` (3 entries, see §5.2), `AAPL,phandle` | MADT GICC (MPIDR, GICR base), `CNTFRQ_EL0`, PPTT for clusters | `pe_identify_machine.c:57-145` (timebase default 24 MHz if absent — kernel never reads `CNTFRQ`), `machine_routines.c:1147-1220`, `AppleARMSMP.cpp:116-154` |
| `/arm-io` | `device_type="soc"`, `ranges=[child=0, parent=0, size]`, `#address-cells` etc. | constant; `ranges[1]` is the SoC MMIO base every `reg` is relative to | `pe_identify_machine.c:163-183` |
| `/arm-io/gic` | `reg=[gicd_base, gicd_size, gicr_base, gicr_size]` (u64×4, relative to `ranges[1]`) | MADT GICD + GICR ranges | `pe_fiq.c:78-118` (panics if missing) |
| `/arm-io/uart` | `compatible="arm,pl011"`, `reg=[base,size]`, `AAPL,phandle`, no `interrupts` (polled) | SPCR (type 0x03/0x0E) or DBG2 | `pe_serial.c:711-732, 926` |
| `/defaults` | `serial-device` (phandle → uart) | — | `pe_serial.c:837-864` |
| `/arm-io/interrupt-controller` | `interrupt-controller="master"`, `reg` (GICD) | MADT | `pe_identify_machine.c:205-210` — legacy lookup; must exist or `ml_init_timebase` is skipped (`:245-260`) |
| `/arm-io/timer` | `device_type="timer"`, `reg` (any small MMIO stub, e.g. GICR frame) | GTDT | `pe_identify_machine.c:215-224`, same reason |
| `/arm-io/pcie@N` | `compatible="pci-host-ecam-generic"`, `reg` (ECAM), `bus-range`, `msi-parent` | MCFG, IORT | Tier 2 kext (M5) |
| `/product`, `/pram`, `/socd-trace-ram` | present-but-empty / absent | — | optional lookups (`pe_init.c:137, 521, 866`) |

Boards whose SPCR names a 16550 (many SBCs, Raspberry Pi with EDK2) need a new `pexpert` serial driver alongside PL011; it is a ~120-line addition to `pe_serial.c` following the `pl011_uart_*` pattern and registered in the `compatible` table at `:804-811`.

**Tier 2 (kernel, ACPICA).** A kext `NeoDarwinACPIPlatform` embedding ACPICA (Intel/BSD dual licence, compatible with APSL redistribution) with an XNU OS-services layer (`AcpiOs*` → `IOMalloc`, `ml_io_map`, `IOLock`, `IOTimerEventSource`, interrupt via the GIC controller). It:
1. maps the tables from `/chosen/acpi-tables`, runs `AcpiInitializeSubsystem/LoadTables/EnableSubsystem` (no SCI needed until power management);
2. walks the namespace and publishes an `IOACPIPlatformDevice` nub per device with `_STA` present (the class and its public API `evaluateObject/ acquireGlobalLock/ getACPITableData` mirror the SDK header `IOACPIPlatformDevice.h`, so third-party ACPI-aware kexts written for Intel Macs port with source compatibility);
3. translates `_CRS` into `IODeviceMemory` ranges and `interrupts` specifiers on the GIC controller; `_PRT` into PCI interrupt routing; `_DSD` into properties;
4. provides ECAM (`MCFG`) and `_CBA` to an open **IOPCIFamily** build (apple-oss-distributions/IOPCIFamily), and IORT/ITS to MSI allocation on the GIC kext.
Nothing in the kernel proper depends on Tier 2; the terminal OS milestone (M4) boots without it.

### 2.3 Kernel-side platform layer: the `SBSA` board config

Created by forking `VMAPPLE`:

| Item | Change | Grounding |
|---|---|---|
| `pexpert/pexpert/arm64/SBSA.h` | copy of `VMAPPLE.h` minus `CPU_HAS_APPLE_PAC`, `HAS_PARAVIRTUALIZED_PAC/CTRR`, `HAS_ARM_FEAT_SME*`, `APPLEVIRTUALPLATFORM`; keep `NO_MONITOR`, `HAS_GIC_V3`, `PL011_UART`, `__ARM_16K_PG__` (or 4K variant), GIC register defines; add `GENERIC_ARM64_PLATFORM 1`, `NO_XNU_PLATFORM_ERROR_HANDLER`, `USE_APPLEARMSMP` | `VMAPPLE.h`, `board_config.h:281-295` |
| `board_config.h` | new `#ifdef ARM64_BOARD_CONFIG_SBSA` block, `MAX_CPUS 64`, `MAX_CPU_CLUSTERS 8`, `MAX_L2_CLINE 7` | `board_config.h:281-295` |
| `makedefs/MakeInc.def` | `SUPPORTED_ARM64_MACHINE_CONFIGS += SBSA`; `MACHINE_FLAGS_ARM64_SBSA = -DARM64_BOARD_CONFIG_SBSA -march=armv8.4-a`; build with `ARCH_STRING_FOR_CURRENT_MACHINE_CONFIG=arm64 BTI_BUILD=0` (no `arm64e`, no BTI on pre-8.5 cores); `EXTRA_TARGET_CONFIGS_RELEASE="SBSA"` sidesteps the missing EmbeddedDeviceMap (`MakeInc.def:298-320`) | `MakeInc.def:32, 55-66, 87, 293-326` |
| `osfmk/arm64/proc_reg.h` | add `#elif defined(SBSA)` cache-line branch | `proc_reg.h:238-247` (`#error processor not supported`) |
| `apple_arm64_common.h` → `generic_arm64_common.h` | drop `AIC.h`, `apple_arm64_regs.h`, `apple_arm64_cpu.h`, `apple_uart_regs.h`; keep `ARM_ARCH_TIMER`, `__ARM_COHERENT_CACHE__`; **do not** define `APPLE_ARM64_ARCH_FAMILY` | `apple_arm64_common.h` |
| `APPLEVIRTUALPLATFORM` sites (≈30) | audit each: GIC ack in `sleh_fiq` (`sleh.c:2624, 2670`) and `reset_vector_vaddr` (`arm_init.c:192, 359`; `cpu.c:97-101, 351-355, 1086-1132`; `AppleARMSMP.cpp:279-281`) become `GENERIC_ARM64_PLATFORM`; hypercall probes in `arm64_hypercall.c` and `machine_routines_apple.c:197-264` compile to the `#else` stubs (an `hvc` at EL1 with no hypervisor is UNDEFINED → panic) | grep list in this drop |
| Implementation-defined sysregs | `HID*_EL1`, `S3_x_C15_*` accesses in `machine_routines.c`, `sleh.c`, `caches_asm.s`, `machine_routines_asm.s`, `machine_routines_apple.c`, `pmap.c:11039-11071` must be under `APPLE_ARM64_ARCH_FAMILY`; any that are not get guarded | grep in this drop |
| Timer | virtual timer (`CNTV_*`, `machine_routines.c:2383-2551`) + PPI 27 — matches SBSA; loader must leave `CNTVOFF_EL2 = 0` | `pe_fiq.c:147-150` |
| IPIs | no `HAS_IPI` → `PE_cpu_signal` → `gAIC->sendIPI` when the CPU node exposes 3 interrupt specifiers (`aic_ipis = true` path) | `AppleARMSMP.cpp:131-144, 332-360`; `cpu_common.c:503-521` |
| Page size | 16 KiB kernel granule requires `ID_AA64MMFR0_EL1.TGran16` (Cortex-A55/A7x/A720: yes; A53/A72: no). Ship 16K primary; keep a 4K build variant alive (`WKdmCompress_4k.s` etc. exist) | `proc_reg.h:915-1069` |

**Platform expert compiled into the kernel.** `AppleARMSMP.cpp` demonstrates that IOKit platform code may live in `iokit/Kernel/arm/` inside the kernel image. NeoDarwin follows it for the boot-critical set so M0–M4 need no kernelcache linker and no closed kexts:

| Class | Base | Responsibilities | Contract |
|---|---|---|---|
| `NeoDarwinPlatformExpert` | `IODTPlatformExpert` | match root `compatible`; `processTopLevel` creates nubs from DT; `registerInterruptController`; publish `IOPMGR`; halt/restart via PSCI `SYSTEM_OFF/RESET` | `IOPlatformExpert.h:340`, `IOPlatformExpert.cpp:1615-1750` |
| `GICv3InterruptController` | `IOInterruptController` | `initVector/enableVector/disableVectorHard/causeVector`, `handleInterrupt` (ICC_IAR1/EOIR1 loop, SPI→vector), `sendIPI/cancelDeferredIPI` (ICC_SGI1R_EL1), `setCPUInterruptProperties`; per-CPU GICR init for PPIs; registers as the `PassthruInterruptController` child | `IOInterruptController.h:80-108`, `PassthruInterruptController.cpp:43-100` |
| `PSCIPowerManager` | `IOPMGR` | `enableCPUCore(cpu, entry_pa)` → `SMC PSCI_CPU_ON(mpidr, entry_pa, 0)`; `disableCPUCore` → `CPU_OFF`; `initCPUIdle/enterCPUIdle/exitCPUIdle/updateCPUIdle` → `WFI` (later `CPU_SUSPEND`); cluster ops no-op | `IOPMGR.h:57-134` |
| `NeoDarwinRamDisk` | (uses in-tree `memdev`) | exposes `/chosen/memory-map/RAMDisk` as `md0` (already automatic in `IOKitBSDInit.cpp:766`) | — |

### 2.4 BSD / userland glue

- **Root filesystem ladder.** XNU ships no disk filesystem (`bsd/vfs/vfs_conf.c` lists devfs/mockfs only; APFS is closed). Ladder: (M3) `MOCKFS` (`config/MASTER:114`): the ramdisk *is* a static executable and `mockfs_mountroot` runs it as PID 1 — proves syscalls with zero filesystem code; (M4) **HFS+** kext from apple-oss-distributions `hfs` on an HFS+ ramdisk image, mounted via `rd=md0`; (M5) real volume through IOStorageFamily (open) + virtio-blk / NVMe; root on OpenZFS with boot environments follows in roadmap Phase 3 (`docs/architecture/filesystems.md`).
- **init.** The kernel hardcodes `/sbin/launchd` (`kern_exec.c:7339-7406`). Apple's current launchd is closed; use the last APSL launchd (launchd-842) or Darling's port. M3/M4a may use a static init shim at that path.
- **Toolchain.** Cross-compile `arm64-apple-darwin` on a macOS host: dyld, Libc/libsystem, libplatform, libpthread, libdispatch, xnu headers, following PureDarwin/Darling recipes. Binaries are ad-hoc signed by `ld64` by default; `CONFIG_ENFORCE_SIGNED_CODE` is not set in `config/MASTER.arm64.MacOSX`, AMFI (closed) is absent, and `cs_enforcement_disable=1` (`kern_cs.c:167`) is the belt-and-braces boot-arg.
- **Console.** `serial=3` makes the kernel console the PL011 (polled I/O via `PE_stub_poll_input`, `pe_init.c:599`); `/dev/console` and a getty need no IOSerialFamily.
- **NVRAM.** No UEFI runtime services after handoff → `IODTNVRAM` backed by an in-memory dictionary, persisted later to a file on the ESP by a userland daemon.

---

## 3. Agent swarm division of labour

Each agent owns a directory, a test, and a written contract. Dependencies flow downward; parallel columns start on day 1.

| # | Agent | Inputs | Outputs | Depends on | Definition of done |
|---|---|---|---|---|---|
| A0 | **Build & Toolchain** | xnu tree, Xcode toolchain, this doc | `SBSA` machine config; `make SDKROOT=macosx TARGET_CONFIGS="RELEASE ARM64 SBSA" ARCH_STRING_FOR_CURRENT_MACHINE_CONFIG=arm64 BTI_BUILD=0` produces `kernel.release.sbsa`; `kcgen` (open MH_FILESET linker for M5); QEMU harness `tools/run-qemu.sh` (`-M virt,gic-version=3 -cpu cortex-a76 -bios QEMU_EFI.fd`) with serial capture and pass/fail grep | — | kernel links; harness boots `BOOTAA64.EFI` and asserts on serial output |
| A1 | **UEFI Loader (neoboot)** | §2.1 rules, DT-ABI table, ACPI spec 6.5 (MADT/GTDT/SPCR/MCFG/IORT), Mach-O fileset + chained-fixups format, Embedded Swift UEFI target from A0 | `BOOTAA64.EFI` (Embedded Swift, C shim); `dt-abi.md` (generated from code); `dtdump` host tool that prints the synthesised DT; unit tests for fixup walker against a real kernel | A0 (kernel image, Embedded Swift target) | kernel reaches `arm_init` and prints `iBoot version:` line on QEMU virt |
| A2 | **Kernel Platform Bridge** | §2.3 table, grep lists | `SBSA.h`, `generic_arm64_common.h`, `proc_reg.h` branch, `APPLEVIRTUALPLATFORM` audit, 16550 serial driver, Group 1 timer option (§5.2), `pe_fiq.c` reads PPI number from `/arm-io/gic` `timer-ppi` | A0 | single-CPU boot to `kernel_bootstrap` complete, timer interrupts counting |
| A3 | **IOKit Platform (in-kernel)** | §2.3 class table, IOKit headers | `iokit/Kernel/arm/NeoDarwin*.cpp`: platform expert, GICv3 controller, PSCI IOPMGR, PSCI halt/restart | A2 | all CPUs online (`cpus=N` boot-arg respected), IPIs measured, SPI test device (virtio console) interrupts |
| A4 | **ACPI Runtime (ACPICA kext)** | ACPICA source, `IOACPIPlatformDevice.h` SDK header, IORT/MCFG | `NeoDarwinACPIPlatform.kext`, `IOPCIFamily` open build wired to ECAM, MSI via GIC ITS | A3, A0 (`kcgen`) | `ioreg` shows PCI bus with virtio-pci / NVMe nubs on QEMU and on CD8180 |
| A5 | **Storage & Root FS** | HFS+ source, IOStorageFamily, virtio spec, NVMe spec | `hfs.kext` build, HFS+ ramdisk builder, `virtio-blk` and open `NVMe` IOKit drivers | A3 (ramdisk), A4 (PCIe) | M4: root on HFS+ ramdisk; M5: root on NVMe volume by `boot-uuid` |
| A6 | **BSD / Userland Glue** | PureDarwin/Darling recipes, launchd-842, xnu headers | cross toolchain SDK; `ramdisk.img` builder; `/sbin/launchd`, `/etc/rc`, getty on `/dev/console`, `sh`, coreutils; ad-hoc signing policy | A0 (headers) — independent of kernel until M3 | `login:` prompt on serial, shell commands run, `uname -a` correct |
| A7 | **Verification & CI** | all of the above | QEMU virt + sbsa-ref matrix (16K/4K, 1/4/8 CPUs, with/without secure firmware via TF-A `QEMU_EFI` built with `ARM_TRUSTZONE`), hardware-in-the-loop runner for OrangePi 6 Plus (serial + power relay), boot-time budget, regression corpus of DT dumps | A0 | every merge boots both QEMU machines; nightly hardware run |
| A8 | **Documentation & DT-ABI Keeper** | code reads | keeps `dt-abi.md` and this blueprint in sync with the tree; reviews every PR that touches `pexpert/`, `boot.h`, or the loader for ABI drift | — | no undocumented property read by the kernel |

Agent messaging discipline: the DT-ABI table is the interface between A1 and A2/A3; it is a versioned document and both sides test against `dtdump` output, so the loader and the kernel can be developed and tested independently (the kernel under QEMU with a hand-written DT blob until neoboot lands).

---

## 4. Roadmap and milestones

| Milestone | Deliverable | Agents | Exit test |
|---|---|---|---|
| **M0 Build** | `kernel.release.sbsa` links; QEMU harness; hand-made DT blob loader stub | A0, A7 | CI green on "kernel image exists" |
| **M1 First light** | neoboot loads kernel; PL011 prints `iBoot version:` and `bsd_init` banner attempts | A1, A2 | serial contains `arm_init` traces |
| **M2 SMP + time** | all CPUs online via PSCI; timer FIQ/IRQ ticking; IPIs; `panic()` backtraces symbolicate | A2, A3 | `sysctl hw.ncpu` equals MADT count |
| **M3 PID 1** | mockfs runs a static `hello` init; syscalls, VM, Mach IPC exercised by a test binary | A3, A6 | test binary prints from userland |
| **M4 Terminal OS** | HFS+ ramdisk root, launchd, getty, shell, coreutils; `boot-uuid` plumbing | A5, A6 | interactive shell over serial; reboot/halt via PSCI |
| **M5 ACPI runtime + storage** | ACPICA kext, IOPCIFamily, virtio-blk/NVMe, root from disk, `kcgen` kernelcache with real kexts | A4, A5, A0 | boot from NVMe on QEMU sbsa-ref |
| **M6 Hardware** | OrangePi 6 Plus (CIX CD8180, GIC-700, PL011, UEFI+ACPI) boots to shell | A7 + all | HIL nightly green |
| **M7 Graphics groundwork** | GOP framebuffer via `Boot_Video` → `IOFramebuffer` stub; input over USB (open IOUSBHostFamily build); Wayland compositor spike in userland | new agents | pixels on HDMI from userland |

---

## 5. Critical path and risk analysis

### 5.1 Risk 1 — Virtual-memory handoff (single DRAM window, fixups, cache state)
*Failure mode:* kernel faults in `start.s` or `arm_vm_init` with no serial output; or boots and later corrupts firmware-reserved memory.
*Root causes grounded in code:* `boot_args` has one `{physBase, memSize}` (`boot.h:69-71`); physmap covers exactly that range (`arm_vm_init.c:1835-1869`); the kernel expects fixups applied and a 2 MiB-aligned base (`start.s:553-575`); everything read before the MMU is on must be clean to PoC.
*Mitigation:*
- Loader chooses the largest hole-free conventional run; refuses to boot below 512 MiB; logs the map. `memSizeActual` carries the full total.
- Deterministic first-light mode: `slide=0`, KASLR off, `-noprogress`, `debug=0x14e`, so an early fault is reproducible; A7 keeps a golden `boot_args`+DT dump.
- Loader-side fixup walker unit-tested on the host against the same kernel image; A0 provides a `kcheck` tool that verifies every rebased pointer lands inside the image.
- Explicit cache clean + `dsb sy; ic iallu; tlbi vmalle1` before MMU-off; QEMU cannot catch this, so A7 runs the sequence on hardware early (M6 pre-work on any UEFI SBC).

### 5.2 Risk 2 — Interrupt-controller binding (GICv3 Group 0 FIQ vs. TrustZone)
*Failure mode:* boots perfectly on QEMU and under Apple's hypervisor, hangs at first timer interrupt on real boards.
*Root cause:* `pe_init_fiq` puts the timer PPI in **Group 0** and enables `ICC_IGRPEN0_EL1` (`pe_fiq.c:144-170`); `sleh_fiq` acks via `ICC_IAR0_EL1` (`sleh.c:2624`). On silicon with TF-A, `GICD_CTLR.DS = 0`, Group 0 is Secure, non-secure EL1 cannot own it and the FIQ is routed to EL3. QEMU `virt` without `secure=on` has `DS = 1`, which hides the bug.
*Mitigation (designed in from day 1, A2):*
- New DT property `/arm-io/gic` `timer-group` (0 or 1). With group 1: `pe_init_fiq` programs `GICR_IGROUPR0` bit 27 = 1 and `ICC_IGRPEN1_EL1`; the timer then arrives as **IRQ**. In `sleh_irq`/`PE_handle_ext_interrupt`, the GIC controller's `handleInterrupt` recognises INTID 27 and calls the same `rtclock_intr` path the FIQ handler uses (`sleh.c:2650-2665`), acking with `ICC_EOIR1_EL1`. Boards set `timer-group=1`; QEMU tests both.
- IPIs are SGIs in Group 1 through `sendIPI` (`ICC_SGI1R_EL1`); CPU nodes carry three `interrupts` specifiers so AppleARMSMP takes the `aic_ipis` path (`AppleARMSMP.cpp:131-144`).
- `PMR = 0xFF`, `BPR`, `EOImode = 0` as VMAPPLE does; A7 adds a TF-A-backed QEMU config (`secure=on` with `ARM_TRUSTZONE` firmware) to CI so the DS=0 case is tested in software before hardware.
- Redistributor discovery already loops `GICR_TYPER` for the CPU's affinity (`pe_fiq.c:44-68`); loader passes the full GICR stride region from MADT GICR structures, not a per-CPU list.

### 5.3a Measured: the published XNU source does not link on its own

Building `xnu-12377.1.9` VMAPPLE RELEASE from public sources (`//kernel:vmapple_release_gaps`, 2026-09-27) compiles every file but leaves **395 undefined symbols** at link. Apple's public build fills them from a closed per-SoC archive in the login-gated Kernel Debug Kit (`libVMAPPLE.os.RELEASE.a`). NeoDarwin does not use the KDK; the gaps close from source instead:

| Gap | Symbols | Plan |
|---|---|---|
| ARM64 machine code and pmap present in the tree but excluded by the public config options `nos_arm_asm`/`nos_arm_pmap` | about 237 | `kernel/patches/0001` enables them; the SBSA board config supplies the guards the newly built files need (Apple `CPU_OVRD` registers, per-SoC tunables, Apple IOMMU headers) |
| Required only by export lists (Tightbeam 111, `IOUnifiedAddressTranslator` 19, others) | 157 | NeoDarwin drops the exclaves and Apple-IOMMU export lists |
| Apple SoC IOMMUs, kernel-integrity regions, NVMe PPL | 26 | not built for SBSA |
| libTrustCache runtime | 2 | NeoDarwin stub |

The categories overlap (some pmap symbols are also exported). The authoritative list is `kernel/link_gaps/vmapple_release.txt`, and a ratchet test keeps it from growing.

**Status, 2026-09-27:** closed. `//kernel:sbsa_release` links `kernel.release.sbsa` (Mach-O `arm64`, no undefined symbols) from the public archive plus `kernel/patches/0001`–`0005`; `kernel/README.md` records how each gap closed. The remaining pre-boot hazard is measured separately: implementation-defined system-register accesses (`kernel/sysreg_audit/sbsa_release.txt`), owned by P1-02.

### 5.3 Risk 3 — Closed kexts and kernelcache tooling
*Failure mode:* the project stalls on binaries it cannot build: `AppleARMPlatform`, `AppleInterruptController`, `AppleVirtualPlatform`, `AppleMobileFileIntegrity`, `apfs`, `IONVMeFamily`, and the `kmutil`/EmbeddedDeviceMap build tooling.
*Mitigation:*
- Compile the boot-critical platform layer into the kernel image (§2.3), so M0–M4 need no kernelcache at all; `AppleARMSMP.cpp` is the in-tree precedent.
- Explicit *no-closed-code* list with open replacements: platform expert (new), GIC (new), PSCI PMGR (new), AMFI (none needed; `cs_enforcement_disable`), apfs → HFS+ (open), NVMe (new, spec-driven), IOPCIFamily/IOStorageFamily/IOUSBHostFamily (open source drops).
- `kcgen` (A0) implements `MH_FILESET` + `LC_FILESET_ENTRY` + fixup-chain emission using the format the kernel already parses (`OSKext.cpp:13701`), so M5 does not depend on `kmutil` behaviour changing.
- EmbeddedDeviceMap bypass is a supported path in the makefiles (`EXTRA_TARGET_CONFIGS_*`, `MakeInc.def:305-320`), not a hack.

### 5.4 Secondary risks (tracked, not on the critical path)
| Risk | Signal | Mitigation |
|---|---|---|
| 16 KiB granule unsupported on target core | `ID_AA64MMFR0_EL1.TGran16 == 0` | loader refuses 16K kernel and picks the 4K build from the ESP |
| BTI / PAC assumptions | `XNU_BUILT_WITH_BTI` default on (`MakeInc.def:57-66`); arm64e ABI | build `arm64`, `BTI_BUILD=0` for v8.4 targets; enable per-board when v8.5 |
| SME/SVE feature flags in `VMAPPLE.h` | illegal-instruction on cores without SME | drop `HAS_ARM_FEAT_SME*`; gate on `ID_AA64PFR1_EL1` |
| Non-coherent DMA on cheaper SoCs | data corruption in virtio/NVMe | `__ARM_COHERENT_IO__` off for those boards; IODMACommand cache ops in the drivers (CD8180 PCIe is coherent through the CCI) |
| PSCI/SMC from EL1 trapped by a hypervisor | `SMC` UNDEF | loader verifies `CurrentEL` and `HCR_EL2.TSC = 0`; fallback HVC conduit selectable via `/chosen/psci-conduit` |
| `hvc` probes left in | UNDEF at `arm64_hypercall.c:62` | A2 audit; CI runs on QEMU with `-cpu` lacking EL2 |
| Legacy `interrupt-controller`/`timer` node lookup | `ml_init_timebase` skipped → no `rtclock_timebase_func` | loader emits both stub nodes; A2 removes the requirement later |
| No UEFI runtime → no NVRAM | `nvram` writes lost | in-memory `IODTNVRAM`, ESP file persistence at M4 |
| Licensing | APSL 2.0 kernel, ACPICA dual BSD/GPL, EDK2 BSD+Patent | keep loader and kexts BSD-2; no GPL in kernel image |

---
