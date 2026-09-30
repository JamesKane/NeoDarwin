<!-- SPDX-License-Identifier: BSD-2-Clause -->
# PCI: IOPCIFamily over ECAM (P1-09)

**P1-09, checkpoint 2.** The SBSA kernel enumerates PCI Express with Apple's open **IOPCIFamily** on each ACPI PCI host bridge that checkpoint 1 publishes (`acpi.md`). A NeoDarwin host bridge reaches configuration space through ECAM, gives IOPCIFamily the bus range and windows from `_CRS`, and negotiates `_OSC`. The ACPI nub routes legacy interrupts through `_PRT`, and the GIC configures each SPI as it is registered. Every `IOPCIDevice` is logged with its IDs, BARs and INTx GSIV, and QEMU's `edu` device proves that INTx reaches a handler.

Checkpoint 3 adds MSI and MSI-X through the GIC ITS (`gic-its.md`); P1-10 adds the virtio-blk and NVMe drivers that match these nubs. What they need is at the end and in `gic-its.md`.

## Pieces

| Where | What |
|---|---|
| `@apple_iopcifamily` (`MODULE.bazel`) | IOPCIFamily **726.0.5**, from the macOS 26.0 release set (distribution-macOS `macos-260`), pinned by SHA-256 (`kernel/upstream.lock`). APSL 2.0 (`THIRD_PARTY_NOTICES.md`). The seven sources that build for arm64 and the `IOKit/pci` headers |
| `kernel/neodarwin/pci/nd_pci.h`, `nd_pci_ecam.c` | the ECAM registry and configuration access, by segment; C, shared by ACPICA's OS layer and the host bridge |
| `kernel/neodarwin/pci/NeoDarwinPCIHostBridge.{h,cpp}` | the `IOPCIHostBridge` for PNP0A08/PNP0A03 nubs |
| `kernel/neodarwin/pci/NeoDarwinPCIEduTest.cpp` | a driver for QEMU's `edu` device (1234:11e8) that raises INTA and waits for its handler, then the same through its MSI (checkpoint 3) |
| `kernel/neodarwin/pci/NeoDarwinPCIMSI.{h,cpp}`, `nd_iort.{h,c}` | checkpoint 3: the ITS-backed messaged interrupt controller and the MADT/IORT parser (`gic-its.md`); its NVMe MSI-X proof, `NeoDarwinPCINVMeTest.cpp`, was retired in P1-10 for the NVMe driver (`storage.md`) |
| `kernel/neodarwin/pci/compat` | `os/availability.h` and `IOKit/dart/IODARTKeys.h`, which the kext gets from the SDK and from Apple's closed DART driver |
| `kernel/neodarwin/acpi` | MCFG into the ECAM registry, AML configuration access (`nd_acpi_osl.c`), `_PRT` routing (`IOACPIPlatformDevice::callPlatformFunction`) |
| `kernel/neodarwin/platform/NeoDarwinGICv3.cpp` | SPI group, priority, trigger and routing; shared level SPIs |
| `kernel/patches/0022-iokit-build-iopcifamily-and-pci-host-bridge.patch` | lists IOPCIFamily and NeoDarwin's PCI files in `iokit/conf/files.arm64`, their search paths, and the built-in personalities |
| `kernel/patches/0023-iopcifamily-arm64-fixes-and-64-bit-bars.patch` | two compile errors in IOPCIFamily, and 64-bit BARs above 4 GiB on arm64 |
| `kernel/patches/0024-iokit-build-gic-its-and-pci-msi.patch` | checkpoint 3's files and the NVMe test's personality (the test driver retired by patch 0029) |

`kernel/BUILD.bazel` overlays NeoDarwin's files at `iokit/ndpci` and IOPCIFamily at `iokit/ndpci/IOPCIFamily`. As with ACPICA and HFS+, everything is compiled into the kernel, because `kcgen` links no kexts until M5.

## What of IOPCIFamily builds, and what NeoDarwin replaces

