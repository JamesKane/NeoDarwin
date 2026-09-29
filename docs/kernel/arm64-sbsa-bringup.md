# Kernel bring-up: XNU on ARM64 SBSA boards (UEFI + ACPI)

> **Scope.** This document is the *kernel bring-up* design for the ARM64 lane of NeoDarwin. The project-wide charter, build system, packaging, ports, drivers, console, filesystems, FreeBSD-parity and multi-arch designs live under `docs/architecture/`; the phased plan lives in `roadmap/`. Milestones M0–M7 below map onto roadmap Phase 1 (`P1-*` epics).

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
  5. Copy the kernelcache as one flat image (no fixups: the kernel applies them itself in arm_init; KASLR slide chosen here by the placement)
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
  • PE_init_platform(TRUE) → pe_arm_init_interrupts → ml_init_timebase; pe_init_fiq (GICR wake, timer PPI on the DT's group: 0 FIQ, 1 IRQ)
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

**Format:** A UEFI application (`BOOTAA64.EFI`) written in **Embedded Swift** (language policy T3), built by `rules/efi.bzl` (see `toolchains/README.md`). AArch64 UEFI uses the standard AAPCS64 calling convention, so Swift calls firmware services directly; the only C is the UEFI structure header and the compiler's memory routines. The toolchain was proven on QEMU in P0-09, so no C fallback loader is kept. It needs no runtime services after `ExitBootServices` and never returns.

**What it loads:** an `MH_FILESET` kernelcache. Rationale: on arm64 `OSKext` disables kext requests to userland (`libkern/c++/OSKext.cpp:354-356`) and safe-boot handling assumes the booter loaded a kext collection (`OSKext.cpp:1015-1023`). For milestones M0–M4 the fileset contains only the kernel because the platform expert is compiled in (§2.3); real kexts appear at M5.

**Loading rules (mirrors iBoot):**
| Step | Rule | Why (source) |
|---|---|---|
| Placement | Copy the kernelcache file as one image: `kcgen` makes every file offset equal its VM offset and materialises bss, so no per-segment work is needed. The collection header's physical address must equal its virtual address `VM_KERNEL_LINK_ADDRESS + slide` modulo 32 MiB; with slide 0 on QEMU `virt` that puts it at `0x41004000` | `start.s:545-574`: for a fileset member the base is the collection header. `create_bootstrap_mapping` then maps virtual to physical block for block with 16 KiB-granule L2 blocks, which are 32 MiB |
| Fixups | None in the loader; it must not touch pointers. The kernel rebases the whole collection from its `LC_DYLD_CHAINED_FIXUPS` before anything else runs. `kcheck` verifies the chains at build time (§2.1.1) | `arm_slide_rebase_and_sign_image()` → `kernel_collection_slide()` (`osfmk/arm/arm_init.c:230-285`, compiled for `config_pmap_ppl`, which SBSA uses like VMAPPLE; the SPTM kernel has its own copy in `arm_init_sptm.c`) |
| KASLR | `slide` = random multiple of 16 KiB inside the chosen window; it is implied by where the collection is mapped, never written into the image | the kernel computes `slide = &_mh_execute_header − kernel __TEXT.vmaddr` (`arm_init.c:246`) |
| boot_args | `Revision=2 Version=2`; `virtBase = VM_KERNEL_LINK_ADDRESS + slide − (kernelcache_phys − physBase)`; pointers (`deviceTreeP`) are given in **virtual** terms (kernel uses them after MMU on via `PE_state.deviceTreeHead`, `pe_init.c:423`) | `boot.h:61-64`, `pe_init.c:412-444` |
| Video | If GOP has a linear framebuffer: `v_baseAddr, v_rowBytes, v_width, v_height, v_depth=32`; always `v_display=0`, a text console (§2.1.7) | `pe_init.c:425-436, 536-548` |
| CommandLine | `boot.cfg` contents, e.g. `serial=3 debug=0x14e rd=md0 -v cs_enforcement_disable=1` | `PE_boot_args()` in `pe_bootargs.c` |
| EL | Enter kernel at **EL1**. If firmware hands off at EL2: `HCR_EL2 = RW`, `CNTHCTL_EL2 = EL1PCTEN|EL1PCEN`, `CNTVOFF_EL2 = 0`, `CPTR_EL2` no FP trap, `SCTLR_EL1 = RES1`, `SPSR_EL2 = EL1h + DAIF`, `ERET` | `start.s` never inspects `CurrentEL` and programs only `*_EL1` registers |
| Caches | Clean to PoC every byte the kernel will read with MMU off (image, DT, boot_args, ramdisk), invalidate I-cache, then disable MMU | `start.s` reads boot_args before enabling MMU |

**Memory-map policy:** XNU cannot express holes. The loader selects the largest run of `EfiConventionalMemory` (plus `EfiBootServicesCode/Data` and `EfiLoaderData`, which are reclaimable after `ExitBootServices`) with no non-conventional descriptor inside it. `EfiACPIReclaim`, `EfiACPIMemoryNVS`, `EfiRuntimeServices*` and `EfiReserved` ranges must be *outside* the window; if firmware places one mid-DRAM the loader picks the larger side and logs the loss. ACPI table pages are copied into the window (into the DT-adjacent area, recorded in `/chosen/memory-map` as `ACPITables`) so the kernel-side ACPICA kext can reach them through the physmap.

**Secondary CPU entry:** PSCI `CPU_ON` starts a core with MMU off at the caller's EL with `x0 = context`. XNU's reset vector (`LowResetVectorBase`, `start.s:106-203`) ignores `x0` and identifies itself by `MPIDR_EL1`, matching against `CpuDataEntries` populated from `/cpus`. So the loader has no per-CPU work; it only guarantees that the kernel is at EL1 and that `SMC` from EL1 reaches EL3 (`HCR_EL2.TSC = 0`), and it tells the kernel how to reach PSCI: `/chosen` `psci-conduit` (`"smc"` or `"hvc"`), from the FADT's `ARM_BOOT_ARCH`, or SMC when the FADT reports none but the CPU implements EL3 (P1-06, `dt-abi.md`). The entry point is `LowResetVectorBase`'s physical address, which patch 0017 passes to `NeoDarwinPSCI` as `APPLEVIRTUALPLATFORM` passes it to Apple's PMGR.

### 2.1.1 Kernel collection layout (`kcgen`)

The boot collection neoboot loads is built by `//tools/kcgen` (`//kernel:sbsa_kc` → `kernelcache.release.sbsa`) and checked by `//tools/kcheck`. It is flat, so the file is the memory image. Offsets below are from the collection header, which sits at `VM_KERNEL_LINK_ADDRESS` (`0xfffffe0007004000`).

| Offset | Top-level segment | Contents | Why (source) |
|---|---|---|---|
| 0 | `__TEXT` | collection header and load commands: 8 segments, `LC_DYLD_CHAINED_FIXUPS`, `LC_UUID`, one `LC_FILESET_ENTRY` (`com.apple.kernel`) | `arm_init.c:253`: the collection header is at `VM_KERNEL_LINK_ADDRESS + slide` |
| 0x4000 | `__PRELINK_INFO` | XML plist: empty `_PrelinkInfoDictionary`, `_PrelinkKCID` = the collection UUID | `libsa/bootstrap.cpp:204-205`: dereferenced unconditionally in a fileset |
| 0x8000 | `__PRELINK_TEXT` | empty (kexts, M5) | looked up by name (`arm_vm_init.c:2049`) |
| 0x8000 | `__KERNEL` | kernel `__TEXT`, `__DATA_CONST`, `__TEXT_EXEC`, `__KLD`: the whole kernel moved up by 0x8000 | a NeoDarwin name; the kernel never looks this range up |
| | `__DATA_CONST` | exactly kernel `__LASTDATA_CONST` | `arm_vm_init.c:2032-2038`: must contain it; the remainder becomes kext `PLK_DATA_CONST` |
| | `__TEXT_EXEC` | empty, at kernel `__LAST` | `arm_vm_init.c:2020-2029`: must contain `__LAST`; anything after it is remapped as kext text, so it must end there |
| | `__DATA` | kernel `__KLDDATA`, `__DATA` (bss written out as zeros), `__BOOTDATA` | `arm_vm_init.c:2041-2045`: must contain the kernel's empty `__PRELINK_DATA` |
| | `__LINKEDIT` | kernel `__LINKINFO` and `__LINKEDIT`, then the chained fixups | `kernel_collection_slide()` locates the fixups through it |

The kernel is linked as position-independent (`MH_PIE`) with 65,407 local `ARM64_RELOC_UNSIGNED` relocations. `kcgen` moves the whole kernel by one constant, so PC-relative code is unchanged. Each relocation becomes a `DYLD_CHAINED_PTR_64_KERNEL_CACHE` fixup, one chain per 16 KiB page, with no pointer authentication (plain `arm64`). `kcgen` also moves the symbol values, section addresses and entry point by the same constant, and sets `MH_DYLIB_IN_CACHE` on the kernel's header. That flag is how `start.s` and `arm_init` recognise a fileset member.

### 2.1.2 First boot on QEMU (P1-03)

`//kernel:sbsa_boot_test` boots neoboot, `kernelcache.release.sbsa` and the PID 1 test program as the ramdisk on QEMU `virt` (`gic-version=3`, 2 GiB, one CPU). In under six seconds the kernel goes through the loader handoff, its own fixups, MMU and VM bootstrap, zalloc, the scheduler, IPC and logging, and prints `iBoot version: neoboot-0.1` from `PE_init_iokit`. With ndcrypto (P1-13), ndamfi (P1-14), the platform expert (P1-06) and libpthread's `kern/` (P1-16) it continues through crypto, PRNG, trust caches, IOKit and BSD initialisation, including networking. It then roots on mockfs over the ramdisk and runs the first userland program as PID 1 (P1-07, §2.1.3). The whole boot takes about six seconds. Since P1-04 the device tree is synthesised from the ACPI tables (MADT, GTDT, SPCR, FADT) to the DT-ABI v1 contract, `docs/kernel/dt-abi.md`; nothing in it is specific to `virt`.

What the first boot established:

| Finding | Consequence |
|---|---|
| The kernel executes `TLBI RVALE1IS` (FEAT_TLBIRANGE, part of Armv8.4). QEMU's `cortex-a76` (v8.2) and `neoverse-v1` models don't advertise it, so the instruction is undefined there | tested on `neoverse-n2` (Armv9.0) until P1-17. The Q8B's Cortex-X1C/A78C are Armv8.2 and lack it too. Since P1-17 the kernel's baseline is Armv8.2 without range TLBI (patch 0019, §2.1.8), and the `sbsa_a76_*` tests boot it on `cortex-a76` |
| kalloc_type's zone policy gave the 48-byte class 15 + 17 zones; with the shared zone that overran a 32-entry stack array, which upstream only asserts on | patch 0007 enforces the limit |
| With no framebuffer, `PE_init_iokit`'s progress-bar centring loop never ends | `-noprogress` in neoboot's default command line; the framebuffer console (§2.1.7) is text, so it stays |
| Data abort in `kmem_crypto_init`: nothing had called `register_crypto_functions()`, which Apple's corecrypto kext does. Apple's full corecrypto source is evaluation-only, so it can't be used | P1-13, done: ndcrypto, compiled into the kernel from xnu's own corecrypto subset and FreeBSD's kernel crypto (`crypto-provider.md`) |
| `image4 interface not available` in `bsd/kern/kern_trustcache.c`. AppleImage4 and AMFI, both closed kexts, normally register the Image4 and AMFI interfaces | P1-14, done: ndamfi (`amfi-provider.md`), with Apple's published trust-cache format and lookup |
| `Unable to find driver for this platform: "NeoDarwin,sbsa"` (`IOPlatformExpert.cpp`) | P1-06: `NeoDarwinPlatformExpert`, `NeoDarwinGICv3`, `NeoDarwinPSCI` (patch 0009), matched by a built-in personality; secondary CPUs in §2.1.6 |
| A 16 MB `kmem_alloc` failed while mapping the GIC redistributors: the device tree gave QEMU's whole 123-CPU region | the tree describes one frame per MADT GICC (P1-04), and the GIC driver caps its mapping at `MAX_CPUS` frames |
| `pthread kernel extension not loaded` (`pthread_shims.c`), after BSD init has brought up the MAC framework, buffer cache and mbufs: pthread.kext registers the pthread function table | P1-16, done: Apple's libpthread-539 `kern/` (APSL) built into libkern by patch 0010 and started at `EARLY_BOOT` by `kernel/neodarwin/pthread`. It compiles as the kext does, against the exported headers without `XNU_KERNEL_PRIVATE`, with a compat `TargetConditionals.h`; its `current_uthread` and `pthread_kern` are renamed to avoid clashing with the kernel's |
| `Waiting on <dict …IOProviderClass… IOMedia … Apple_HFS…>`, after `dlil` and `lo0`: no root device | P1-07, done: mockfs roots on the ramdisk as md0 (§2.1.3); P1-08 brings an HFS+ ramdisk, P1-10 virtio-blk |
| Data abort in `OSMetaClass::applyToInstances` as soon as IOFindBSDRoot chose md0: `publishHiddenMedia()` asserts that the IOMedia class exists, and IOStorageFamily isn't loaded | patch 0012 skips the walk when there is no IOMedia class |
| `mockfs_fsnode_vnode failed to create fictitious pages for a memory-backed device` at the first exec | patch 0011: mockfs took the memory device's address from a 32-bit count of 4 KiB pages and shifted it by the 16 KiB `PAGE_SHIFT`; it now asks `mdevgetrange()`. The same patch fixes its node allocation, which named the pointer typedef |
| `unexpected SIGKILL of init … namespace 9 code 0x1`: `OS_REASON_EXEC`/`EXEC_EXIT_REASON_BAD_MACHO`, because RELEASE kernels refuse static arm64 executables | patch 0013: on `GENERIC_ARM64_PLATFORM`, process 1 may be static |
| Every boot sat idle for 30 s in BSD initialisation. `IOKitInitializeTime()` waits up to 30 s for an `IORTC` resource, which Apple's closed RTC kext publishes, and then reads the calendar through the platform expert, which had no clock | neoboot reads UEFI `GetTime()` and passes it with the counter value in `/chosen` (`neodarwin,utc-seconds`, `neodarwin,utc-counter`). `NeoDarwinPlatformExpert` serves the time of day as that value plus elapsed counter ticks, and publishes `IORTC`. The boot went from 36 s to 6 s, and PID 1 checks the calendar. A time set later is kept only until reboot |
| QEMU's GICv3 trace showed no `ICC_IAR0_EL1` reads: `sleh_fiq` acknowledges and completes Group 0 only `#if APPLEVIRTUALPLATFORM`, so the SBSA kernel handled every timer FIQ without touching the GIC. It worked only because the timer is level-sensitive | patch 0014 gives `GENERIC_ARM64_PLATFORM` the same `IAR0`/`EOIR0` handling, as §2.3 planned |

### 2.1.3 First userland: mockfs and PID 1 (P1-07)

The first root needs no filesystem code. neoboot loads `\NeoDarwin\ramdisk` (optional) between the device tree and `boot_args`, below `topOfKernelData` because the kernel maps it with `ml_static_ptovirt()`. It publishes the ramdisk as `/chosen/memory-map/RAMDisk`, zero-padded to 16 KiB, and appends `rd=md0` when the command line names no root. `IOFindBSDRoot` turns it into md0 (`IOKitBSDInit.cpp`). `vfs_mountroot` tries mockfs last (patch 0011 builds it for SBSA). mockfs presents the device as one file that answers to `/sbin/launchd` and a directory that devfs mounts over at `/dev`.

The ramdisk is `//tests/qemu/pid1`. It's a static arm64 Mach-O in Embedded Swift with no libSystem and no dyld, built by `rules/static_macho.bzl`. It enters the kernel through the Darwin trap ABI (`svc #0x80`, the call in `x16`) and checks, in order:
- that it is process 1;
- that `/dev/console` opens and takes writes;
- that anonymous memory maps, holds a pattern across four pages, and unmaps;
- that `task_self_trap` and `mach_reply_port` return port names;
- that a message sent to its own receive right comes back through `mach_msg2_trap`, the path libsystem_kernel's `mach_msg()` takes (`MACH64_SEND_MQ_CALL`).

A failed check exits with its number, which the kernel's "initproc exited" panic carries. On success PID 1 blocks in a Mach receive. `sbsa_boot_test` asserts `BSD root: md0` and the program's lines through `pid1: all checks passed`.

Xcode's `ld` links it: `-static`, an `LC_UNIXTHREAD` entry and an ad hoc linker signature, which the arm64 kernel requires of every executable page. The toolchain's `ld64.lld` implements neither `-static` nor `LC_UNIXTHREAD`.

### 2.1.4 HFS+ ramdisk root (P1-08a)

`//kernel:sbsa_boot_test` now roots on HFS+. `//images:pid1_root` (`rules/ramdisk.bzl`) is a raw HFS+ volume with no partition map, since md0 is the whole device. It holds the PID 1 test program as `/sbin/launchd` and an empty `/dev` for devfs. Phase 1 builds it with the host's `hdiutil`, not journaled. neoboot loads it as `\NeoDarwin\ramdisk` exactly as before. `//kernel:sbsa_mockfs_boot_test` keeps the P1-07 boot, with PID 1 itself as the ramdisk.

HFS+ is Apple's `hfs-704.0.3.0.2` (the macOS 26.0 release, APSL), built into the kernel by patch 0015 rather than as `hfs.kext`, because `kcgen` links no kexts until M5 (`filesystems.md` §4). It compiles as the hfs and HFSEncodings kexts do, as libpthread's `kern/` already does:
- the kext target's 38 published sources and HFSEncodings' two C files;
- the kexts' prefix header `core/kext-config.h`, which picks the macOS feature set;
- the exported-header view of the kernel.

`bsd_init()` calls `nd_hfs_start()` (`kernel/neodarwin/hfs`) right after `bsd_autoconf()`. It does what the two kexts' `start()` routines do: it initialises the encoding converters and calls `vfs_fsadd()`. A kext matched through IOKit would start asynchronously and race `vfs_mountroot()`.

What the port needed:

| Finding | Fix |
|---|---|
| Kext code sees none of the kernel's `CONFIG_*` options. With them, `CONFIG_PROTECT` built iOS content protection (AppleKeyStore, unpublished) and `CONFIG_MACF` built MAC checks that read `struct vnode` | patch 0015 undefines both for the HFS objects |
| `hfs_iokit.h` includes `AppleKeyStore/AppleKeyStoreFSServices.h` for types only | a NeoDarwin compat header (`kernel/neodarwin/hfs/compat`) with opaque key types. The key-wrapping helpers return `ENXIO`, and nothing calls them |
| `sys/cprotect.h` includes `crypto/aes.h`, which kexts get from Kernel.framework | `-idirafter $(SRCROOT)/bsd`: found after every exported header |
| xnu names C++ objects `.cpo`, so per-object flags for `hfs_iokit.o` never applied | the flags name `hfs_iokit.cpo` |
| `OSKextGetCurrentLoadTag` is undefined: a kext gets its own from its build | `nd_hfs.c` returns 0, the kernel's load tag |
| `hfs_allocated` is defined by both kexts | HFSEncodings' copy is renamed |
| `ENOEXEC` from `/sbin/launchd`: `vfstable_add` links a run-time filesystem after the static entries, so after mockfs, which then claimed the HFS+ ramdisk as its executable | patch 0011: mockfs declines a device that doesn't start with a Mach-O magic number (Apple's own TODO in `mockfs_mountroot`) |

