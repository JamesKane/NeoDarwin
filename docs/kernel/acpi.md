<!-- SPDX-License-Identifier: BSD-2-Clause -->
# ACPI in the kernel: ACPICA and IOACPIPlatformDevice (P1-09)

**P1-09, checkpoint 1.** The SBSA kernel runs ACPICA over the tables neoboot hands it and publishes an `IOACPIPlatformDevice` nub for each device in the ACPI namespace. This is Tier 2 of `arm64-sbsa-bringup.md` §2.2: Tier 1 is neoboot's device tree, which carries only what early boot needs (GIC, timer, UART, CPUs). The DSDT and SSDTs describe everything else: the PCI host bridge, virtio-mmio slots, the GED, the RTC, GPIO blocks, a real board's peripherals.

Checkpoint 2, IOPCIFamily over ECAM (MCFG, `_CBA`) with `_CRS` windows and `_PRT` routing, is done: `pci.md`. Checkpoint 3 is MSIs through the GIC ITS; P1-10 then adds drivers that match the PCI nubs. What checkpoint 2 took from this one is at the end.

## Pieces

| Where | What |
|---|---|
| `@acpica` (`MODULE.bazel`, `third_party/acpica`) | ACPICA **20260408**, the acpica/acpica release archive, pinned by SHA-256 (`third_party/acpica/upstream.lock`, `kernel/upstream.lock`). ACPICA carries Intel's dual licence; NeoDarwin uses it under the BSD-3-Clause terms (section 3 of each file header). Not modified |
| `third_party/acpica/acpica.BUILD` | `kernel_srcs`: the OS-independent core, 163 C files (dispatcher, events, executer, hardware, namespace, parser, resources, tables, utilities) and the headers. No debugger, disassembler, compiler or tools, and no `rsdump.c`, which is debugger code |
| `kernel/neodarwin/acpi/acnd.h` | ACPICA's host configuration (below) |
| `kernel/neodarwin/acpi/nd_acpi_osl.c` | ACPICA's OS services layer, `AcpiOs*` on XNU |
| `kernel/neodarwin/acpi/NeoDarwinACPIPlatform.{h,cpp}` | the ACPI platform: starts ACPICA, walks the namespace, publishes the nubs |
| `kernel/neodarwin/acpi/IOACPIPlatformDevice.cpp`, `include/IOKit/acpi/*.h` | the nub class and its interface headers |
| `kernel/patches/0018-iokit-build-acpica-and-acpi-platform.patch` | lists ACPICA and the three NeoDarwin files in `iokit/conf/files.arm64`, with per-object flags |

`kernel/BUILD.bazel` overlays the NeoDarwin files at `iokit/ndacpi` and ACPICA at `iokit/ndacpi/acpica`. Like the platform expert (patch 0009) and HFS+ (patch 0015), everything is compiled into the kernel, because `kcgen` links no kexts until M5. `NeoDarwinPlatformExpert::start` starts the ACPI platform once IOKit is up (`nd_acpi_platform_start`), and it runs on a kernel thread of its own, so AML evaluation never holds up the platform expert.

## ACPICA's configuration: `acnd.h`

ACPICA chooses its host header in `platform/acenv.h` from compiler macros. clang defines `__APPLE__`, which selects `acmacosx.h` and, through it, `aclinux.h`: Linux user space. Rather than patch ACPICA, every ACPICA object is compiled with `-include acnd.h` (patch 0018), and NeoDarwin's files include it through `nd_acpica.h`. It claims the include guards of `acmacosx.h` and `acgccex.h`, so neither is read, and it sets:

| Setting | Why |
|---|---|
| `ACPI_MACHINE_WIDTH 64`, `long long` 64-bit types, `ACPI_CPU_FLAGS` = `unsigned long` | LP64; the flags hold an `IOInterruptState` |
| `ACPI_REDUCED_HARDWARE 1` | every SBSA/SBBR machine is hardware-reduced ACPI: no SCI, PM1 registers, GPE blocks, fixed events, FACS or global lock. ACPICA compiles all of that out, and `AcpiEnableSubsystem` installs no SCI handler |
| `ACPI_USE_LOCAL_CACHE` | ACPICA's own object caches (`utcache.c`); no OS cache interface to write |
| ACPICA's C library, renamed | ACPICA's `utclib.c` and `utprint.c` (its `memcpy`, `strcpy`, `vsnprintf` and so on) are built, under `AcpiNd*` names (`ND_ACPICA_CORE`, set only for ACPICA's objects). The kernel's `string.h` is fortified and marks `strcpy` and `strcat` deprecated or unavailable, and its `printf` ignores the precision in ACPICA's `%8.8X` formats |
| no `ACPI_DEBUG_OUTPUT`, `ACPI_DEBUGGER`, `ACPI_DISASSEMBLER` | release kernel. Errors, warnings and the table list are still printed |

ACPICA's objects build with `-w`: upstream warnings are not NeoDarwin's to fix (the HFS precedent).

## OS services layer

| ACPICA interface | NeoDarwin (`nd_acpi_osl.c`) |
|---|---|
| `AcpiOsGetRootPointer` | `/chosen` `acpi-rsdp`: neoboot's copy of the RSDP (`dt-abi.md`) |
| `AcpiOsMapMemory` / `UnmapMemory` | DRAM (`/chosen` `dram-base`, `dram-size`), which includes the ACPI copy below `topOfKernelData`: the kernel's physmap, `ml_static_ptovirt`, cacheable; unmapping is a no-op. Anything else is MMIO (an OperationRegion, a GAS register): `ml_io_map_unmappable(VM_WIMG_IO)`, device memory, kept on a list so that `AcpiOsUnmapMemory` can `ml_io_unmap` it |
| `AcpiOsReadMemory` / `WriteMemory` | map, one aligned volatile access of the given width, unmap |
| `AcpiOsAllocate` / `Free` | `IOMalloc` / `IOFree`, with the size in a 16-byte header, since `AcpiOsFree` has no size |
| `AcpiOsCreateLock` and friends | `IOSimpleLock`, taken with interrupts disabled (`IOSimpleLockLockDisableInterrupt`) |
| `AcpiOsCreateSemaphore` and friends (ACPICA's mutexes too) | an `IOLock`, a count and `IOLockSleep` / `IOLockSleepDeadline`; timeouts in ms, `ACPI_WAIT_FOREVER` without one |
| `AcpiOsGetThreadId` | `current_thread()` |
| `AcpiOsExecute`, `AcpiOsWaitEventsComplete` | a kernel thread per call (notify handlers are rare), counted so that the wait can drain them |
| `AcpiOsSleep`, `AcpiOsStall`, `AcpiOsGetTimer` | `IOSleep`, `IODelay`, `mach_absolute_time` in 100 ns units |
| `AcpiOsPrintf`, `AcpiOsVprintf` | formatted by ACPICA's own `vsnprintf`, collected into whole lines, then the kernel's `printf` |
| `AcpiOsReadPort` / `WritePort` | `AE_SUPPORT`: Arm has no I/O port space. An AML `SystemIO` access fails |
| `AcpiOsReadPciConfiguration` / `Write…` | ECAM (`nd_pci.h`, `pci.md`): the platform registers MCFG's windows after `AcpiLoadTables`, before any AML runs. 64-bit accesses are two 32-bit ones; a misaligned read is assembled from bytes, a misaligned write refused |
| `AcpiOsInstallInterruptHandler` | `AE_SUPPORT`: there is no SCI. Device interrupts, the GED's among them, go through IOKit and the GIC |
| `AcpiOsSignal` | a fatal AML `Fatal` op is logged; breakpoints are ignored |
| table overrides, `AcpiOsEnterSleep`, `AcpiOsRedirectOutput` | none; `AE_OK` |

## Start-up and the namespace walk

`NeoDarwinACPIPlatform::start`: `AcpiInitializeSubsystem`, `AcpiInitializeTables`, `AcpiLoadTables`, `AcpiEnableSubsystem(ACPI_FULL_INITIALIZATION)` (hardware-reduced: no mode switch, no SCI), `AcpiInitializeObjects` (`_INI`, `_REG`). Then it walks the devices under `\_SB` (`AcpiWalkNamespace`, which releases the namespace mutex around each callback, so the callback can evaluate objects):
- `_STA` is read; a device without one is present. Present: published. Absent but functioning: left out, its children walked. Absent and not functioning: its subtree skipped (ACPI 6.5 §6.3.7).
- When `_STA` itself fails (AML touching an address space nothing handles, or a dependency that isn't there yet), the device is logged, left out, and its children still walked. One bad device must not hide a subtree; the bring-up board's 132 KB DSDT (SC8280XP) has `_DEP` chains on its PEP device. ACPICA ignores `_DEP`; nothing orders devices by it yet.
- Every published device becomes an `IOACPIPlatformDevice` in **IOACPIPlane**, under the nub of its nearest ancestor device (the ACPI platform at the top), and `AcpiAttachData` ties the namespace node to it.
- A device with a `_HID` or `_CID` is also attached in the service plane (under the nearest ancestor nub that is there, else the ACPI platform) and registered, so drivers can match it. A device named only by `_ADR` (the PCI slot devices `S00`, `S08`, `S10` under QEMU's `PCI0`) stays in IOACPIPlane, for the PCI bus driver to pair with the device it finds.

### Nub properties

| Property | Value |
|---|---|
| registry name, location | the four-character ACPI name without padding (`PCI0`, `COM0`); `_UID`, else `_ADR` in hex: `PCI0@0` |
| `name`, `compatible` | `_HID`, then `_HID` and each `_CID`, NUL-separated, as in a device tree. `IONameMatch` compares against the registry name, `_HID` and each `_CID` (`compareName`) |
| `_HID`, `_CID` (array), `_UID` (string), `_ADR` (64-bit), `_STA` | as evaluated (`gIOACPIHardwareIDKey` and friends) |
| `acpi-path` | the full path, `\_SB.PCI0` |
| `IODeviceMemory` | `_CRS` memory descriptors (Memory32, FixedMemory32, and Word/DWord/QWord/Extended address descriptors of memory type) as CPU physical addresses: minimum plus translation offset |
| `interrupts` | u32 per `_CRS` `Interrupt()` descriptor whose resource source is empty, that is, on the GIC. A GSIV is the GIC INTID (SPIs are GSIVs 32 and up), which is the one-cell specifier `NeoDarwinGICv3` takes |
| `IOInterruptSpecifiers`, `IOInterruptControllers` | two cells per interrupt, the INTID and its `acpi-interrupt-flags` value, each naming the GIC's controller (`IODTInterruptControllerName` of `/arm-io/gic`), so `registerInterrupt(index, …)` on the nub reaches the GIC, which configures the SPI's trigger from the flags (`pci.md`) |
| `interrupt-parent` | the GIC's `AAPL,phandle` |
| `acpi-interrupt-flags` | u32 per interrupt: bit 0 edge-triggered, bit 1 active low, bit 2 shared, bit 3 wake-capable. `NeoDarwinGICv3` programs `GICD_ICFGR` from bit 0 (checkpoint 2); the GED's is edge |
| `acpi-bus-range` | host bridges only: (u32 first, u32 last) from the producer bus-number descriptor |
| `acpi-windows` | host bridges only: `{type = memory, prefetchable or io; base; length; translation}` for each producer address descriptor, bus-side base and the offset to CPU addresses |

A host bridge's producer descriptors are windows, not registers. Elsewhere the producer/consumer bit is not trusted: QEMU marks `PCI0.RES0`'s ECAM (a PNP0C02 motherboard resource) as a producer, and Linux ignores the bit for such devices too.

`GPIO` and serial-bus descriptors, and interrupts on another controller (an `Interrupt()` with a resource source), are noted in the log line and left to the drivers of those controllers.

### The interface (`IOKit/acpi/IOACPIPlatformDevice.h`)

Class name, method names and signatures are those of Apple's Intel-era header in Kernel.framework, so source written against it compiles. It is a subset, written from the published interface: `evaluateObject` and `evaluateInteger` (32- and 64-bit, by name or `OSSymbol`; arguments and results convert `OSNumber`/`OSBoolean` ↔ Integer, `OSString` ↔ String, `OSData` ↔ Buffer, `OSArray` ↔ Package, and a reference inside a package, such as a `_PRT` entry's link device, becomes its path as an `OSString`), `validateObject`, `getACPITableData` (a copy of the table, owned by the platform), `getDeviceHandle`, `getDeviceStatus`, `getDeviceType`/`setDeviceType`, and `acquireGlobalLock`/`releaseGlobalLock`, which succeed at once with a token of 0: hardware-reduced ACPI has no global lock, so nothing can contend for it. Power management, fixed events, GPEs, address-space handlers and the I/O-port helpers are not declared. The vtable is NeoDarwin's, so binary Intel kexts don't load. `gIOACPIPlane` and the `_HID`/`_UID`/`_ADR`/`_STA` key symbols are defined. On a host bridge (a nub with `_PRT`) it also answers the platform functions IOPCIFamily sends to route legacy interrupts, "ResolvePCIInterrupt" and "SetDeviceInterrupts" (checkpoint 2, `pci.md`), which Apple's closed ACPI platform answered on Intel.

## The boot log

```
ACPI: RSDP 0x0000000041D78000 000024 (v02 BOCHS )
ACPI: XSDT 0x0000000041D78030 000064 (v01 BOCHS  BXPC     00000001      01000013)
…one line per table (ACPICA)…
ACPI: 1 ACPI AML tables successfully acquired and loaded
NeoDarwinACPIPlatform: \_SB.C000 ACPI0007 uid 0
NeoDarwinACPIPlatform: \_SB.COM0 ARMH0011 uid 0 mem 0x9000000+0x1000 irq 33
…
NeoDarwinACPIPlatform: \_SB.PCI0: PCI segment 0, bus 0, ECAM 0x4010000000 (_CBA); _PRT 128 routes; MCFG 60 bytes
NeoDarwinACPIPlatform: ACPICA 20260408; 9 tables (FACP DSDT APIC PPTT GTDT MCFG SPCR DBG2 IORT); 44 devices published, 3 more in IOACPIPlane; 127 ms
```

One line per published device; boot-arg `ndacpi_verbose=0` turns them off (the summary and host-bridge lines stay). For each PCI host bridge the platform reads `_SEG`, `_BBN`, `_CBA` (else the MCFG entry for that segment and bus) and `_PRT` through the nub's own `evaluateInteger`, `evaluateObject` and `getACPITableData`, which exercises that interface on every boot.

### Measured on QEMU 11.1 `virt` (`neoverse-n2`, TCG)

One CPU: 44 devices published (47 with `-smp 4`: one `ACPI0007` per CPU), 3 more in IOACPIPlane only, in 115–180 ms of TCG time. Identical on `virt` and on `virt,secure=on` with TF-A.

| Path | `_HID` (`_CID`) | Resources |
|---|---|---|
| `\_SB.C000`… | `ACPI0007` (processor device) | — |
| `\_SB.COM0` | `ARMH0011` (PL011) | mem 0x9000000+0x1000, irq 33 |
| `\_SB.FWCF` | `QEMU0002` (fw_cfg; `_STA` 0x0B) | mem 0x9020000+0x18 |
| `\_SB.VR00`–`VR31` | `LNRO0005` (virtio-mmio), `_UID` 0–31 | mem 0xa000000+0x200 × n, irq 48–79 |
| `\_SB.L000`–`L003` | `PNP0C0F` (PCI interrupt links) | irq 35–38 |
| `\_SB.PCI0` | `PNP0A08` (`PNP0A03`) | bus 0–255; windows: memory 0x10000000+0x2eff0000, I/O 0x0+0x10000 at CPU 0x3eff0000, memory 0x8000000000+0x8000000000 |
| `\_SB.PCI0.RES0` | `PNP0C02` (motherboard resources) | mem 0x4010000000+0x10000000: ECAM |
| `\_SB.GED` | `ACPI0013` (Generic Event Device) | irq 41, edge |
| `\_SB.PWRB` | `PNP0C0C` (power button) | — |
| `\_SB.GEDD` | `PNP0C33` (error device) | — |
| `\_SB.PCI0.S00`, `S08`, `S10` | `_ADR` only (0, 0x10000, 0x20000) | IOACPIPlane only |

QEMU's `virt` DSDT has no PL031 RTC (`ARMH0031`) or PL061 GPIO (`ARMH0061`) devices; its RTC and GPIO keys are in the device tree only. `sbsa-ref` has them in its SbsaQemu ASL.

`//kernel:sbsa_boot_test` (`virt`) asserts the summary and the lines for `PCI0` (with its windows), the host-bridge line, `COM0`, `VR31` and `GED`; `//kernel:sbsa_secure_boot_test` (`virt,secure=on`, TF-A) asserts the summary and the host-bridge line. **`sbsa-ref`** is not in the matrix yet (`qemu-secure.md`, alternatives): it needs TF-A `qemu_sbsa` and edk2-platforms' SbsaQemu, and RAM at 1 TiB, which neoboot and the kernel haven't been tried on. P1-09's exit names it, so it stays open.

`ioreg` isn't in the images yet (IOKitTools needs IOKit.framework in userland), so the tests read the kernel log. The nubs are in the registry for `ioreg -p IOACPIPlane` and `ioreg -c IOACPIPlatformDevice` once it is.

## What the port found

| Finding | Fix |
|---|---|
| clang's `__APPLE__` makes ACPICA read `aclinux.h`, its Linux user-space host | `acnd.h`, force-included, claims `acmacosx.h`'s include guard; ACPICA is unmodified |
| `acgccex.h`, read last by every ACPICA file, does `#undef strchr` (a workaround for an old gcc macro). That undid the renaming, and `utclib.c` then defined the kernel's `strchr`: duplicate symbol at link | `acnd.h` claims `acgccex.h`'s guard too; it contains nothing else |
| `rsdump.c` doesn't compile without `ACPI_DEBUGGER` (it refers to the dump tables `rsdumpinfo.c` builds only with debug output) | left out, as Linux does |
| `ml_io_map` maps with `KMA_PERMANENT` in the I/O submap, and `ml_io_unmap` frees from `kernel_map`: the pair can't be used together | ACPICA's MMIO mappings use `ml_io_map_unmappable`, which `ml_io_unmap` undoes |
| QEMU marks the ECAM in `PCI0.RES0` (PNP0C02) as a producer | the producer bit is read on host bridges only |
| On one CPU the ACPI thread may run before or after BSD mounts its root, depending on the scheduler; either way within about 200 ms | nothing depends on it yet. Once a disk hangs off PCI, `IOFindBSDRoot` waits for the root device, and the nubs are published by then |
| `ACPI0007` processor devices are published, one per CPU | harmless; the CPUs themselves come from the device tree (MADT), and nothing matches `ACPI0007` |

## For checkpoint 2 (IOPCIFamily over ECAM)

Done (`pci.md`): each point below is implemented there, except `_PRT` on bridges and MSI, which is checkpoint 3.

- **Host bridge nub.** Match `IONameMatch` `PNP0A08` (and `PNP0A03`) on `IOACPIPlatformDevice`. The nub carries `acpi-bus-range` and `acpi-windows`, and `describeHostBridge` shows how to read `_SEG`, `_BBN`, `_CBA` and MCFG through it. Real boards (the SC8280XP bring-up board has seven MCFG segments, ECAM above 4 GiB) need `_SEG` matched to MCFG entries; `_CBA` is optional (QEMU has it, many firmwares don't).
- **Config space for AML.** `AcpiOsReadPciConfiguration`/`Write…` return `AE_SUPPORT`; with ECAM mapped they should do real accesses (segment, bus, device, function, register), since `_OSC`, `_DSM` and `PCI_Config` regions may touch config space. Implement them in `nd_acpi_osl.c` against the ECAM mapping checkpoint 2 creates.
- **`_OSC`.** Evaluate `\_SB.PCI0._OSC` (PCIe native hot-plug, AER, PME, LTR control) before taking over those features.
- **Interrupt routing.** `evaluateObject("_PRT")` returns an `OSArray` of `{address, pin, source, source-index}`; the source is a link device's path (`\_SB.L000`), whose nub's `interrupts` holds the GSIV (QEMU: INTA–INTD on 35–38, swizzled by slot). A zero source means the index is the GSIV. The GIC driver must honour `acpi-interrupt-flags` (edge vs level) before edge interrupts like the GED's are enabled.
- **Slots in IOACPIPlane.** The `_ADR`-only children of the bridge (`S00`, `S08`, `S10`) are waiting in IOACPIPlane: `_ADR` is (device << 16) | function. Pairing each `IOPCIDevice` with its ACPI node gives it `_DSM`, `_PRW` and `_SUN`.
- **MSI.** IORT maps the root complex's requester IDs to the ITS (`getACPITableData("IORT")`); the ITS itself comes from the MADT's GIC ITS entries, which neither neoboot nor the kernel reads yet.
- **Port I/O.** The bridge's I/O window is MMIO at CPU address 0x3eff0000 on QEMU (translation offset): IOPCIFamily must map I/O BARs through it rather than use port instructions.
