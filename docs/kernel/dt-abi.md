<!-- SPDX-License-Identifier: BSD-2-Clause -->
# DT-ABI v1: the device tree neoboot gives the kernel

**Version 1, P1-04.** This is the contract between the loader (`boot/neoboot`) and the SBSA kernel. neoboot writes it from the machine's ACPI tables and its own facts, in Apple's flattened format (`pexpert/pexpert/device_tree.h`, not FDT). The design is in `arm64-sbsa-bringup.md` §2.2.

The contract is enforced in three places, all built from the same sources in `boot/neoboot/Sources/Portable/`:
- `ACPI.swift` reads the tables and refuses ones the kernel can't run on (`ACPI.check`).
- `Platform.swift` writes the tree.
- `DTCheck.swift` checks a tree against the rules below. neoboot runs it on every tree it writes and doesn't boot one that fails. `//tools/dtdump` runs it on the tree it builds from an acpidump, and adds cross-checks against the tables.

A change to any node or property changes this document, the checker, and the dtdump golden files (`tools/dtdump/testdata`) together. A property the kernel doesn't read is either marked as reserved for a named consumer or left out of the tree.

Line numbers below refer to xnu-12377.1.9 as pinned (`@apple_xnu`) with NeoDarwin's patches, and to `kernel/neodarwin/platform`.

## Where the values come from

| Source | Read by | Gives |
|---|---|---|
| UEFI configuration table, `EFI_ACPI_20_TABLE_GUID` | `ACPIFirmware.swift` `Firmware.rsdp` | the RSDP. Without one neoboot stops: it has no built-in machine description to fall back on |
| RSDP (revision ≥ 2, both checksums) → XSDT | `ACPI.xsdtAddress`, `ACPI.xsdtTable` | the table list. There's no RSDT fallback: SBBR requires ACPI 6 |
| MADT (`APIC`): GICD (type 0x0C) | `ACPI.parseMADT` | distributor base and GIC version. Exactly one GICD. Version 3, or 0 (unspecified) |
| MADT: GICR (type 0x0E), else the GICC GICR base addresses | `ACPI.parseMADT` | the first redistributor frame, and the space reserved. Adjacent GICR ranges are merged. Without GICR structures, the GICC frames must be contiguous |
| MADT: GICC (type 0x0B) | `ACPI.parseMADT` | per CPU: MPIDR, flags (Enabled, Online Capable), performance-interrupt GSIV, processor UID |
| GTDT | `ACPI.parse` | the EL1 virtual timer's GSIV and flags. It must be 27 |
| SPCR | `ACPI.parse` | the console UART's interface type and base address, which must be in system-memory space |
| FADT (`FACP`), else the XSDT header | `ACPI.parse`, `ACPI.copyOEM` | OEM ID and OEM table ID for `model`; hardware-reduced flag; ARM boot flags (PSCI, HVC) |
| the loader | `Main.swift` | DRAM window, `CNTFRQ_EL0`, the boot CPU's `MPIDR_EL1`, the `CNTPCT` seed, UEFI `GetTime()`, ramdisk placement, and where the ACPI copy goes |

Every table is checked for length and checksum. The FACS has no checksum; it's dumped but never parsed. Tables neoboot doesn't parse (DSDT, MCFG, IORT, PPTT, DBG2, …) are still copied for the kernel.

neoboot refuses to boot, and prints why, when:
- the RSDP or any table is missing, truncated, or fails its checksum;
- there is no MADT, GTDT or SPCR;
- there is no GICD, more than one GICD, or no GICC;
- the GIC version is neither 3 nor 0. GICv4 redistributors are 256 KiB, and the kernel steps through them in 128 KiB `GICR_PE_SIZE` units;
- the GICR ranges are too small for one frame per GICC, or the GICC frames aren't contiguous;
- the virtual timer isn't INTID 27;
- a GICC's performance interrupt isn't a PPI;
- two enabled GICCs have the same MPIDR;
- the SPCR UART isn't a PL011 or an SBSA Generic UART (types 0x03, 0x0D, 0x0E). The 16550 family (0x00, 0x01, 0x12) waits for P1-12;
- the boot CPU isn't an enabled GICC;
- a device sits in physical page 0.