IOPCIFamily is a kext, and only its generic half is open. Its platform half is closed: the Intel Macs' `AppleACPIPCI` host bridge and ACPI platform, and Apple silicon's per-SoC PCIe drivers. With `ACPI_SUPPORT` 0 (`IOPCIPrivate.h`: it is 1 only on Intel), IOPCIFamily builds for arm64 as it does for Apple silicon Macs.

| IOPCIFamily source | On NeoDarwin |
|---|---|
| `IOPCIBridge.cpp`: `IOPCIBridge`, `IOPCI2PCIBridge`, `IOPCIHostBridge`, `IOPCIHostBridgeData`, `IOPCIEventSource` | built. `IOPCI2PCIBridge` drives root ports and switches (personality: `IOPCIClassMatch` 0x0604) |
| `IOPCIConfigurator.cpp` | built: enumeration, bus numbers, BAR and window allocation, one configurator per host bridge |
| `IOPCIDevice.cpp`, `IOPCIDeviceMappedIO.cpp` | built: the nub drivers use; `IOPCIDeviceMappedIO` is the memory-mapped I/O-space path non-Intel machines use |
| `IOPCIMessagedInterruptController.cpp` | built; subclassed by `NeoDarwinPCIMessagedInterruptController` (checkpoint 3) |
| `IOPCIRange.cpp`, `IOPCITraceEventBuffer.cpp` | built |
| `AppleVTD.cpp`, `IOPCIDeviceI386.cpp`, `IOPCIBridgeLegacy.cpp` | not built: Intel-only (VT-d, port instructions, the shared x86 host bridge data) |
| `PCIDriverKit/*.iig`, `IOPCIDevice.iig` | not built: DriverKit's user-space interface, absent until there is a DriverKit |
| `AppleACPIPCI` (closed) | **`NeoDarwinPCIHostBridge`** |
| ACPI `_PRT` routing, the "ResolvePCIInterrupt" and "SetDeviceInterrupts" platform functions (closed `AppleACPIPlatform` on Intel) | **`IOACPIPlatformDevice::callPlatformFunction`** |
| MSI controller (closed on both) | **`NeoDarwinPCIMessagedInterruptController`** over the GIC ITS (`gic-its.md`) |

The kext's Info.plist personalities become entries in `gIOKernelConfigTables` (patch 0022): `NeoDarwinPCIHostBridge` on `IOACPIPlatformDevice` with `IONameMatch` PNP0A08 or PNP0A03; `IOPCI2PCIBridge` on `IOPCIDevice` with `IOPCIClassMatch` `0x06040000&0xffff0000`; `NeoDarwinPCIEduTest` with `IOPCIMatch` `0x11e81234`.

What IOPCIFamily needed to compile in the kernel (patches 0022 and 0023):

| Finding | Fix |
|---|---|
| `TargetConditionals.h` and `os/availability.h` are SDK user-space headers, not on the kernel's search path | the compat `TargetConditionals.h` of patch 0010; a compat `os/availability.h` whose `API_*` attributes expand to nothing |
| `IOKit/dart/IODARTKeys.h` comes with the closed DART driver | a compat header with the two platform-function names IOPCIFamily sends only to an `IODARTMapper`, which never exists here |
| `isDescendant()` returns `NULL` from a `bool` function; `restartPowerAssertionTimer()` returns a value from a `void` one: errors with xnu's C++ flags | patch 0023 |
| `PCIDriverKit/PCIDriverKitPrivate.h` is included unconditionally | it is in the archive; the overlay carries it |

## The host bridge

`NeoDarwinPCIHostBridge` matches a host bridge nub and, in `probe`, before `IOPCIHostBridge::probe` creates the configurator:

- **Segment and buses.** `_SEG` (default 0), `_BBN`, and the bus range from `_CRS`'s producer bus-number descriptor (`acpi-bus-range`); a range past 255 is clamped (the Q8B's descriptor has a length of 512).
- **ECAM.** `_CBA` if the bridge has one, else the MCFG allocation for its segment and `_BBN`, whose bus range then bounds the bridge's. Both give the address of bus 0 of the segment. ECAM above 4 GiB is the normal case (QEMU: 0x40_1000_0000; the Q8B: 0x4_0000_0000 to 0x7_0000_0000).
- **`_OSC`** (PCI Firmware 3.3 §4.5): a query, then a request for what the query granted, as Linux and FreeBSD do. Support: extended configuration space, ASPM, clock PM, segments, and MSI when the ITS is up (checkpoint 3; the host bridge sets up the MSI controller first, and the log line says `with MSI`). Control requested: native hot plug, PME, AER, PCIe capability structure, LTR. A missing or failing `_OSC` grants nothing. IOPCIFamily reads its flags (`gIOPCIFlags`) when the configurator is created, and they are global: AER stays enabled only if every host bridge granted it. The grant is `acpi-osc-control` on the nub. QEMU grants 0x1d (everything but LTR).

Then `start` adds the windows from `acpi-windows` and hands the bridge to IOPCIFamily (`IOPCIBridge::start` → configurator → `probeBus`):

| Window | IOPCIFamily |
|---|---|
| memory, translation 0 | `addBridgeMemoryRange`: below or above 4 GiB |
| prefetchable, translation 0 | `addBridgePrefetchableMemoryRange` |
| memory with a translation offset | not used, and logged: IOPCIFamily takes a memory BAR's bus address for its CPU address. Neither QEMU nor the Q8B translates memory |
| I/O | `addBridgeIORange` for the ports, from 0x1000 up (Linux's `PCIBIOS_MIN_IO` on arm64; IOPCIFamily takes a BAR at port 0 for an unassigned one, and EDK2 on QEMU hands out port 0). `ioDeviceMemory()` is the window as memory at its translation offset, from port 0: QEMU's is at CPU 0x3eff0000. IOPCIFamily maps an I/O BAR as a sub-range of it at the port number, so drivers reach I/O BARs as MMIO, never through port instructions |

**BARs.** The configurator probes every BAR, bridge window and bus number the firmware left, keeps those that fit the windows, and assigns the rest; UEFI has assigned everything on QEMU. Upstream, a non-Intel configurator refuses 64-bit placement (`kIOPCIConfiguratorPFM64` cleared for every host bridge): every BAR must lie below 4 GiB, so EDK2's 64-bit BARs in QEMU's 512 GiB window at 0x80_0000_0000 were moved into the 32-bit window, and the Q8B, whose windows are all above 4 GiB, could not have placed any. Patch 0023 keeps the flag when the host bridge decodes memory above 4 GiB: EDK2's placements now stand (the NVMe BAR at 0x80_0010_4000, virtio BAR4s in the same window). A 32-bit-only BAR still needs a window below 4 GiB, which the Q8B does not have.

**Configuration space** (`nd_pci.h`). One registry of ECAM windows, keyed by segment and bus range. The ACPI platform registers every MCFG allocation between `AcpiLoadTables` and the first AML (`_INI`, `_REG`), so `PCI_Config` operation regions, `_OSC` and `_DSM` reach the hardware; a host bridge adds its `_CBA` window (one that overlaps a registered window of its segment is ignored, with a message if the bases differ). A bus is mapped the first time it is touched, 1 MiB of device memory (nGnRnE) through `ml_io_map_unmappable`, in `kernel_map`: `ml_io_map`'s I/O submap has 8 MiB in all. Accesses are 1, 2 or 4 bytes, naturally aligned, one access of that width; anything else fails and reads all ones, as does a bus not yet mapped when the caller is in interrupt context. Writes end with a `DSB`. The host bridge's `configRead`/`configWrite` pass IOPCIFamily's register (the low byte in `offset`, bits 8–11 in the address space's `registerNumExtended`) to it, with the bridge's segment: IOPCIFamily's address space has no segment, but each host bridge has its own configurator.

**AML's configuration accesses** (`AcpiOsReadPciConfiguration`, `Write…`): through the same registry. A 64-bit access is two 32-bit ones; a misaligned read is assembled from single bytes; a misaligned write is refused (`AE_BAD_PARAMETER`), since registers may have side effects.

## Legacy interrupts (INTx)