`hfs_key_roll.c` is listed in the kext target but not published. It serves content protection, which macOS HFS+ leaves off.

Iterating on kernel sources: `ND_XNU_KEEP_WORK=DIR` makes `tools/xnu/kernel.sh` build in DIR and keep it, and writes the make command to `DIR/make.sh`. Run the action's command from `bazel aquery 'mnemonic("XnuKernel", //kernel:sbsa_release)'` outside Bazel with it set, then rerun `make.sh` after each edit: only what changed recompiles. The per-object compile commands are in the objects' `.o.json` and `.cpo.json` files.

Debugging: QEMU's gdbstub (`-s`) with `lldb`, loading `kernel.release.sbsa.unstripped` with `--slide 0x8000` (the kernel's offset inside the collection at slide 0) and hardware breakpoints (`breakpoint set -H`). Panic `caller` and `pc` values minus 0x8000 symbolise with `atos` against the unstripped kernel. The kernel has no line tables, so a hardware breakpoint on `os_reason_create` or on a return site, plus `bt` and a register read, is the quickest way to place an exec failure. Exit-reason namespaces are in `bsd/sys/reason.h`: 9 is `OS_REASON_EXEC`, not codesigning (3).

### 2.1.5 A dynamically linked PID 1 through dyld (P1-08b)