## The ACPI copy

The kernel's physmap covers only `[physBase, physBase+memSize)`, and firmware keeps ACPI tables in `EfiACPIReclaimMemory` outside that window. neoboot therefore copies the tables into loader memory below `topOfKernelData`, between the device tree and the ramdisk (`ACPI.relocate`):
- The copy holds the RSDP (36 bytes, with no RSDT), the XSDT, every XSDT table and the DSDT, each 16-byte aligned.
- The XSDT entries, the RSDP's `XsdtAddress` and the FADT's `X_DSDT`/`DSDT` are rewritten to point into the copy, and the checksums are recomputed.
- The FACS stays where it is. It belongs to the firmware, and hardware-reduced ACPI (every SBSA machine so far) has none.

neoboot parses the copy again before it boots, and dtdump does the same.

## Nodes and properties

Types: `u32` and `u64` are little-endian. `string` is NUL-terminated. `(u64,u64)` is an address and a length. Unless a row says otherwise, a value is written by `Platform.deviceTree`.

### `/`

| Property | Value | Source | Kernel reader |
|---|---|---|---|
| `name` | `"device-tree"` | constant | `pe_init.c:451,461` (`SecureDTFindEntry("name","device-tree")`) |
| `compatible` | `"NeoDarwin,sbsa"` | constant | `IOPlatformExpert.cpp:1623` `compareNames` against the `IONameMatch` of `NeoDarwinPlatformExpert` (patch 0009, `KernelConfigTables.cpp`) |
| `model` | `"<OEMID>,<OEM Table ID>"`, trailing spaces dropped; QEMU gives `"BOCHS,BXPC"` | FADT header, else XSDT header | `pe_init.c:461-469` (`gModelTypeBuffer`) |
| `target-type` | `"sbsa"` | constant | `pe_init.c:451-459` (`gTargetTypeBuffer`) |
| `#address-cells`, `#size-cells` | u32 2, 2 | constant | `IODeviceTreeSupport.cpp` `IODTGetCellCounts` for `/arm-io` `ranges` |
| `AAPL,phandle` | u32 1 | constant | `IODeviceTreeSupport.cpp:160` (phandle map) |

### `/chosen`

| Property | Value | Source | Kernel reader |
|---|---|---|---|
| `dram-base`, `dram-size` | u64 each: `physBase`, `memSize` | loader | `arm_init.c:545-559`: panics if either is absent |
| `debug-enabled` | u32 1 | constant | `pe_init.c:471-488` |
| `firmware-version` | `"neoboot-0.1"` | constant | `pe_init.c:489-496` (`iBoot version:` line) |
| `random-seed` | 64 bytes, xorshift64 of `CNTPCT` | loader | `pe_gen.c:177-178` (early PRNG) |
| `neodarwin,utc-seconds`, `neodarwin,utc-counter` | u64 each. Omitted when the firmware has no clock | UEFI `GetTime()`, `CNTVCT` | `NeoDarwinPlatformExpert.cpp:109-116` (`IORTC`, time of day) |
| `acpi-rsdp` | u64: physical address of the copied RSDP | ACPI copy | reserved: NeoDarwinACPIPlatform (Tier 2 ACPICA kext) |
| `acpi-tables` | (u64,u64): the ACPI copy | ACPI copy | reserved: as above |
| `AAPL,phandle` | u32 2 | constant | phandle map |

### `/chosen/memory-map`

| Property | Value | Source | Kernel reader |
|---|---|---|---|
| `RAMDisk` | (u64,u64), length a multiple of 16 KiB. Omitted without a ramdisk | loader | `IOKitBSDInit.cpp:766-826` → md0 |
| `ACPITables` | (u64,u64), equal to `/chosen acpi-tables` | ACPI copy | `IODTGetLoaderInfo` (`IODeviceTreeSupport.cpp`) for the Tier 2 kext. `libsa/bootstrap.cpp:392` walks the node but takes only `Driver-*` entries |