IOPCIFamily resolves a device's INTx lazily, when something first reads its `IOInterruptSpecifiers` (a driver's `registerInterrupt` or `getInterruptType`). The host bridge's log no longer does (checkpoint 3): it asks `_PRT` the same way for the GSIV it prints, so that a driver can still choose its MSI-X vectors (`gic-its.md`). `IOPCIBridge::resolveLegacyInterrupts` sends "ResolvePCIInterrupt" (the bridge's provider, the device number, the pin) up the provider chain; for every device it arrives at the host bridge's ACPI nub:

1. **Swizzle.** When the request started at a PCI-to-PCI bridge's `IOPCIDevice` (a root port), the pin becomes (pin + device) mod 4 and the device the bridge's own (from its `reg`), up to the root bus (PCI-to-PCI Bridge 1.2 §9.1). `_PRT` on bridges is not consulted; neither QEMU nor the Q8B has one.
2. **`_PRT`** (ACPI 6.5 §6.2.13): the entry for (device, pin). A source of 0 means the index is the GSIV (the Q8B: 0x241–0x244); a source path is a link device (QEMU: `\_SB.L000`–`L003`, PNP0C0F), whose nub's `interrupts` (its `_CRS`) holds the GSIV. Link devices are not reprogrammed (`_SRS`): hardware-reduced Arm firmware fixes them.
3. **"SetDeviceInterrupts"** makes the GSIV the device's specifier 0 on the GIC: `{GSIV, flags}` with the shared bit, level-triggered, as PCI INTx is. MSI or MSI-X vectors follow it at index 1 and up (checkpoint 3), as IOPCIFamily expects.

**The GIC.** `NeoDarwinGICv3` used to leave SPIs as reset: Group 0 on a GIC with one security state (DS = 1), which is this kernel's FIQ, the timer's, and never programmed trigger, priority or routing. Now `initVector`, on an SPI's first registration, disables it, waits for `GICD_CTLR.RWP`, and sets:

| Register | Value |
|---|---|
| `GICD_IGROUPR` | Group 1. With DS = 0 the register is RAZ/WI to Non-secure software and TF-A has made every SPI Group 1 Non-secure already |
| `GICD_IPRIORITYR` | 0x80, as for SGIs and PPIs |
| `GICD_ICFGR` | edge or level, from bit 0 of the specifier's second cell (ACPI flags, `acpi-interrupt-flags`); one-cell device-tree specifiers are level |
| `GICD_IROUTER` | the boot CPU's affinity, `IRM` = 0 |

It reads them back into the log (`NeoDarwinGICv3: SPI 35: Group 1, priority 0x80, level, routed to 0x0`; with DS = 0 the group reads as zero, and the line says the Secure firmware set it). The SPI stays disabled until the driver enables it. Level SPIs can be shared (`vectorCanBeShared`): QEMU's four INTx lines serve every slot, and a root port shares its line with the device below it; IOInterruptController puts an `IOSharedInterruptController` on the vector at the second registration. ACPI interrupt specifiers now carry two cells, `{GSIV, flags}`, on every nub (`acpi.md`), so edge interrupts such as the GED's will be configured as edge.

**Proof.** `NeoDarwinPCIEduTest` matches QEMU's `edu` device, registers a handler on INTA (source 0), enables it, writes the device's interrupt-raise register and waits up to a second: the handler acknowledges in the device and counts. INTA → `_PRT` → `L000` → GSIV 35 → SPI 35 → IRQ → handler. Since checkpoint 3 the time is measured from the raise to the handler (tens of µs of TCG time), and the same test follows through the device's MSI (`gic-its.md`):

```
NeoDarwinGICv3: SPI 35: Group 1, priority 0x80, level, routed to 0x0
NeoDarwinPCIEduTest: 00:04.0: edu 0x010000ed: INTA on GSIV 35 (level) reached its handler in 7 us: status 0x4e440000, 1 interrupt
```

## DMA coherence and the IORT