`//kernel:sbsa_dyld_boot_test` boots `//images:hello_root`: the HFS+ root with the userland base built from Apple source, and a dynamically linked hello world as `/sbin/launchd`. The base is dyld, libSystem.B and the 27 libraries it reexports (`docs/base/libsystem.md`). The kernel execs it and maps `/usr/lib/dyld`. dyld loads libSystem and the libraries under it (32 images, with no shared cache) and runs libSystem's initializer. The program then prints from stdio, allocates, runs a pthread, calls `dlopen` and `dlsym`, and runs a function on a libdispatch global queue through the kernel's pthread workqueue. The kernel needed no changes for any of this: the exec, dyld and workqueue paths are xnu's own.

A launch failure is visible: dyld's error goes into the exit reason, which the "initproc failed to start" panic prints ("Symbol not found: ..."). That panic then takes a nested kernel data abort (FAR 0xc) inside the panic path. It doesn't affect a successful boot, but it hides the backtrace.

**Open threads (parked 2026-09-28).** In order of the boot path:
- **P1-08 is done** (`docs/base/session.md`). launchd-842 is PID 1 and starts getty from its plist, and `//kernel:sbsa_session_boot_test` logs in over serial. Next on the userland side: zsh or bash with ncurses, OpenPAM, diskdev_cmds' `mount` and fsck, and replacing more of the libxpc stand-in.
- **The nested panic on a failed PID 1 launch** (§2.1.5): a data abort at FAR 0xc in the panic path after "initproc failed to start".
- **P1-06 is done.** Secondaries start through PSCI, and the exit is met on QEMU: `//kernel:sbsa_smp_boot_test` (`virt`, `-smp 4`, PSCI over HVC) and `//kernel:sbsa_secure_smp_boot_test` (TF-A at EL3, PSCI over SMC, GIC DS=0) log in and read `hw.ncpu` and `hw.activecpu` as 4 and 4 with `sysctl` (system_cmds), and the platform expert logs `NeoDarwinPlatformExpert: 4 of 4 CPUs online` and one IPI round trip per CPU. The design is in §2.1.6.
- **P1-15.** CoreEntitlements, static trust caches from neoboot, and signed trust-cache loads, replacing ndamfi's default deny.
- **P1-04 is done.** The device tree from ACPI, DT-ABI v1 (`dt-abi.md`), `dump-acpi` in neoboot and the `dtdump` host tool.
- **P1-09, checkpoint 1 is done** (`acpi.md`): ACPICA 20260408 compiled into the kernel (patch 0018) over neoboot's copy of the tables, hardware-reduced, and an `IOACPIPlatformDevice` nub per present device, with `_CRS` memory as `IODeviceMemory` and GSIVs as GIC interrupt specifiers. On QEMU `virt` it publishes 44 devices, the PCI host bridge `PNP0A08` among them, asserted by `//kernel:sbsa_boot_test` and `//kernel:sbsa_secure_boot_test`.
- **P1-09, checkpoint 2 is done** (`pci.md`): Apple's IOPCIFamily-726.0.5 built into the kernel (patches 0022 and 0023) on `NeoDarwinPCIHostBridge`, an ECAM host bridge for the ACPI PNP0A08 nubs: segments and ECAM from `_SEG`/`_CBA`/MCFG (above 4 GiB), windows from `_CRS` (I/O as MMIO at the translation offset), `_OSC` before AER, firmware BARs kept (64-bit ones above 4 GiB too, patch 0023), INTx through `_PRT` and link devices, SPIs configured in the GIC (Group 1, trigger, priority, routing), `dma-coherent` from `_CCA`/IORT. `//kernel:sbsa_pci_boot_test` and `//kernel:sbsa_secure_pci_boot_test` enumerate virtio-blk (transitional and, behind a PCIe root port, modern), NVMe and QEMU's `edu`, whose INTA reaches its handler. `sbsa-ref` is still not in the test matrix.
- **P1-09, checkpoint 3 is done** (`gic-its.md`): MSI and MSI-X through the GICv3 ITS. LPI configuration and pending tables in `NeoDarwinGICv3` (every redistributor, attributes read back, non-cacheable with cache maintenance if the GIC doesn't snoop), `NeoDarwinGICv3ITS` (device and collection tables, two-level when large, the command queue, MAPD/MAPC/MAPTI/INV/INVALL/SYNC/DISCARD), the ITS from the MADT and DeviceIDs through the IORT (the Q8B's go through its SMMUv3: 2:01:00.0 is DeviceID 0xa0100), and an `IOPCIMessagedInterruptController` whose vectors are LPIs (patch 0024). edu's MSI and two MSI-X vectors of an NVMe controller reach their handlers in `//kernel:sbsa_pci_boot_test`, `sbsa_secure_pci_boot_test` (DS=0), `sbsa_smp_pci_boot_test`, `sbsa_pci_smmu_boot_test` (`iommu=smmuv3`, forced non-cacheable tables) and, with `nd_pci_msi=0`, everything stays on INTx (`sbsa_pci_nomsi_boot_test`).

Each probe build of `//kernel:sbsa_kc` takes about 9 minutes. Never run `bazel clean`: it throws the kernel build away.

### 2.1.6 Secondary CPUs: PSCI, banked GIC vectors, IPIs (P1-06)

- **Conduit.** neoboot writes `/chosen` `psci-conduit` (`dt-abi.md`): `"smc"` or `"hvc"` from the FADT's `ARM_BOOT_ARCH`; SMC when the FADT reports no PSCI but `ID_AA64PFR0_EL1.EL3` ≠ 0 (QEMU with TF-A); nothing when it says HVC but neoboot runs at EL2, or reports no PSCI on a CPU without EL3. Without a conduit neoboot appends `cpus=1` (unless `boot.cfg` names `cpus=` or `cpumask=`) and logs why. `NeoDarwinPSCI` issues `smc #0` or `hvc #0` accordingly, and logs `PSCI_VERSION` (`NeoDarwinPSCI: PSCI 1.1 over HVC`). `nd_psci_hvc` is the kernel's only `hvc`, allowed by name in `kernel/isa_audit/sbsa_release.txt`.
- **CPU_ON.** `AppleARMSMP::cpu_boot_thread` → `processor_boot` → `cpu_start` → `PE_cpu_start_internal` → `NeoDarwinPSCI::enableCPUCore(cpu, ml_vtophys(reset_vector_vaddr))` (patch 0017) → `CPU_ON(MPIDR from the cpu node's reg, LowResetVectorBase's physical address, 0)`. Return codes are named in the panic message; `ON_PENDING` is accepted. The CPU enters `start.s` with the MMU off, finds its `cpu_data` by MPIDR, and runs `arm_init_cpu`: its timer (`cpu_timebase_init`, and the timer PPI in `pe_init_fiq`, in the group `timer-group` names), then `pe_gic_cpu_init_hook`. The kernel gives each CPU one second to arrive (`cpu_boot_timeout_secs`, `processor_wait_for_start`).
- **GIC.** Every cpu node names SGI 0 and SGI 1, and AppleARMSMP registers them once per CPU. `NeoDarwinGICv3` keeps a cpu nub's INTIDs 0–31 in vectors of that CPU's own (`BankedCPU`) and enables them in that CPU's redistributor, found by walking `GICR_TYPER`; the IRQ loop dispatches SGIs and PPIs through the vectors of the CPU taking them. As each secondary comes up, `pe_init_fiq` (redistributor wake, `SRE`, `PMR`, `EOImode`, timer) ends in the hook, where the controller re-applies the CPU's SGI priorities and enables and sets `ICC_BPR1_EL1` and `ICC_IGRPEN1_EL1`. That second pass matters with TF-A, which resets a redistributor's SGIs and PPIs when `CPU_ON` powers its CPU. IPIs are `ICC_SGI1R_EL1` writes with Aff1, Aff2, `RS` and the Aff0 target bit from the MPIDR: Non-secure Group 1 whatever `DS` is.
- **Measured.** After `IOAllCPUInitialized` a thread bound to the boot CPU, with preemption disabled, times 128 cross-calls (`cpu_xcall`: SIGPxcall → `PE_cpu_signal` → SGI 0) to each other CPU until the target's handler has run. QEMU 11.1 TCG on an Apple M-series host, `neoverse-n2`: median 3–9 µs, minimum 2 µs, on `virt` and on `virt,secure=on` alike. Idle targets are in WFI, so a sample includes the wake-up. The counter QEMU gives is coarse at this scale (samples fall on 1 µs steps), so these numbers bound TCG's IPI path rather than any hardware's.