Every entry lies inside `[dram-base, dram-base+dram-size)`, below `topOfKernelData`.

### `/defaults`

| Property | Value | Source | Kernel reader |
|---|---|---|---|
| `serial-device` | u32: the UART's `AAPL,phandle` (3) | constant | `pe_serial.c:837`, resolved at `:921`. The node's `compatible` selects the driver at `:934` |

### `/cpus` and `/cpus/cpuN`

`/cpus` has `#address-cells` 1 and `#size-cells` 0. It holds one child per **enabled** MADT GICC, in MADT order, at most 32 (`MAX_CPUS`; `ml_parse_cpu_topology` asserts). The boot CPU is always listed. Online-capable CPUs that are not enabled are left out until hot-plug exists.

| Property | Value | Source | Kernel reader |
|---|---|---|---|
| `name` | `"cpu<index>"` | index | IORegistry naming only |
| `device_type` | `"cpu"` | constant | `AppleARMSMP.cpp:97` matching; `IOPlatformExpert.cpp:1681` cpu nubs |
| `reg` | u32: MPIDR Aff2:Aff1:Aff0 | GICC MPIDR | `machine_routines.c:1197` (`phys_id`, mandatory); `AppleARMSMP.cpp:98`; `IOPlatformExpert.cpp:1686` |
| `state` | `"running"` for the boot CPU, `"waiting"` for the rest | boot MPIDR | `machine_routines.c:1066` (`ml_is_boot_cpu`); `pe_identify_machine.c:64` (only the running CPU's timebase is read) |
| `timebase-frequency` | u32 `CNTFRQ_EL0` | loader | `pe_identify_machine.c:70-80`. Without it the kernel assumes 24 MHz: it never reads `CNTFRQ` |
| `interrupt-parent` | u32 5 (the GIC) | constant | `IODeviceTreeSupport.cpp:566` |
| `interrupts` | three u32: SGI 0 (IPI), the PMU PPI, SGI 1 (deferred IPI) | GICC performance GSIV; 23 (SBSA PPI 7) if the MADT gives 0 | `AppleARMSMP.cpp:125-150`: with three specifiers it registers entries 0 and 2 as IPIs and never enables entry 1. SGI numbers match `NeoDarwinGICv3.cpp` `ND_SGI_IPI`/`ND_SGI_DEFERRED_IPI` |
| `AAPL,phandle` | u32 16 + index | constant | phandle map |

**Secondary CPUs (until P1-06).** Every CPU node takes part in the topology whatever its `state` (`machine_routines.c:1160-1240`), and `AppleARMSMP::cpu_boot_thread` then starts each one through `IOPMGR::enableCPUCore`. On a multiprocessor neoboot therefore appends `cpus=1` to the command line when it names neither `cpus=` nor `cpumask=`. `ml_parse_cpu_topology` then keeps only the boot CPU (`machine_routines.c:1138, 1181-1186`), and `IODTPlatformExpert::createNubs` registers the other cpu nubs unused. That case is anticipated at `IOPlatformExpert.cpp:1693-1700`. The tree still describes the whole machine. Measured on QEMU `-smp 4` without the cap, the kernel panics with `Error registering IPIs @AppleARMSMP.cpp:138` when it gets to the second CPU. PSCI would fail next: `NeoDarwinPSCI` uses `smc`, and QEMU `virt` has no EL3.

### `/arm-io`

| Property | Value | Source | Kernel reader |
|---|---|---|---|
| `name` | `"arm-io"` | constant | `pe_identify_machine.c:170` |
| `device_type` | `"soc"` | constant | `pe_identify_machine.c:172` |
| `ranges` | three u64: child 0, parent `socBase`, size | MADT, SPCR | `pe_identify_machine.c:176-177`: `ranges[1]` is `gSocPhys`, the base every `reg` below is relative to. 0 means "no SoC" to `pe_identify_machine` (`:45`) and `serial_init` (`pe_serial.c:902-907`), so the timebase and the console would be lost |
| `#address-cells`, `#size-cells` | u32 2, 2 | constant | `IODTGetCellCounts`; `pe_serial.c` and `pe_fiq.c` assume u64 pairs |
| `AAPL,phandle` | u32 4 | constant | phandle map |

`socBase` is the lowest of the GICD, GICR and UART bases, rounded down to 64 KiB. `socSize` covers the highest end, rounded up to 64 KiB. Each device `reg` is therefore its ACPI physical address minus `socBase`, whatever the board's memory map. An identity mapping (parent 0) is impossible because of the zero test above. Nothing in the kernel maps the whole range: every user adds `gSocPhys` to one `reg` and maps only that (`pe_serial.c:726`, `pe_fiq.c:107,114`, `pe_identify_machine.c:209,220`, `NeoDarwinGICv3.cpp:70-71`).

### `/arm-io/gic`

| Property | Value | Source | Kernel reader |
|---|---|---|---|
| `compatible` | `"arm,gic-v3"` | constant | informational |
| `reg` | four u64: GICD offset, 64 KiB, GICR offset, frames × 128 KiB | MADT GICD, GICR/GICC | `pe_fiq.c:78-118`: panics without the node or with fewer than 32 bytes. `NeoDarwinGICv3.cpp:63-71` caps the GICR mapping at `MAX_CPUS` frames |
| `interrupt-controller` | `"gic"` | constant | `IODeviceTreeSupport.cpp:573,659` (ends the interrupt-parent walk) |
| `#interrupt-cells` | u32 1: a specifier is one INTID | constant | `IODeviceTreeSupport.cpp:609` |
| `#address-cells` | u32 0 | constant | interrupt-map resolution |
| `AAPL,phandle` | u32 5 | constant | `interrupt-parent` of every cpu node; `NeoDarwinGICv3.cpp:95` `IODTInterruptControllerName` |

**Redistributors.** There is one 128 KiB frame per MADT GICC, counted over every GICC and not only the listed CPUs. QEMU reserves 0xF60000 (123 frames) for any CPU count, and mapping all of that made a 16 MB `kmem_alloc` fail (`arm64-sbsa-bringup.md` §2.1.2). The kernel finds each CPU's frame by walking `GICR_TYPER` from the start of the range until the `Last` bit (`pe_fiq.c:47-68`, `NeoDarwinGICv3.cpp` `redistributorForCurrentCPU`), so the range must begin at the first frame and hold every CPU's frame. v1 describes one range. A second GICR range that isn't adjacent to the first (multi-socket) is ignored when the first is big enough, and refused when it isn't.

### `/arm-io/interrupt-controller` and `/arm-io/timer`

These are legacy lookups. Without them `pe_arm_map_interrupt_controller` fails and `ml_init_timebase` is never called (`pe_identify_machine.c:206-226, 238-243`).

| Node | Property | Value | Kernel reader |
|---|---|---|---|
| `interrupt-controller` | `interrupt-controller` | `"master"` | `pe_identify_machine.c:206` |
| | `reg` | (GICD offset, 64 KiB) | `pe_identify_machine.c:208-209` |
| | `AAPL,phandle` | u32 6 | phandle map |
| `timer` | `device_type` | `"timer"` | `pe_identify_machine.c:217` |
| | `reg` | (first GICR frame, 64 KiB): any small MMIO range serves | `pe_identify_machine.c:219-220` |
| | `AAPL,phandle` | u32 7 | phandle map |

### `/arm-io/uart0`

| Property | Value | Source | Kernel reader |
|---|---|---|---|
| `device_type` | `"serial"` | constant | informational |
| `compatible` | `"arm,pl011"` | SPCR type 0x03, 0x0D or 0x0E | `pe_serial.c:804-811, 934` |
| `reg` | (u64,u64): SPCR base − `socBase`, 4 KiB | SPCR | `pe_serial.c:719-726`, asserting exactly 16 bytes |
| `AAPL,phandle` | u32 3 | constant | `/defaults serial-device` |

The UART has no `interrupts`: the console is polled (`serial_keyboard_poll`). neoboot's own console switches to this UART at `ExitBootServices`.

## The checks (`DTCheck.check`)

A tree is DT-ABI v1 when:
1. **Structure.** It parses: no node or property overruns it, and the root node ends exactly at its length.
2. **Names and phandles.** Every node has a `name` string. Every `AAPL,phandle` is a u32 that is unique in the tree.
3. **`/`.** It has the name, compatible, model, target-type and cell counts listed above.
4. **`/chosen`.**
   - `dram-base` and `dram-size` are u64, the size is non-zero and the base is 16 KiB aligned.
   - `random-seed` is at least 64 bytes; `debug-enabled` is a u32; `firmware-version` is a string.
   - `acpi-rsdp` is non-zero and lies inside `acpi-tables`, which is (u64,u64).
   - `/chosen/memory-map` exists. Each entry is a non-empty (u64,u64) inside DRAM, `ACPITables` equals `acpi-tables`, and a `RAMDisk` length is a multiple of 16 KiB.
5. **`/arm-io`.**
   - `device_type` is `"soc"`; `ranges` is three u64 with `ranges[1]` ≠ 0; the cell counts are 2 and 2.
   - Every child `reg` is non-empty (u64,u64) pairs inside `ranges`.
   - `gic` has the compatible and interrupt properties above and a 32-byte `reg`, with GICD ≥ 64 KiB and GICR a whole number of 128 KiB frames.
   - `interrupt-controller` is `"master"`, with the GICD as its `reg`.
   - Exactly one node has `device_type` `"timer"`, with one `reg` pair.
6. **Console.** `/defaults serial-device` names exactly one node, which is `"arm,pl011"` with one `reg` pair.
7. **`/cpus`.**
   - `#address-cells` is 1 and `#size-cells` is 0; there are 1 to 32 cpus.
   - Each cpu has `device_type` `"cpu"` and a u32 `reg` that is unique.
   - Each `state` is `"running"` or `"waiting"`, and exactly one is running.
   - Each `timebase-frequency` is non-zero, and `interrupt-parent` is the GIC's phandle.
   - Each `interrupts` is three u32: two distinct SGIs, with a PPI between them.
8. **Redistributors.** There is one GICR frame per cpu node, or at least one per node when the tree holds the maximum of 32.

dtdump adds cross-checks against the tables:
- one cpu node per enabled GICC, up to 32;
- GICR frames equal to the number of MADT GICCs;
- GICD, GICR and UART bases, plus `socBase`, equal to their ACPI addresses;
- the relocated copy parsing to the same facts as the original.

## Tools

- `\NeoDarwin\boot.cfg` containing `dump-acpi` makes neoboot print every table (RSDP, XSDT, each XSDT table, DSDT, FACS) before booting. The format is Linux `acpidump`'s text: a `SIG @ 0x…` line per table and rows of 16 bytes. Capture it from any UEFI board's serial console. `boot/neoboot/testdata/qemu-virt-smp{1,4}.acpidump` were captured this way on QEMU 11.1 `virt,gic-version=3`, `neoverse-n2`, 2 GiB.
- `dtdump [--dram-base HEX] [--dram-size HEX] [--timebase HZ] [--boot-mpidr HEX] [--seed HEX] [--acpi-base HEX] [--ramdisk HEX,HEX] [--write-dt FILE] ACPIDUMP` prints the tables' facts and the tree, then checks it, and exits 1 on any violation. `dtdump --dt FILE` checks an existing binary tree. The tests are `//tools/dtdump:all`: two golden trees, a truncated MADT, and a tree with a dangling `serial-device`.

## Versioning

Adding an optional property is backwards compatible and keeps v1, with a row here. Removing or retyping a property, or changing a node's meaning, makes v2. Planned additions:
- P1-05: `/arm-io/gic` `timer-ppi` and `timer-group`, from GTDT.
- P1-06: a PSCI conduit, from FADT `ARM_BOOT_ARCH`, which neoboot already reads and logs. Dropping the `cpus=1` cap also belongs to P1-06.
- P1-10 and the Tier 2 kext: `/arm-io/pcie@N`, from MCFG and IORT.
- P1-11 (CD8180): GICv4 redistributors, whose 256 KiB frames need the kernel's frame stride from the tree.
- P1-12: a 16550 serial node.