Each host bridge, and every `IOPCIDevice` below it, gets `dma-coherent` (a boolean) and, when IORT gives it, `dma-address-bits`. Coherence comes from `_CCA` on the host bridge, else from the IORT root complex node of its segment (cache coherency attribute), else it is taken as not coherent, as ACPI requires of Arm and Linux does. The IORT node's memory size limit is `dma-address-bits` (QEMU: 64; the Q8B: 36, so its PCIe masters reach only the first 64 GiB). The log also names where the root complex's requester IDs go: QEMU's go to the ITS group, the Q8B's to an SMMUv3. P1-10's drivers must honour both properties (`IODMACommand` cache maintenance when not coherent, and an address limit).

There is no IOMMU driver: the SMMUv3 is not programmed. On QEMU the root complex maps straight to the ITS, so DMA is untranslated. On the Q8B the IORT routes PCIe through an SMMUv3 that the firmware reserves; whether it is left in bypass is to be checked on the board.

## The boot log

With `virtio-blk-pci`, `nvme` and `edu` on the root bus, a `pcie-root-port` with a second `virtio-blk-pci` behind it and another with a second `edu` (QEMU 11.1 `virt`; the default NIC at 00:01 and the ESP's virtio disk at 00:07 are QEMU's own):

```
NeoDarwinPCIHostBridge: \_SB.PCI0: segment 0, buses 0-255, ECAM 0x4010000000 (_CBA); mem 0x10000000+0x2eff0000, io 0x1000+0xf000 at 0x3eff1000, mem 0x8000000000+0x8000000000
NeoDarwinPCIHostBridge: \_SB.PCI0: _OSC control 0x1d of 0x3d with MSI; DMA coherent (_CCA, 64 address bits); requester IDs to ITS group
[ PCI configuration begin ]
[ PCI configuration end, bridges 3, devices 8 ]
NeoDarwinPCIHostBridge: 0000:00:00.0 1b36:0008 class 060000
NeoDarwinPCIHostBridge: 0000:00:01.0 1af4:1000 class 020000 bar0 io 0x3eff1100+0x20 bar1 mem 0x10544000+0x1000 bar4 mem pf 0x800010c000+0x4000 INTA gsiv 36
NeoDarwinPCIHostBridge: 0000:00:02.0 1af4:1001 class 010000 bar0 io 0x3eff1000+0x80 bar1 mem 0x10543000+0x1000 bar4 mem pf 0x8000108000+0x4000 INTA gsiv 37
NeoDarwinPCIHostBridge: 0000:00:03.0 1b36:0010 class 010802 bar0 mem 0x8000104000+0x4000 INTA gsiv 38
NeoDarwinPCIHostBridge: 0000:00:04.0 1234:11e8 class 00ff00 bar0 mem 0x10400000+0x100000 INTA gsiv 35
NeoDarwinGICv3: SPI 35: Group 1, priority 0x80, level, routed to 0x0
NeoDarwinPCIEduTest: 00:04.0: edu 0x010000ed: INTA on GSIV 35 (level) reached its handler in 7 us: status 0x4e440000, 1 interrupt
NeoDarwinPCIHostBridge: 0000:00:05.0 1b36:000c class 060400 bridge to buses 1-1 bar0 mem 0x10542000+0x1000 INTA gsiv 36
NeoDarwinPCIHostBridge: 0000:00:06.0 1b36:000c class 060400 bridge to buses 2-2 bar0 mem 0x10541000+0x1000 INTA gsiv 37
NeoDarwinGICv3: SPI 37: Group 1, priority 0x80, level, routed to 0x0
NeoDarwinPCIHostBridge: 0000:00:07.0 1af4:1001 class 010000 bar0 io 0x3eff1080+0x80 bar1 mem 0x10540000+0x1000 bar4 mem pf 0x8000100000+0x4000 INTA gsiv 38
NeoDarwinPCIHostBridge: 0000:01:00.0 1af4:1042 class 010000 bar1 mem 0x10200000+0x1000 bar4 mem pf 0x8000000000+0x4000 INTA gsiv 36
NeoDarwinPCIHostBridge: 0000:02:00.0 1234:11e8 class 00ff00 bar0 mem 0x10000000+0x100000 INTA gsiv 37
NeoDarwinGICv3: SPI 37: shared for pci1234,11e8
NeoDarwinPCIEduTest: 02:00.0: edu 0x010000ed: INTA on GSIV 37 (level) reached its handler in 0 us: status 0x4e440000, 1 interrupt
NeoDarwinGICv3: SPI 36: Group 1, priority 0x80, level, routed to 0x0
NeoDarwinPCIHostBridge: \_SB.PCI0: segment 0: 10 devices (2 PCI-to-PCI bridges) on buses 0-2, 9 with INTx, 9 capable of MSI or MSI-X
```