What P1-06's SMP half found:

| Finding | Fix |
|---|---|
| `Error registering IPIs @AppleARMSMP.cpp:138` on the second CPU: every cpu nub names the same banked SGIs, and `IOInterruptController` has one vector per number | per-CPU vectors for INTIDs 0–31 of cpu nubs in `NeoDarwinGICv3` |
| `reset_vector_vaddr` and passing it to the PMGR exist only `#if APPLEVIRTUALPLATFORM`: SBSA gave `enableCPUCore` an entry point of 0 | patch 0017 keeps them for `GENERIC_ARM64_PLATFORM` |
| QEMU's FADT says PSCI is absent under TF-A, although BL31 implements it | SMC when the CPU implements EL3 (`qemu-secure.md`) |
| `sbsa_isa_audit` forbids `hvc`, which QEMU `virt`'s PSCI needs | one named function, chosen at run time from the firmware, allowed by name in the baseline |
| TF-A resets each redistributor's SGI/PPI enables at `CPU_ON`, after the boot CPU enabled the secondary's IPIs | the GIC programs them again on the CPU itself (`pe_gic_cpu_init_hook`, patch 0017) |
| On the Group 0 timer path `pe_init_fiq` enables only `ICC_IGRPEN0_EL1`, so a secondary would never take a Group 1 IPI | the same hook sets `ICC_IGRPEN1_EL1` |
| `ICC_SGI1R_EL1` was written without `RS`, so a CPU with Aff0 ≥ 16 would get another CPU's IPI | `RS` = Aff0 / 16 |
| The measurement thread ran wherever the scheduler put it (cpu 3 in the first boot) | it binds itself to the boot CPU (`thread_bind`, which IOKit sees only through its own declaration) |

**For real hardware (P1-11, the Radxa Dragon Q8B).** Since 2026-09-29 the bring-up board is the Radxa Dragon Q8B, Qualcomm SC8280XP (8cx Gen 3), in parallel with FreeBSD's port of the same board (`../freebsd-src`, branch `radxa-dragon-q8b`, which boots it fully under ACPI). Its tables, read with neoboot's parser and `dtdump`, say:

| Area | Q8B | NeoDarwin |
|---|---|---|
| CPUs | 8 (4 Cortex-X1C + 4 Cortex-A78C, **Armv8.2**), MPIDR 0x000–0x700: DynamIQ, cores in Aff1 | P1-17, §2.1.8: the kernel's baseline is Armv8.2 (`-march=armv8.2-a+rcpc`, no range TLBI; patch 0019), enforced by `sbsa_isa_audit`. The reset vector matched MPIDR & 0xFF, so every core (Aff0 = 0) would have run on the boot CPU's `cpu_data`; it and the other CPU lookups now use Aff2:Aff1:Aff0 (patch 0020). QEMU tests: `cortex-a76` with 8 CPUs, and 18 CPUs for Aff1 (0x100, 0x101) |
| GIC | GICv3, GICD 0x17a00000, one GICR range of 8 × 128 KiB frames at 0x17a60000, ITS at 0x17a40000 | supported as is |
| Timer | virtual timer PPI 27, level | supported |
| PSCI | FADT: compliant, SMC | supported (P1-06) |
| Console | SPCR type 0x13, Qualcomm QUPv3 GENI UART at 0x884000 (the SPCR IRQ is wrong; the DSDT's `UARD` QCOM0616 has GSIV 615). The header pads are 1.8 V | the HDMI framebuffer console until P1-12: neoboot leaves the UART out of the tree when a GOP exists (DT-ABI v1.1, §2.1.7) and refuses the board without one. P1-12 adds a GENI pexpert driver; FreeBSD's `uart_dev_qcom_geni` is a BSD reference |
| PCIe | MCFG: 7 segments, ECAM above 4 GiB (0x400000000–0x700000000); NVMe on segment 2, the TC956x Ethernet on 4; IORT routes through a firmware-reserved SMMUv3 at 0x14f80000 | P1-09 and P1-10. One `NeoDarwinPCIHostBridge` per segment; its memory windows are all above 4 GiB, which IOPCIFamily could only use with patch 0023; PCIe masters reach 36 address bits (IORT); `_OSC` should grant 0x14, without AER (`pci.md`, "The Radxa Dragon Q8B") |
| DMA | the firmware reports `_CCA` 0 for the USB controllers, 1 for the PCIe host bridges | drivers must not assume coherent DMA (see the risk table); PCI nubs carry `dma-coherent` and `dma-address-bits` (`pci.md`) |
| Firmware | Qualcomm UEFI BOOT.MXF.1.1, ACPI and a DTB; RSDP 0xffffd000; no RTC (`GetTime` fails) | neoboot omits `neodarwin,utc-seconds`; the clock needs NTP |

Also still open from P1-06:
- With EL2 present, `CPU_ON` enters a secondary at the highest Non-secure EL, while `start.s` assumes EL1. On the Q8B, Qualcomm's hypervisor normally owns EL2 and the OS runs at EL1; check which EL neoboot starts in.
- Caches are real: `ResetHandlerData`, `CpuDataEntries` and the reset vector are read with the MMU off. `cpu_start` cleans the per-CPU entries; the rest is cleaned at boot only by neoboot's image clean. QEMU TCG can't catch a miss.

The Q8B's ACPI tables, as captured by the FreeBSD work, reconstructed into `acpidump` format (`boot/neoboot/testdata/radxa-dragon-q8b.acpidump`), reproduce these findings in `dtdump`: `//tools/dtdump:radxa_dragon_q8b_no_gop_test` and `radxa_dragon_q8b_gop_test`.

### 2.1.7 The framebuffer console (UEFI GOP)

The Q8B's serial console is a GENI UART on 1.8 V pads that nothing reads yet, while its UEFI firmware drives HDMI through GOP (FreeBSD shows its console there with efifb). So the kernel's console also goes to the firmware's framebuffer:

- **neoboot** (`Sources/GOP.swift`) looks up every `EFI_GRAPHICS_OUTPUT_PROTOCOL` handle, prefers the one that is also a console-out device, and takes its **current** mode: it never calls `SetMode`, since a mode switch can blank a monitor on real firmware. Only linear 32-bit formats are used (`PixelBlueGreenRedReserved8BitPerColor`, the xRGB word `video_console.c` draws, and `PixelRedGreenBlue…`, where coloured text swaps red and blue); `PixelBitMask` and `PixelBltOnly` (virtio-gpu's GOP) are skipped. It fills `boot_args.Video` with `FrameBufferBase`, `PixelsPerScanLine × 4`, the resolution and depth 32, with `v_display = 0` (`dt-abi.md`, `boot_args.Video`). If the framebuffer lies inside the DRAM window, the window loses it and keeps its larger side, as for any other hole, because the kernel owns every page of its window. The framebuffer is cleaned to PoC before `ExitBootServices`. `gop=off` in `boot.cfg` passes no framebuffer. The serial log names the mode: `neoboot: GOP: 800x600 BGRx, mode 1 of 3, 3200 bytes per row, framebuffer 0xbc7a0000 (0x300000 bytes)`.
- **The kernel** needed no new driver. `PE_init_platform` copies `boot_args.Video` on every arm64 board, and `PE_create_console` takes `kPETextMode` for `v_display = 0`. `initialize_screen` then maps the framebuffer, `kernel_bootstrap_thread` acquires the screen, and `vcattach` (from `PE_init_iokit`) replays the message buffer onto it. Patch 0021 does three things. It makes `_cnputc` hand every character to the video console as well while the serial console is selected (`serial=3`) and a framebuffer is mapped, so kernel printf, IOLog, panics and `/dev/console` (getty, the shell) reach both. Input stays on the serial port. It maps the framebuffer with `VM_WIMG_WCOMB`, which outside the DRAM window gives Device-GRE with gathered writes instead of Device-nGnRnE. It also fixes `io_map`'s early path, which reserved `round_page(size)` but mapped a page more when the address isn't 16 KiB aligned (GOP buffers are 4 KiB aligned). The kernel logs `video console: 800x600, 32 bpp, 3200 bytes per row, framebuffer at 0x…`.
- **Nothing tears it down.** The only callers of `kPEReleaseScreen`/`kPEDisableScreen` are `IOPlatformExpert::setConsoleInfo` (an `IOFramebuffer`, which doesn't exist) and the commented-out calls around `IOCPUSleepKernel`. `vcattach` releases and re-acquires the screen once, which clears it before the replay.
- **The framebuffer alone (DT-ABI v1.1).** When the SPCR names a UART the kernel has no driver for (the Q8B's GENI, type 0x13), or `boot.cfg` says `uart=off`, and a GOP exists, neoboot leaves `/arm-io/uart0` and `/defaults serial-device` out of the tree (`dt-abi.md`, "The console"). `serial_init` then finds no device, `arm_init` doesn't switch to the serial console despite `serial=3`, and `cons_ops_index` stays `VC_CONS_OPS`: the video console is the only console, as upstream, and patch 0021's mirror never runs. `PE_init_kprintf` points `kprintf` at `console_write_unbuffered`, the same console. `consdebug_putc`, which panics print through, also copies each character to `PE_kputc` when the console isn't serial, so every panic character was drawn twice; patch 0021 skips that when `PE_kputc` is the console itself. There is no input yet: no serial device and no USB HID, so getty waits at `login:`. neoboot stops writing to the UART at `ExitBootServices`, so serial shows only its earlier lines, which also reach HDMI through the firmware's console. Without a GOP neoboot refuses such a board, since the kernel would have no console.
- **Cost.** Every character is drawn, and every newline scrolls the whole framebuffer with `bcopy`. The cursor is drawn by reading pixels back. On Device memory those reads are uncached: about 8 MiB per line at 1080p. That is tolerable for bring-up; a shadow buffer or repainting from `gc_buffer_*` would fix it.

Without the patch the screen showed the log only up to `PE_init_iokit` (the replay), ending at `Initializing serial KDP`, and nothing after it. `//kernel:sbsa_fb_console_boot_test` boots the getty session with QEMU's `ramfb`, which EDK2 drives through GOP (`QemuRamfbDxe`, 800x600, in `EfiReservedMemoryType` above the DRAM window). It logs in over serial, runs `echo fb-$((6*7)); uname -sm`, takes a screendump through QEMU's monitor and reads the screen back with xnu's own 8x16 font (`qemu_efi_test.sh --screen-font`). It requires `fb-42` and `Darwin arm64` on screen as well as on serial. `//boot/neoboot:neoboot_gop_qemu_test` checks the loader's side. `//kernel:sbsa_fb_only_boot_test` boots the same image with `uart=off`: it reads screendumps every few seconds (`qemu_efi_test.sh --until-screen`) until getty_init's line, getty's banner and `login:` are on the screen, and fails if the kernel's banner or neoboot's post-`ExitBootServices` line reaches serial (`--absent`). `neoboot_uart_off_qemu_test` and `neoboot_uart_off_no_gop_qemu_test` check neoboot's choice and its refusal.

On the Q8B, still to check: where the firmware's framebuffer lies and what memory type the UEFI map gives it (it must be outside the window, or neoboot trims the window); that the current mode is the monitor's native one; and that the display engine scans out what the kernel's Device-GRE writes put in memory. neoboot prints the framebuffer's address, mode and UEFI memory type, both to serial and to its firmware console, which is the same HDMI screen.

### 2.1.8 An Armv8.2 baseline and DynamIQ CPU numbering (P1-17)

The Q8B's cores are Cortex-X1C and Cortex-A78C: Armv8.2 plus some later features. FreeBSD reads them as ISAR0 `CondM-8.4,DP,RDM,Atomic,CRC32,SHA2,SHA1,AES+PMULL`, ISAR1 `GPA,RCPC-8.4,APA EPAC2,DCPoP`, PFR0 without DIT, SVE or AMU, and PFR1 `SSBS`. They have no FEAT_TLBIRANGE, DIT, SHA3/SHA512, RNDR or MTE. Their MPIDRs are DynamIQ's, 0x000–0x700: the core number is in Aff1 and Aff0 is 0. QEMU's `cortex-a76` has the same Armv8.2 ISA, with RCpc at the 8.3 level (LDAPR without LDAPUR).

**Baseline.** The kernel builds with `-march=armv8.2-a+rcpc` (patch 0019). Every Armv8.3+ dependency the build had, and what happened to each:

| Dependency | Where | Now |
|---|---|---|
| FEAT_TLBIRANGE (`TLBI RVAE1IS`, `RVALE1IS`, …) | `__ARM_RANGE_TLBI__` in `SBSA.h`: pmap's range flushes, `pmap_clear_refmod_range_options` | not defined. pmap flushes by page, or by ASID past 256 pages, as on Apple's pre-A14 configurations; `pmap_clear_refmod_range_options` returns false and the VM clears refmod bits page by page |
| Range TLBI under `__ARM_MIXED_PAGE_SIZE__` | `pmap_switch_user`'s commpage flush on a switch between 4K and 16K pmaps: unguarded, since every Apple mixed-page configuration has range TLBI | patch 0019 guards it; without range TLBI the switch flushes the local TLB, as the shared-region switch already did (the first build failed here) |
| FEAT_DIT (`PSTATE.DIT`) | `cswitch.s` saves and restores it per thread `#if __ARM_ARCH_8_4__` | on `GENERIC_ARM64_PLATFORM`, only when `gARM_FEAT_DIT` (from `ID_AA64PFR0_EL1`, set by commpage init) says the CPU has it, through the encoded name `S3_3_C4_C2_5`. corecrypto's `CC_ENSURE_DIT_ENABLED` compiles to nothing in the kernel (`CC_HAS_DIT()` is 0 outside Apple's internal SDK): the audit finds no `MSR DIT, #imm` |
| FEAT_LSE2 (16-byte single-copy atomicity) | `os_atomic_load_is_plain` treats 16-byte loads as atomic `#if __ARM_ARCH_8_4__` | off at 8.2: those loads use the exclusive-pair path |
| RCpc (LDAPR) | compiler code generation for acquire loads | kept (`+rcpc`): the Q8B and `cortex-a76` both have it. LDAPUR (RCpc 8.4, which QEMU's `cortex-a76` lacks) is not generated at 8.2 |
| PAC, BTI, SB, `DC CVADP` and later | none | plain `arm64` without BTI, as before; the audit would list any of them |

**The ISA audit.** `//kernel:sbsa_isa_audit` (`tools/xnu/isa_audit.sh --mattr +v8.2a,+rcpc,+dotprod,+aes,+sha2,+fullfp16`, the ISA both the Q8B's cores and `cortex-a76` have) disassembles the kernel twice: once with every feature LLVM knows and once with that list only. A word the two decode differently needs a feature outside the list. Plain instructions then show as `<unknown>`, and system-instruction aliases fall back to their generic form: `TLBI RVAE1IS` becomes `sys`, `MSR DIT` becomes `S3_3_C4_C2_5`. Two kinds of difference are allowed, because they're safe on Armv8.2: hint-space words (`hint #N`: PACIBSP, BTI and the like are NOPs where they aren't implemented) and `MRS` from the ID register space `S3_0_C0_*` (RAZ where unallocated; the kernel reads `ID_AA64SMFR0_EL1` in commpage init). The Apple-ISA checks (C15 system registers, `hvc`, AMX, GXF) still run. `kernel/isa_audit/sbsa_release.txt` allows three things by name:
- the HVC PSCI conduit;
- the six DIT save/restore sites in the context-switch macros, guarded at run time;
- ten data words in `__TEXT_EXEC` that decode as SVE, SME or `CB<cc>` only with every feature on: lz4's constant tables, and panic strings placed after `fleh_*`, `preempt_underflow`, `_update_mdscr` and `_os_cpu_in_cksum_mbuf`.

The Armv8.4 build executed `TLBI RVALE1IS` (§2.1.2) and touched DIT on every context switch; the Armv8.2 kernel's TLB maintenance is `TLBI VMALLE1(IS)`, `ASIDE1(IS)`, `VAE1IS`, `VALE1IS`, `VAAE1IS` and `VAALE1IS` only.

**Userland.** swiftc and clang target `apple-m1` for `arm64-apple-macos` unless told otherwise. On `cortex-a76` the first session boot ran the kernel and launchd, then `launchctl` died with SIGILL on `LDAPUR` (RCpc 8.4). `tools/darwin_executable/build.sh` and `tools/static_macho/build_static_macho.sh` now pass `-target-cpu cortex-a76` to swiftc and `-mcpu=cortex-a76` to clang. The Q8B has LDAPUR, but not every `apple-m1` feature (FHM, SHA3, FRINTTS, FlagM2, SB), so this matters on the board too. The C base (`tools/base/common.sh` `TARGET_FLAGS`, plain `-arch arm64`) passes the same double-disassembly check except for Apple's run-time-gated paths, which the commpage steers: DIT in `libsystem_platform`'s `timingsafe_*` and dyld's corecrypto, `CNTVCTSS_EL0` in `mach_absolute_time`, and `STG` in libunwind. It still targets Xcode clang's default CPU; pinning it to `cortex-a76`, and auditing the images, is still to do.

**DynamIQ MPIDRs.** The cpu node's `reg`, which the kernel keeps as `cpu_phys_id`, has always been MPIDR Aff2:Aff1:Aff0 (`dt-abi.md`). Without `HAS_CLUSTER`, xnu matched less of it (patch 0020 fixes each on `GENERIC_ARM64_PLATFORM`):
- the reset vector (`start.s`) compared `MPIDR_EL1 & 0xFF` with `cpu_phys_id`. On the Q8B every core has Aff0 = 0, so every secondary would have found the boot CPU's `cpu_data` and run on it. It now compares bits 23:0; the MT bit (24), which DynamIQ cores set, is outside the mask;
- `ml_get_cpu_number()` (used by `NeoDarwinGICv3` to map a cpu nub to its CPU) and `find_gicr_pe_base()` (`pe_fiq.c`, each CPU's redistributor) compared Aff1:Aff0. That is enough for the Q8B, whose Aff2 is 0, but not for Aff2 ≠ 0; both now use Aff2:Aff1:Aff0.
`NeoDarwinGICv3` already matched redistributors on Aff3.Aff2.Aff1.Aff0 and built `ICC_SGI1R_EL1` from Aff1, Aff2, `RS` and the Aff0 bit (P1-06), and `NeoDarwinPSCI` passes the whole `reg` to `CPU_ON`, which now logs `NeoDarwinPSCI: CPU_ON cpu 16, MPIDR 0x100`. neoboot refuses a MADT whose enabled GICCs have Aff3 ≠ 0, because the kernel's CPU id holds 24 bits.

**Clusters.** Without `HAS_CLUSTER`, `ml_parse_cpu_topology` takes a CPU's cluster from `cluster-type` alone. neoboot writes none, so every CPU is in logical cluster 0: one SMP processor set, which is what a DynamIQ cluster is (the Q8B's eight cores share one DSU). neoboot writes `die-cluster-id` 0 and `cluster-core-id` = index so the per-CPU fields describe that cluster; the kernel's defaults (MPIDR Aff1 and Aff0) would be a core number and 0 on DynamIQ. Splitting the X1C and A78C cores (`cluster-type` `'P'`/`'E'`) needs the AMP scheduler, which SBSA doesn't build.

**QEMU.** `virt` can't give eight CPUs DynamIQ numbers. `virt_cpu_mp_affinity` (`hw/arm/virt.c`) puts 16 CPUs in each Aff1 value, since that is what fits `ICC_SGI1R_EL1`'s target list, whatever `-smp`'s sockets, clusters and cores say (8 CPUs as `clusters=2,cores=4` or `clusters=8,cores=1` are still 0x0–0x7). So:
- `//kernel:sbsa_a76_smp8_boot_test`: `cortex-a76`, 8 CPUs, to root's shell; `hw.ncpu hw.activecpu` = 8 8;
- `//kernel:sbsa_a76_aff1_boot_test`: `cortex-a76`, 18 CPUs, where cpu 16 and 17 are MPIDR 0x100 and 0x101. 0x100 has Aff0 = 0, like the boot CPU, which is the Q8B's failure. All 18 come up, cpu 16 and 17 answer IPIs sent through `ICC_SGI1R_EL1`'s Aff1 field, and `hw.ncpu hw.activecpu` = 18 18;
- `//kernel:sbsa_a76_secure_smp8_boot_test`: `cortex-a76`, 8 CPUs under TF-A (PSCI over SMC, the timer on Group 1). TF-A's QEMU platform stops at eight cores, so it can't run the Aff1 case;
- `//tools/dtdump:qemu_virt_a76_smp18_test` checks the tree for those tables (captured with `dump-acpi`), and `mpidr_aff3_test` checks the Aff3 refusal.
The existing tests keep `neoverse-n2`.

**Only on the Q8B:**
- the reset-vector match and GIC redistributor lookup with Aff0 = 0 on every core, and the MT bit set;
- that the firmware's `CPU_ON` accepts the MADT's MPIDRs as they are;
- whether Qualcomm's EL2 leaves the kernel at EL1 on secondaries;
- the userland's C base on real Armv8.2 cores.

### 2.2 ACPI → kernel: the two-tier strategy

**Tier 1 (loader, static tables → Apple DT).** The kernel's early boot reads a fixed set of DT nodes through `SecureDT*` (`pexpert/gen/device_tree.c`). The DT binary format is Apple's, not FDT: nodes are `{u32 nProperties; u32 nChildren; props[]; children[]}` with each property `{char name[32]; u32 length; u8 value[length] (4-byte aligned)}` (`pexpert/pexpert/device_tree.h:74-90`). The loader emits this directly.

Required node/property contract (the "DT-ABI"), derived from the source reads. **The versioned contract is `dt-abi.md` (DT-ABI v1, P1-04)**, which gives each property's ACPI source and kernel reader with line numbers, and the checks neoboot and `dtdump` apply. The table below is the original design sketch; where they differ, `dt-abi.md` is what neoboot does (e.g. `/arm-io` ranges are computed from the device addresses, and the ACPI tables are copied into the DRAM window).

| Node | Property | Source of value | Consumer |
|---|---|---|---|
| `/` | `name="device-tree"`, `model`, `target-type`, `compatible` | SPCR/DBG2 OEM ids, FADT OEMID, hardcoded `"NeoDarwin,sbsa"` | `pe_init.c:451-470`; IODTPlatformExpert `probe` name-match (`IOPlatformExpert.cpp:1615-1627`) |
| `/chosen` | `dram-base`, `dram-size` (u64) | = `physBase`, `memSize` | `arm_init.c:545-559` (panics if absent) |
| `/chosen` | `random-seed` (≥64 B) | EFI_RNG_PROTOCOL or RNDR/CNTPCT mix | `pexpert/gen/pe_gen.c:145-178` → PRNG seed |
| `/chosen` | `debug-enabled` (u32), `osenvironment`, `boot-uuid` | cfg | `pe_init.c:471-520`, `IOKitBSDInit.cpp:428` |
| `/chosen` | `neodarwin,utc-seconds` (u64, seconds since 1970, omitted if the firmware has no clock), `neodarwin,utc-counter` (u64, CNTVCT when it was read) | UEFI `GetTime()` before `ExitBootServices`, read as UTC | **new**: `NeoDarwinPlatformExpert`, the time of day behind `IORTC` (§2.1.2) |
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

**As built (P1-09 checkpoint 1, `acpi.md`).** Not a kext yet: for the reason HFS+ isn't (§2.1.4, no kexts before M5), ACPICA and `NeoDarwinACPIPlatform` are compiled into IOKit by patch 0018, and `NeoDarwinPlatformExpert` starts the platform on a thread of its own. ACPICA is configured hardware-reduced (`ACPI_REDUCED_HARDWARE`): no SCI, GPEs or global lock. The tables are read through the physmap, since neoboot copied them into the DRAM window; OperationRegions outside DRAM are mapped as device memory. **Checkpoint 2** (`pci.md`) adds item 4 without the ITS: ECAM from MCFG and `_CBA` for an open IOPCIFamily build (patches 0022 and 0023) on `NeoDarwinPCIHostBridge`, and `_PRT` routing. **Checkpoint 3** (`gic-its.md`) adds MSI allocation on the ITS, compiled in rather than in a GIC kext; `_DSD` is still to come.

### 2.3 Kernel-side platform layer: the `SBSA` board config

Created by forking `VMAPPLE`:

| Item | Change | Grounding |
|---|---|---|
| `pexpert/pexpert/arm64/SBSA.h` | copy of `VMAPPLE.h` minus `CPU_HAS_APPLE_PAC`, `HAS_PARAVIRTUALIZED_PAC/CTRR`, `HAS_ARM_FEAT_SME*`, `APPLEVIRTUALPLATFORM`; keep `NO_MONITOR`, `HAS_GIC_V3`, `PL011_UART`, `__ARM_16K_PG__` (or 4K variant), GIC register defines; add `GENERIC_ARM64_PLATFORM 1`, `NO_XNU_PLATFORM_ERROR_HANDLER`, `USE_APPLEARMSMP` | `VMAPPLE.h`, `board_config.h:281-295` |
| `board_config.h` | new `#ifdef ARM64_BOARD_CONFIG_SBSA` block, `MAX_CPUS 64`, `MAX_CPU_CLUSTERS 8`, `MAX_L2_CLINE 7` | `board_config.h:281-295` |
| `makedefs/MakeInc.def` | `SUPPORTED_ARM64_MACHINE_CONFIGS += SBSA`; `MACHINE_FLAGS_ARM64_SBSA = -DARM64_BOARD_CONFIG_SBSA -march=armv8.2-a+rcpc` (Armv8.4 until P1-17, §2.1.8); build with `ARCH_STRING_FOR_CURRENT_MACHINE_CONFIG=arm64 BTI_BUILD=0` (no `arm64e`, no BTI on pre-8.5 cores); `EXTRA_TARGET_CONFIGS_RELEASE="SBSA"` sidesteps the missing EmbeddedDeviceMap (`MakeInc.def:298-320`) | `MakeInc.def:32, 55-66, 87, 293-326` |
| `osfmk/arm64/proc_reg.h` | add `#elif defined(SBSA)` cache-line branch | `proc_reg.h:238-247` (`#error processor not supported`) |
| `apple_arm64_common.h` → `generic_arm64_common.h` | drop `AIC.h`, `apple_arm64_regs.h`, `apple_arm64_cpu.h`, `apple_uart_regs.h`; keep `ARM_ARCH_TIMER`, `__ARM_COHERENT_CACHE__`; **do not** define `APPLE_ARM64_ARCH_FAMILY` | `apple_arm64_common.h` |
| `APPLEVIRTUALPLATFORM` sites (≈30) | audit each: GIC ack in `sleh_fiq` (`sleh.c:2624, 2670`; done, patch 0014) and `reset_vector_vaddr` (`arm_init.c:192, 359`; `AppleARMSMP.cpp:279-281`; done, patch 0017; the sleep paths in `cpu.c:97-101, 351-355, 1086-1132` wait for system sleep) become `GENERIC_ARM64_PLATFORM`; hypercall probes in `arm64_hypercall.c` and `machine_routines_apple.c:197-264` compile to the `#else` stubs (an `hvc` at EL1 with no hypervisor is UNDEFINED → panic) | grep list in this drop |
| Implementation-defined sysregs | none in the SBSA kernel: `generic_arm64_common.h` leaves `APPLE_ARM64_ARCH_FAMILY` undefined, and patch 0006 removes the Apple PMC counter driver and AWL writes | `//kernel:sbsa_isa_audit` (disassembly; the baseline allows only NeoDarwinPSCI's `hvc` conduit, P1-06) |
| Timer | virtual timer (`CNTV_*`, `machine_routines.c:2383-2551`) + PPI 27 — matches SBSA; loader must leave `CNTVOFF_EL2 = 0` | `pe_fiq.c:147-150` |
| IPIs | no `HAS_IPI` → `PE_cpu_signal` → `gAIC->sendIPI` when the CPU node exposes 3 interrupt specifiers (`aic_ipis = true` path). AppleARMSMP registers them once per CPU, so `NeoDarwinGICv3` banks SGI/PPI vectors per CPU (P1-06) | `AppleARMSMP.cpp:131-144, 332-360`; `cpu_common.c:503-521` |
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
- **Console.** `serial=3` makes the kernel console the PL011. Input is polled by the serial keyboard thread (`serial_keyboard_poll`, `osfmk/console/serial_general.c:81`, every 16 ms) into the console tty (`cons_cinput`, `bsd/dev/arm/km.c:396`); `PE_stub_poll_input` is the video console's path. `/dev/console` and a getty need no IOSerialFamily (P1-08, `docs/base/session.md`).
- **NVRAM.** No UEFI runtime services after handoff → `IODTNVRAM` backed by an in-memory dictionary, persisted later to a file on the ESP by a userland daemon.
- **Time of day.** No UEFI runtime services also means no `GetTime()`/`SetTime()`. neoboot reads the clock before handoff, and `NeoDarwinPlatformExpert` keeps it running from the counter and publishes `IORTC` (§2.1.2). Writing the time back to the hardware clock is later work, like NVRAM persistence, and needs a real RTC driver or a userland daemon.

---

## 3. Agent swarm division of labour

Each agent owns a directory, a test, and a written contract. Dependencies flow downward; parallel columns start on day 1.

| # | Agent | Inputs | Outputs | Depends on | Definition of done |
|---|---|---|---|---|---|
| A0 | **Build & Toolchain** | xnu tree, Xcode toolchain, this doc | `SBSA` machine config; `make SDKROOT=macosx TARGET_CONFIGS="RELEASE ARM64 SBSA" ARCH_STRING_FOR_CURRENT_MACHINE_CONFIG=arm64 BTI_BUILD=0` produces `kernel.release.sbsa`; `kcgen` (open MH_FILESET linker for M5); QEMU harness `tools/run-qemu.sh` (`-M virt,gic-version=3 -cpu cortex-a76 -bios QEMU_EFI.fd`) with serial capture and pass/fail grep | — | kernel links; harness boots `BOOTAA64.EFI` and asserts on serial output |
| A1 | **UEFI Loader (neoboot)** | §2.1 rules, DT-ABI table, ACPI spec 6.5 (MADT/GTDT/SPCR/MCFG/IORT), Mach-O fileset + chained-fixups format, Embedded Swift UEFI target from A0 | `BOOTAA64.EFI` (Embedded Swift, C shim); `dt-abi.md` (generated from code); `dtdump` host tool that prints the synthesised DT | A0 (kernel image, Embedded Swift target) | kernel reaches `arm_init` and prints `iBoot version:` line on QEMU virt |
| A2 | **Kernel Platform Bridge** | §2.3 table, grep lists | `SBSA.h`, `generic_arm64_common.h`, `proc_reg.h` branch, `APPLEVIRTUALPLATFORM` audit, 16550 serial driver, Group 1 timer option (§5.2), `pe_fiq.c` reads PPI number from `/arm-io/gic` `timer-ppi` | A0 | single-CPU boot to `kernel_bootstrap` complete, timer interrupts counting |
| A3 | **IOKit Platform (in-kernel)** | §2.3 class table, IOKit headers | `iokit/Kernel/arm/NeoDarwin*.cpp`: platform expert, GICv3 controller, PSCI IOPMGR, PSCI halt/restart | A2 | all CPUs online (`cpus=N` boot-arg respected), IPIs measured, SPI test device (virtio console) interrupts |
| A4 | **ACPI Runtime (ACPICA kext)** | ACPICA source, `IOACPIPlatformDevice.h` SDK header, IORT/MCFG | `NeoDarwinACPIPlatform.kext`, `IOPCIFamily` open build wired to ECAM, MSI via GIC ITS | A3, A0 (`kcgen`) | `ioreg` shows PCI bus with virtio-pci / NVMe nubs on QEMU and on the Q8B |
| A5 | **Storage & Root FS** | HFS+ source, IOStorageFamily, virtio spec, NVMe spec | `hfs.kext` build, HFS+ ramdisk builder, `virtio-blk` and open `NVMe` IOKit drivers | A3 (ramdisk), A4 (PCIe) | M4: root on HFS+ ramdisk; M5: root on NVMe volume by `boot-uuid` |
| A6 | **BSD / Userland Glue** | PureDarwin/Darling recipes, launchd-842, xnu headers | cross toolchain SDK; `ramdisk.img` builder; `/sbin/launchd`, `/etc/rc`, getty on `/dev/console`, `sh`, coreutils; ad-hoc signing policy | A0 (headers) — independent of kernel until M3 | `login:` prompt on serial, shell commands run, `uname -a` correct |
| A7 | **Verification & CI** | all of the above | QEMU virt + sbsa-ref matrix (16K/4K, 1/4/8 CPUs, with/without secure firmware via TF-A `QEMU_EFI` built with `ARM_TRUSTZONE`), hardware-in-the-loop runner for the Radxa Dragon Q8B (serial at 1.8 V + power relay), boot-time budget, regression corpus of DT dumps | A0 | every merge boots both QEMU machines; nightly hardware run |
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
| **M6 Hardware** | Radxa Dragon Q8B (Qualcomm SC8280XP, GICv3, GENI UART, UEFI+ACPI) boots to shell | A7 + all | HIL nightly green |
| **M7 Graphics groundwork** | GOP framebuffer via `Boot_Video` → `IOFramebuffer` stub; input over USB (open IOUSBHostFamily build); the framebuffer console (`docs/architecture/console.md`) | new agents | pixels on HDMI from userland |

---

## 5. Critical path and risk analysis

### 5.1 Risk 1 — Virtual-memory handoff (single DRAM window, fixups, cache state)
*Failure mode:* kernel faults in `start.s` or `arm_vm_init` with no serial output; or boots and later corrupts firmware-reserved memory.
*Root causes grounded in code:* `boot_args` has one `{physBase, memSize}` (`boot.h:69-71`); physmap covers exactly that range (`arm_vm_init.c:1835-1869`); the kernel expects fixups applied and a 2 MiB-aligned base (`start.s:553-575`); everything read before the MMU is on must be clean to PoC.
*Mitigation:*
- Loader chooses the largest hole-free conventional run; refuses to boot below 512 MiB; logs the map. `memSizeActual` carries the full total.
- Deterministic first-light mode: `slide=0`, KASLR off, `-noprogress`, `debug=0x14e`, so an early fault is reproducible; A7 keeps a golden `boot_args`+DT dump.
- No loader-side fixup walker: the kernel applies its own chains. `kcheck` (P0-07) checks every chain against the rules of the kernel's walker, checks that every target lands inside the image, and round-trips the collection byte for byte against its source kernel.
- Explicit cache clean + `dsb sy; ic iallu; tlbi vmalle1` before MMU-off; QEMU cannot catch this, so A7 runs the sequence on hardware early (M6 pre-work on any UEFI SBC).

### 5.2 Risk 2 — Interrupt-controller binding (GICv3 Group 0 FIQ vs. TrustZone)
*Failure mode:* boots perfectly on QEMU and under Apple's hypervisor, hangs at first timer interrupt on real boards.
*Root cause:* `pe_init_fiq` puts the timer PPI in **Group 0** and enables `ICC_IGRPEN0_EL1` (`pe_fiq.c:144-170`); `sleh_fiq` acks via `ICC_IAR0_EL1` (`sleh.c:2624`). On silicon with TF-A, `GICD_CTLR.DS = 0`, Group 0 is Secure, non-secure EL1 cannot own it and the FIQ is routed to EL3. QEMU `virt` without `secure=on` has `DS = 1`, which hides the bug.
*Mitigation (designed in from day 1, A2):*
- New DT property `/arm-io/gic` `timer-group` (0 or 1). With group 1: `pe_init_fiq` programs `GICR_IGROUPR0` bit 27 = 1 and `ICC_IGRPEN1_EL1`; the timer then arrives as **IRQ**. In `sleh_irq`/`PE_handle_ext_interrupt`, the GIC controller's `handleInterrupt` recognises INTID 27 and calls the same `rtclock_intr` path the FIQ handler uses (`sleh.c:2650-2665`), acking with `ICC_EOIR1_EL1`. Boards set `timer-group=1`; QEMU tests both.
- **Done (P1-05, kernel and loader).** neoboot takes the PPI from the GTDT and chooses the group from `GICD_CTLR.DS`: Group 1 when it reads 0 (two security states), else Group 0. `timer-group=1` in `boot.cfg` forces Group 1, and `/arm-io/gic` carries `timer-ppi` and `timer-group` (`dt-abi.md`). Patch 0016 programs Group 1 without touching a Group 0 register. Until `NeoDarwinGICv3` is attached to the PassthruInterruptController, `sleh_irq` acknowledges the timer itself: `PE_handle_ext_interrupt()` has no child to call before then, and ticks start long before IOKit. After that, the controller's IAR1 loop recognises the timer INTID. Both paths call `sleh_fiq`'s timer branch. `//kernel:sbsa_timer_group1_boot_test` boots the session this way on QEMU. When the controller attaches it logs `NeoDarwinGICv3: timer PPI 27 on Group 1 (IRQ); N timer interrupts taken as IRQs so far`, with N = 5 on QEMU; later ticks go through the controller, and a 1 s `sleep` in zsh wakes after at least a second. The log line comes from the controller because output from `pe_init_fiq` (a `kprintf`) never reached the serial log in a trial boot. Still to confirm: the DS=0 half on QEMU with TrustZone firmware (`secure=on`).
- **DS=0 on QEMU (P1-05).** `--machine virt-secure` in `tools/efi/qemu_efi_test.sh` boots `virt,secure=on` with TF-A v2.15.0 at EL3 and EDK2 ArmVirtQemuKernel as BL33, both built reproducibly from pinned source (`//third_party/qemu_firmware:virt_secure_flash`). neoboot reads `GICD_CTLR = 0x12` there (DS=0), against `0x52` without EL3. `//boot/neoboot:neoboot_secure_qemu_test` and `//kernel:sbsa_secure_boot_test` cover it. On the Group 0 path the timer stays Group 1 Non-secure and pending, then arrives as an unhandled IRQ once `ICC_IGRPEN1_EL1` is set, and the kernel panics in `PE_handle_ext_interrupt`. The configuration, pins and measurements are in `qemu-secure.md`.
- IPIs are SGIs in Group 1 through `sendIPI` (`ICC_SGI1R_EL1`, with `RS` for Aff0 ≥ 16); CPU nodes carry three `interrupts` specifiers so AppleARMSMP takes the `aic_ipis` path (`AppleARMSMP.cpp:131-144`). SGIs are Non-secure Group 1 on every target: `pe_init_fiq` writes `GICR_IGROUPR0` with DS=1, TF-A with DS=0. TF-A resets a redistributor's SGIs and PPIs when `CPU_ON` powers its CPU, so `NeoDarwinGICv3` programs them again on the CPU itself (`pe_gic_cpu_init_hook`, patch 0017).
- `PMR = 0xFF`, `BPR`, `EOImode = 0` as VMAPPLE does; A7 adds a TF-A-backed QEMU config (`secure=on` with `ARM_TRUSTZONE` firmware) to CI so the DS=0 case is tested in software before hardware.
- Redistributor discovery already loops `GICR_TYPER` for the CPU's affinity (`pe_fiq.c:44-68`); the loader passes one range from the start of the MADT's GICR structure (or the contiguous GICC frames), sized one 128 KiB frame per GICC rather than the whole reservation (`dt-abi.md`). GICv4's 256 KiB frames are refused until the kernel takes its stride from the tree (P1-11).

### 5.3a Measured: the published XNU source does not link on its own

Building `xnu-12377.1.9` VMAPPLE RELEASE from public sources (`//kernel:vmapple_release_gaps`, 2026-09-27) compiles every file but leaves **395 undefined symbols** at link. Apple's public build fills them from a closed per-SoC archive in the login-gated Kernel Debug Kit (`libVMAPPLE.os.RELEASE.a`). NeoDarwin does not use the KDK; the gaps close from source instead:

| Gap | Symbols | Plan |
|---|---|---|
| ARM64 machine code and pmap present in the tree but excluded by the public config options `nos_arm_asm`/`nos_arm_pmap` | about 237 | `kernel/patches/0001` enables them; the SBSA board config supplies the guards the newly built files need (Apple `CPU_OVRD` registers, per-SoC tunables, Apple IOMMU headers) |
| Required only by export lists (Tightbeam 111, `IOUnifiedAddressTranslator` 19, others) | 157 | NeoDarwin drops the exclaves and Apple-IOMMU export lists |
| Apple SoC IOMMUs, kernel-integrity regions, NVMe PPL | 26 | not built for SBSA |
| libTrustCache runtime | 2 | NeoDarwin stub |

The categories overlap (some pmap symbols are also exported). The authoritative list is `kernel/link_gaps/vmapple_release.txt`, and a ratchet test keeps it from growing.

**Status, 2026-09-27:** closed. `//kernel:sbsa_release` links `kernel.release.sbsa` (Mach-O `arm64`, no undefined symbols) from the public archive plus `kernel/patches/0001`–`0005`; `kernel/README.md` records how each gap closed. Apple-specific instructions are audited separately (`//kernel:sbsa_isa_audit`: implementation-defined registers, `hvc`, AMX, GXF); after patch 0006 the kernel has none.

### 5.3 Risk 3 — Closed kexts and kernelcache tooling
*Failure mode:* the project stalls on binaries it cannot build: `AppleARMPlatform`, `AppleInterruptController`, `AppleVirtualPlatform`, `AppleMobileFileIntegrity`, `apfs`, `IONVMeFamily`, and the `kmutil`/EmbeddedDeviceMap build tooling.
*Mitigation:*
- Compile the boot-critical platform layer into the kernel image (§2.3), so M0–M4 need no kernelcache at all; `AppleARMSMP.cpp` is the in-tree precedent.
- Explicit *no-closed-code* list with open replacements: platform expert (new), GIC (new), PSCI PMGR (new), AMFI (none needed; `cs_enforcement_disable`), apfs → HFS+ (open), NVMe (new, spec-driven), IOPCIFamily/IOStorageFamily/IOUSBHostFamily (open source drops).
- `kcgen` (A0, P0-07) implements `MH_FILESET` + `LC_FILESET_ENTRY` + kernel-cache fixup-chain emission in the format the kernel already walks (`osfmk/mach/dyld_kernel_fixups.h`), so M5 does not depend on `kmutil`. The kernel-only collection exists; kexts join it at M5.
- EmbeddedDeviceMap bypass is a supported path in the makefiles (`EXTRA_TARGET_CONFIGS_*`, `MakeInc.def:305-320`), not a hack.

### 5.4 Secondary risks (tracked, not on the critical path)
| Risk | Signal | Mitigation |
|---|---|---|
| 16 KiB granule unsupported on target core | `ID_AA64MMFR0_EL1.TGran16 == 0` | loader refuses 16K kernel and picks the 4K build from the ESP |
| BTI / PAC assumptions | `XNU_BUILT_WITH_BTI` default on (`MakeInc.def:57-66`); arm64e ABI | build `arm64`, `BTI_BUILD=0` for the Armv8.2 baseline (§2.1.8); enable per-board when v8.5 |
| SME/SVE feature flags in `VMAPPLE.h` | illegal-instruction on cores without SME | drop `HAS_ARM_FEAT_SME*`; gate on `ID_AA64PFR1_EL1` |
| Non-coherent DMA on cheaper SoCs | data corruption in virtio/NVMe | `__ARM_COHERENT_IO__` off for those boards; IODMACommand cache ops in the drivers (the Q8B's firmware reports `_CCA` 0 for USB; check each device's `_CCA` and the IORT) |
| PSCI/SMC from EL1 trapped by a hypervisor | `SMC` UNDEF | loader verifies `CurrentEL` and `HCR_EL2.TSC = 0`; the conduit comes from the firmware: `/chosen` `psci-conduit` (P1-06); HVC is refused when neoboot itself runs at EL2 |
| `hvc` probes left in | UNDEF at `arm64_hypercall.c:62` | A2 audit; CI runs on QEMU with `-cpu` lacking EL2 |
| Legacy `interrupt-controller`/`timer` node lookup | `ml_init_timebase` skipped → no `rtclock_timebase_func` | loader emits both stub nodes; A2 removes the requirement later |
| No UEFI runtime → no NVRAM | `nvram` writes lost | in-memory `IODTNVRAM`, ESP file persistence at M4 |
| Licensing | APSL 2.0 kernel, ACPICA dual BSD/GPL, EDK2 BSD+Patent | keep loader and kexts BSD-2; no GPL in kernel image |

---