That was checkpoint 2's log. Since checkpoint 3 each device line also names its MSI and MSI-X capabilities (`INTA gsiv 38 msi-x 65`, `INTA gsiv 35 msi 1`), and the MSI lines are in `gic-its.md`.

The second `edu` is behind the root port in slot 6: its INTA swizzles to the port's INTA, `L002`, GSIV 37, which the port's own driver (`IOPCI2PCIBridge`, for AER and hot plug) registered first, so the vector becomes shared. 64-bit BARs stay where EDK2 put them, above 4 GiB (patch 0023); I/O BARs below port 0x1000 were moved.

Devices are logged as IOPCIFamily publishes them, so their order varies. An I/O BAR is shown at its CPU address. The summary follows once the host bridge's subtree is quiet (`waitQuiet`). On a root bus virtio devices are transitional (1af4:1000 net, 1af4:1001 block); behind a PCIe root port they are modern (1af4:1041, 1af4:1042). Boot-arg `pci_log=0x80000200 pci_log_mode=2` prints IOPCIFamily's own enumeration log (every BAR as found, kept or moved).

Bus-0 devices that match an `_ADR`-only child of the bridge in IOACPIPlane (QEMU's `S08`, `S10`, …) get its `acpi-path`, for `_DSM`, `_PRW` and `_SUN`.

`ioreg` isn't in the images yet, so the tests read the log; the nubs are in the registry for `ioreg -c IOPCIDevice` once it is.

## Tests

| Target | Machine | Asserts |
|---|---|---|
| `//kernel:sbsa_pci_boot_test` | `virt` (GIC DS = 1), `neoverse-n2` | the two host bridge lines; 1af4:1001 with its I/O BAR as MMIO and 1b36:0010 with its 64-bit BAR above 4 GiB on the root bus; both root ports and their bus numbers; 1af4:1042 on bus 1; SPI 35's configuration; both edu interrupts, the second on a shared SPI; the summary; PID 1 |
| `//kernel:sbsa_secure_pci_boot_test` | `virt,secure=on` with TF-A (DS = 0) | the same, with the DS = 0 line for SPI 35 |
| `//kernel:sbsa_ref_pci_boot_test` | `sbsa-ref` with TF-A and SbsaQemu, four CPUs (`qemu-sbsa-ref.md`) | the host bridge (ECAM 0xf0000000, windows at 0x80000000 and 4 GiB); the machine's own e1000e and bochs-display (the GOP framebuffer's BAR kept at 0x80000000) and the same added devices from 00:03.0; INTx through `_PRT` link devices under `PCI0`; MSI and MSI-X through the ITS at 0x44081000 via the SMMUv3 in bypass; the summary |

Checkpoint 3 adds MSI lines to both and three more targets (`gic-its.md`, "Tests").

Both use the harness's new `--drive ID=SIZE|FILE` option (a blank sparse image or a copy of a file, as a raw block backend for `--device …,drive=ID`). `//kernel:sbsa_boot_test` and the rest still pass: QEMU's default NIC and the ESP disk are enumerated on every boot.

## Open

- **More than one host bridge per segment** (QEMU's `pxb-pcie`) is expected to work, since each bridge has its own configurator and bus range, but isn't tested.
- **Memory windows with a translation offset** are not used.
- **Hot plug, AER, PME.** IOPCIFamily's root-port driver enables AER when `_OSC` grants it (QEMU does) and handles native hot plug on slots marked hot-pluggable; neither has been exercised. A root port gets an MSI only when it is a hot-plug port without an INTx line, so QEMU's stay on INTx.
- **Power management.** IOPCIFamily's D-state and ASPM code runs as on a Mac; there is no system sleep yet.
- **A PCI framebuffer.** On Intel, IOPCIFamily moves the console with a relocated framebuffer BAR; on arm64 it doesn't. A GOP framebuffer in a BAR (virtio-gpu, bochs-display) that the configurator moves would lose the console. The Q8B's display isn't on PCIe, and QEMU's tests use `ramfb`.

## Checkpoint 3 (MSI and MSI-X through the ITS)

Done: `gic-its.md`. The host bridge answers "GetMessagedInterruptController" with an ITS-backed `IOPCIMessagedInterruptController`; the ITS comes from the MADT, LPIs are set up in `NeoDarwinGICv3`, DeviceIDs follow the IORT (through the Q8B's SMMUv3), and `_OSC` claims MSI.

## For P1-10 (virtio-blk and NVMe)

Both drivers follow this list (`storage.md`): virtio-blk (checkpoint 1) and NVMe (checkpoint 2).

- Match `IOPCIDevice` by `IOPCIMatch` (virtio 0x10011af4 and 0x10421af4; NVMe by `IOPCIClassMatch` 0x01080200). Map BARs with `mapDeviceMemoryWithRegister`; an I/O BAR (transitional virtio) maps as MMIO too.
- Interrupts: ask for MSI-X vectors with `configureInterrupts(kIOInterruptTypePCIMessagedX, …)` before anything else touches the device's interrupts (`gic-its.md`, "For P1-10"). Without MSIs (`nd_pci_msi=0`, no ITS) source 0 is INTx, level and possibly shared: use an `IOFilterInterruptEventSource` whose filter checks the device's own status.
- Honour `dma-coherent` and `dma-address-bits` (above).
- Disks behind root ports are on bus 1 and up; the host bridge's summary counts them.

## The Radxa Dragon Q8B

What its tables (`boot/neoboot/testdata/radxa-dragon-q8b.acpidump`) mean for this code:

- **Seven segments, seven host bridges** (`PCI0`–`PCI6`, `_SEG` 0–6, each with `_BBN` 0), MCFG windows at 0x7_0000_0000, 0x6_0000_0000, … 0x4_0000_0000, 256 buses each. Only `PCI0` and `PCI1` have `_CBA`; the rest use MCFG, which the ACPI platform registered before any AML ran. NVMe is on segment 2.
- **Windows only above 4 GiB**: `PCI0` decodes memory at 0x7_1000_0000 (256 MiB) and prefetchable memory at 0x7_2000_0000 (512 MiB), no I/O. Patch 0023 is what makes these usable; a 32-bit-only BAR cannot be placed.
- **`_CCA` 1** on the host bridges (the USB controllers' 0 doesn't concern PCIe), IORT CCA 1, 36 address bits, requester IDs to an SMMUv3.
- **`_PRT`**: direct GSIVs 0x241–0x244 on `PCI0`.
- **`_OSC`**: it reads a 12-byte capabilities buffer, which NeoDarwin passes (FreeBSD's `AE_AML_BUFFER_LIMIT` suggests a shorter one there). It never grants AER or LTR (control is masked with 0x15), and without MSI, clock PM and ASPM in the support field (0x16) not hot plug either. NeoDarwin claims all three since checkpoint 3 (support 0x1f), so its AML grants 0x15 of the 0x3d requested (hot plug, PME, PCIe capability structure); IOPCIFamily leaves AER off.
- **MSIs**: GIC-600's ITS at 0x17a40000, DeviceIDs through the SMMUv3 (`gic-its.md`, "What the Radxa Dragon Q8B will stress").
- **`_STA`** depends on `PRP0`, and each bridge has `_DEP` on the PEP device. Nothing orders devices by `_DEP` yet; if a bridge reports absent, it isn't published.
- **Risk:** the controllers are Synopsys DesignWare. If the firmware leaves a link down, an ECAM read of an absent device may raise an SError instead of reading all ones; FreeBSD's port is the reference for what the board needs.
