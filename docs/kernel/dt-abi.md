<!-- SPDX-License-Identifier: BSD-2-Clause -->
# DT-ABI v1: the device tree neoboot gives the kernel

**Version 1, P1-04; `timer-ppi` and `timer-group` added in P1-05; `/chosen` `psci-conduit` added in P1-06; `/chosen` `boot-uuid` added in P1-10; v1.1, P1-11: the UART is optional when `boot_args` carries a framebuffer; v1.2, P1-12: the UART may be a Qualcomm GENI serial engine (`"qcom,geni-debug-uart"`).** This is the contract between the loader (`boot/neoboot`) and the SBSA kernel. neoboot writes it from the machine's ACPI tables and its own facts, in Apple's flattened format (`pexpert/pexpert/device_tree.h`, not FDT). The design is in `arm64-sbsa-bringup.md` §2.2.

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
| GTDT | `ACPI.parse` | the EL1 virtual timer's GSIV and flags: a level-sensitive PPI (INTID 16–31), SBSA's being 27 |
| GIC distributor, `GICD_CTLR` | `Main.swift` `chooseTimerGroup` | the security state: `DS` (bit 6) reads 1 when Non-secure software may use Group 0, 0 when the GIC has two security states and Group 0 is Secure. It picks the timer's group |
| SPCR | `ACPI.parse`, `ACPI.uartProblem`, `Platform.uartUse` | the console UART's interface type and base address. It becomes the kernel's serial console only if the kernel drives it (below); otherwise the framebuffer is the only console, if there is one. The Generic Address Structure's access size is recorded but never checked: the Q8B's firmware stores 0x20 there, the register width in bits, instead of an encoded size (0–4). Every UART the kernel drives has 32-bit registers, which its driver knows; Linux and FreeBSD fall back to the driver's width the same way |
| FADT (`FACP`), else the XSDT header | `ACPI.parse`, `ACPI.copyOEM` | OEM ID and OEM table ID for `model`; hardware-reduced flag; ARM boot flags (`PSCI_COMPLIANT`, `PSCI_USE_HVC`), which choose the PSCI conduit |
| `ID_AA64PFR0_EL1.EL3` (bits 15:12) | `Main.swift` `choosePSCIConduit` | whether the CPU implements EL3, where PSCI firmware lives: the conduit when the FADT reports no PSCI |
| the loader | `Main.swift` | DRAM window, `CNTFRQ_EL0`, the boot CPU's `MPIDR_EL1`, the `CNTPCT` seed, UEFI `GetTime()`, ramdisk and trust-cache placement (`\NeoDarwin\trustcache`, checked by `TrustCache.swift`), where the ACPI copy goes, the exception level it runs at, whether a GOP framebuffer exists (`GOP.swift`), and `timer-group=0`/`timer-group=1`, `gop=off` and `uart=off` in `boot.cfg` |

Every table is checked for length and checksum. The FACS has no checksum; it's dumped but never parsed. Tables neoboot doesn't parse (DSDT, MCFG, IORT, PPTT, DBG2, …) are still copied for the kernel.

neoboot refuses to boot, and prints why, when:
- the RSDP or any table is missing, truncated, or fails its checksum;
- there is no MADT or GTDT;
- there is no GICD, more than one GICD, or no GICC;
- the GIC version is neither 3 nor 0. GICv4 redistributors are 256 KiB, and the kernel steps through them in 128 KiB `GICR_PE_SIZE` units;
- the GICR ranges are too small for one frame per GICC, or the GICC frames aren't contiguous;
- the virtual timer isn't a PPI (INTID 16–31), or the GTDT calls it edge-triggered. The generic timer's interrupt is level-sensitive, and the kernel re-arms it by writing `CNTV_CVAL`, which drops the line;
- a GICC's performance interrupt isn't a PPI;
- two enabled GICCs have the same MPIDR;
- an enabled GICC's MPIDR has a non-zero Aff3. The kernel keeps a CPU's id as MPIDR Aff2:Aff1:Aff0 (the cpu `reg`, which `start.s` matches against `MPIDR_EL1[23:0]`, P1-17);
- the kernel has no console: there is no GOP framebuffer, and the SPCR UART is missing, not in system memory, at address 0, not a PL011, an SBSA Generic UART (types 0x03, 0x0D, 0x0E) or a Qualcomm GENI UART (0x11, 0x13), or turned off with `uart=off`. The 16550 family (0x00, 0x01, 0x12) waits for the rest of P1-12. With a framebuffer, such a UART is left out of the tree instead (see "The console" below);
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
| `acpi-rsdp` | u64: physical address of the copied RSDP | ACPI copy | `nd_acpi_osl.c` `AcpiOsGetRootPointer`: ACPICA's root (P1-09, `acpi.md`). Without it the ACPI platform doesn't start |
| `acpi-tables` | (u64,u64): the ACPI copy | ACPI copy | not read: the copy is inside DRAM, which `nd_acpi_osl.c` maps for ACPICA through the physmap (`dram-base`, `dram-size`), anything else as device memory |
| `boot-uuid` | string: a UUID, 36 characters and a NUL. Omitted with a ramdisk (unless `boot.cfg` names one) and when the boot disk has no GPT with an HFS+ partition | `boot.cfg` `boot-uuid=`, else the unique GUID of the boot disk's first HFS+ GPT partition (P1-10, `storage.md`) | `IOKitBSDInit.cpp` `IOFindBSDRoot`: published as the `boot-uuid` resource; AppleFileSystemDriver publishes the IOMedia whose `UUID` (or HFS+ volume UUID) it is as `boot-uuid-media`, the root |
| `psci-conduit` | `"smc"` or `"hvc"`. Omitted when there is no PSCI | FADT `ARM_BOOT_ARCH`, `ID_AA64PFR0_EL1.EL3`, the loader's EL (below) | `NeoDarwinPSCI.cpp` `init`: `CPU_ON`, `CPU_OFF` and `PSCI_VERSION` go through `smc #0` or `hvc #0` |
| `AAPL,phandle` | u32 2 | constant | phandle map |

### `/chosen/memory-map`

| Property | Value | Source | Kernel reader |
|---|---|---|---|
| `RAMDisk` | (u64,u64), length a multiple of 16 KiB. Omitted without a ramdisk | loader | `IOKitBSDInit.cpp:766-826` → md0 |
| `ACPITables` | (u64,u64), equal to `/chosen acpi-tables` | ACPI copy | `IODTGetLoaderInfo` (`IODeviceTreeSupport.cpp`) for the Tier 2 kext. `libsa/bootstrap.cpp:392` walks the node but takes only `Driver-*` entries |
| `TrustCache` | (u64,u64): the static trust cache segment, in whole 16 KiB pages, just below the kernel collection: it starts at `physBase` and `dram-base`. Omitted without `\NeoDarwin\trustcache` | loader: iBoot's layout, `trust_cache_offsets_t` { `num_caches` 1, `offsets[0]` 8 } followed by the file, one version 1 module that neoboot checks as the kernel will (`TrustCache.swift`), zero-filled to the page | `arm_vm_init.c`: must lie below the kernel's lowest segment (a RELEASE kernel panics otherwise, before the console), and is mapped read-only as `EXTRADATA`. `kern_trustcache.c` `load_static_trust_cache` (a `DTTrustCacheRange`): the first module becomes the static trust cache, and a module that fails to load panics. `nd_amfi_policy.c`: its presence turns code-signing enforcement on (P1-15, `amfi-provider.md` §4) |

Every entry lies inside `[dram-base, dram-base+dram-size)`, below `topOfKernelData`. With a trust cache, `boot_args` `physBase` and `virtBase` (and `dram-base`) are the trust cache's page rather than the collection's, as iBoot lays memory out: `virtBase` = link address − the trust cache's length.

### `/defaults`

The node is always there: `get_serial_device_phandle` panics without it (`pe_serial.c:831-833`).

| Property | Value | Source | Kernel reader |
|---|---|---|---|
| `serial-device` | u32: the UART's `AAPL,phandle` (3). Omitted when the console is the framebuffer alone (v1.1) | constant | `pe_serial.c:837`, resolved at `:921`. The node's `compatible` selects the driver at `:934`. Without it `serial_init` returns 0: no serial device |

### `/cpus` and `/cpus/cpuN`

`/cpus` has `#address-cells` 1 and `#size-cells` 0. It holds one child per **enabled** MADT GICC, in MADT order, at most 32 (`MAX_CPUS`; `ml_parse_cpu_topology` asserts). The boot CPU is always listed. Online-capable CPUs that are not enabled are left out until hot-plug exists.

| Property | Value | Source | Kernel reader |
|---|---|---|---|
| `name` | `"cpu<index>"` | index | IORegistry naming only |
| `device_type` | `"cpu"` | constant | `AppleARMSMP.cpp:97` matching; `IOPlatformExpert.cpp:1681` cpu nubs |
| `reg` | u32: MPIDR Aff2:Aff1:Aff0 | GICC MPIDR | `machine_routines.c:1197` (`phys_id`, mandatory); `AppleARMSMP.cpp:98`; `IOPlatformExpert.cpp:1686`; the `CPU_ON` target (`NeoDarwinPSCI`) and the `ICC_SGI1R_EL1` affinity (`NeoDarwinGICv3::sendIPI`). All three affinity levels identify the CPU: the reset vector (`start.s`), `ml_get_cpu_number` and `find_gicr_pe_base` match bits 23:0 (patch 0020), since DynamIQ cores number themselves in Aff1 with Aff0 0 (the Q8B: 0x000–0x700) |
| `die-cluster-id` | u32 0 | constant | `machine_routines.c:1268` (`die_cluster_id`, default MPIDR Aff1). One cluster: see below |
| `cluster-core-id` | u32 index | index | `machine_routines.c:1271` (`cluster_core_id`, default MPIDR Aff0) |
| `state` | `"running"` for the boot CPU, `"waiting"` for the rest | boot MPIDR | `machine_routines.c:1066` (`ml_is_boot_cpu`); `pe_identify_machine.c:64` (only the running CPU's timebase is read) |
| `timebase-frequency` | u32 `CNTFRQ_EL0` | loader | `pe_identify_machine.c:70-80`. Without it the kernel assumes 24 MHz: it never reads `CNTFRQ` |
| `interrupt-parent` | u32 5 (the GIC) | constant | `IODeviceTreeSupport.cpp:566` |
| `interrupts` | three u32: SGI 0 (IPI), the PMU PPI, SGI 1 (deferred IPI) | GICC performance GSIV; 23 (SBSA PPI 7) if the MADT gives 0 | `AppleARMSMP.cpp:125-150`: with three specifiers it registers entries 0 and 2 as IPIs, once per CPU, and never enables entry 1. `NeoDarwinGICv3` keeps them per CPU (banked). SGI numbers match `NeoDarwinGICv3.cpp` `ND_SGI_IPI`/`ND_SGI_DEFERRED_IPI` |
| `AAPL,phandle` | u32 16 + index | constant | phandle map |

**One cluster (P1-17).** The SBSA kernel has no `HAS_CLUSTER`, so `ml_parse_cpu_topology` takes a CPU's cluster from `cluster-type` alone, and every CPU without one (all of them: neoboot writes none) is in logical cluster 0, one SMP processor set. That is right for a DynamIQ system such as the Q8B, whose eight cores share one DSU and L3 whatever their MPIDR Aff1. neoboot writes `die-cluster-id` 0 and `cluster-core-id` = the cpu's index so the per-CPU fields describe that cluster; the kernel's defaults (MPIDR Aff1, Aff0) are a core number and 0 on DynamIQ. Splitting big and little cores (`cluster-type` `'P'`/`'E'`) needs the AMP scheduler, which SBSA doesn't build.

**The PSCI conduit (P1-06).** The kernel starts every CPU but the boot one with PSCI `CPU_ON`. It reaches PSCI through the conduit the firmware describes, chosen at boot as Linux chooses it (`Platform.psciConduit`):
- The FADT's `ARM_BOOT_ARCH` says `PSCI_COMPLIANT`: SMC, or HVC with `PSCI_USE_HVC`. QEMU `virt` without EL3 says HVC: its own PSCI emulation answers.
- It says PSCI is absent, and the CPU implements EL3 (`ID_AA64PFR0_EL1.EL3` ≠ 0): SMC, to that EL3's firmware. QEMU `virt,secure=on` with TF-A is this case. QEMU describes only its own PSCI emulation, which it turns off when firmware owns EL3, although TF-A's BL31 implements PSCI (`qemu-secure.md`).
- It says HVC, but neoboot runs at EL2: none. neoboot enters the kernel at EL1 and leaves nothing at EL2 to answer an HVC.
- Otherwise none.

neoboot logs the choice (`neoboot: PSCI conduit: HVC, as the FADT says`). With no conduit on a multiprocessor it appends `cpus=1` to the command line, unless the line names `cpus=` or `cpumask=`, and says why. `ml_parse_cpu_topology` then keeps only the boot CPU (`machine_routines.c:1138, 1181-1186`), and `IODTPlatformExpert::createNubs` registers the other cpu nubs unused (`IOPlatformExpert.cpp:1693-1700`). The tree still describes the whole machine. A kernel told to start a CPU without a conduit panics and says so (`NeoDarwinPSCI::enableCPUCore`).

**Secondary CPUs.** Every CPU node takes part in the topology whatever its `state` (`machine_routines.c:1160-1240`), and `AppleARMSMP::cpu_boot_thread` starts each one through `IOPMGR::enableCPUCore`, which is `NeoDarwinPSCI`: `CPU_ON` with the node's `reg` as the target MPIDR and the physical address of `LowResetVectorBase` as the entry point (patch 0017). The CPU enters there with the MMU off and finds its `cpu_data` by MPIDR (`start.s`). `state` stays `"waiting"` for them: the kernel only asks which CPU is `"running"`.

Every cpu node names the same SGIs, which are banked: each CPU has its own. `NeoDarwinGICv3` gives a cpu nub's SGIs and PPIs vectors of that CPU's own and enables them in that CPU's redistributor. Before P1-06 the second CPU's registration of SGI 0 panicked (`Error registering IPIs @AppleARMSMP.cpp:138`).

### `/arm-io`

| Property | Value | Source | Kernel reader |
|---|---|---|---|
| `name` | `"arm-io"` | constant | `pe_identify_machine.c:170` |
| `device_type` | `"soc"` | constant | `pe_identify_machine.c:172` |
| `ranges` | three u64: child 0, parent `socBase`, size | MADT, SPCR | `pe_identify_machine.c:176-177`: `ranges[1]` is `gSocPhys`, the base every `reg` below is relative to. 0 means "no SoC" to `pe_identify_machine` (`:45`) and `serial_init` (`pe_serial.c:902-907`), so the timebase and the console would be lost |
| `#address-cells`, `#size-cells` | u32 2, 2 | constant | `IODTGetCellCounts`; `pe_serial.c` and `pe_fiq.c` assume u64 pairs |
| `AAPL,phandle` | u32 4 | constant | phandle map |

`socBase` is the lowest of the GICD, GICR and UART bases, rounded down to 64 KiB. `socSize` covers the highest end, rounded up to 64 KiB. Each device `reg` is therefore its ACPI physical address minus `socBase`, whatever the board's memory map. An identity mapping (parent 0) is impossible because of the zero test above. Nothing in the kernel maps the whole range: every user adds `gSocPhys` to one `reg` and maps only that (`pe_serial.c:726`, `pe_fiq.c:153,160`, `pe_identify_machine.c:209,220`, `NeoDarwinGICv3.cpp:82-83`).

### `/arm-io/gic`

| Property | Value | Source | Kernel reader |
|---|---|---|---|
| `compatible` | `"arm,gic-v3"` | constant | informational |
| `reg` | four u64: GICD offset, 64 KiB, GICR offset, frames × 128 KiB | MADT GICD, GICR/GICC | `pe_fiq.c:125-166`: panics without the node or with fewer than 32 bytes. `NeoDarwinGICv3.cpp:73-83` caps the GICR mapping at `MAX_CPUS` frames |
| `interrupt-controller` | `"gic"` | constant | `IODeviceTreeSupport.cpp:573,659` (ends the interrupt-parent walk) |
| `#interrupt-cells` | u32 1: a specifier is one INTID | constant | `IODeviceTreeSupport.cpp:609` |
| `#address-cells` | u32 0 | constant | interrupt-map resolution |
| `timer-ppi` | u32: the EL1 virtual timer's INTID, 16–31 (27 on SBSA machines) | GTDT virtual timer GSIV | `pe_fiq.c:45` `pe_gic_timer_properties` (patch 0016); `NeoDarwinGICv3.cpp` `handleInterrupt`. Absent means 27; out of range panics |
| `timer-group` | u32: 0 for Group 0 (a FIQ), 1 for Group 1 (an IRQ) | `GICD_CTLR.DS`, or `boot.cfg` | `pe_fiq.c:194` (patch 0016): Group 1 skips every Group 0 register. `sleh.c` `sleh_irq`, `NeoDarwinGICv3.cpp`. Absent means 0; above 1 panics |
| `AAPL,phandle` | u32 5 | constant | `interrupt-parent` of every cpu node; `NeoDarwinGICv3.cpp:105` `IODTInterruptControllerName` |

**The timer's group (P1-05).** neoboot reads `GICD_CTLR` before it leaves boot services. `DS` = 1 means one security state, or security disabled: QEMU `virt` without EL3 and Apple's hypervisor are like this. The timer then stays on Group 0 and arrives as a FIQ, the path Apple's `pe_fiq.c` and `sleh_fiq` use. `DS` = 0 means two security states. A Non-secure read sees `DS` as 0 because it is RES0 in the Non-secure view. Group 0 is Secure there: its interrupts go to EL3, and the `ICC_*0_EL1` registers trap to EL3 when `SCR_EL3.FIQ` is set. The timer therefore goes on Group 1 and arrives as an IRQ. `timer-group=1` in `boot.cfg` forces Group 1 on a GIC that would allow Group 0; that's how QEMU tests the TrustZone path (`//kernel:sbsa_timer_group1_boot_test`). `timer-group=0` forces Group 0, which a DS = 0 machine cannot run: the timer never reaches the kernel as a FIQ, and the Group 0 register writes may trap to EL3. The option also reaches the kernel's command line, which ignores it.

With Group 1 the kernel writes only registers that Non-secure software owns when DS = 0:
- `GICR_IGROUPR0`, for the DS = 1 case only. It is RAZ/WI to Non-secure accesses when DS = 0, and the firmware has already made the PPIs Non-secure Group 1;
- the timer's `GICR_IPRIORITYR` byte (0x80), `GICR_ISENABLER0`, and `GICD_CTLR` `ARE_NS`/`EnableGrp1A`. Bits 4 and 1 hold those fields in both views;
- `ICC_SRE_EL1`, `ICC_BPR1_EL1`, `ICC_PMR_EL1`, `ICC_CTLR_EL1.EOImode` and `ICC_IGRPEN1_EL1`.

It never writes `GICR_IGRPMODR0`, `ICC_BPR0_EL1` or `ICC_IGRPEN0_EL1`. The timer is then acknowledged with `ICC_IAR1_EL1` and completed with `ICC_EOIR1_EL1`. Before `NeoDarwinGICv3` attaches, `sleh_irq` does that itself, because AppleARMSMP's `PE_handle_ext_interrupt()` has no controller to call yet. After that, `NeoDarwinGICv3`'s IRQ loop does it. Both run `sleh_fiq`'s timer branch (`rtclock_intr`).

**Redistributors.** There is one 128 KiB frame per MADT GICC, counted over every GICC and not only the listed CPUs. QEMU reserves 0xF60000 (123 frames) for any CPU count, and mapping all of that made a 16 MB `kmem_alloc` fail (`arm64-sbsa-bringup.md` §2.1.2). The kernel finds each CPU's frame by walking `GICR_TYPER` from the start of the range until the `Last` bit (`pe_fiq.c:94-115`, `NeoDarwinGICv3.cpp` `redistributorForCurrentCPU`), so the range must begin at the first frame and hold every CPU's frame. v1 describes one range. A second GICR range that isn't adjacent to the first (multi-socket) is ignored when the first is big enough, and refused when it isn't.

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
| `compatible` | `"arm,pl011"` for SPCR type 0x03, 0x0D or 0x0E; `"qcom,geni-debug-uart"` (Linux's name for the GENI debug UART) for 0x11 or 0x13 (v1.2) | SPCR interface type (`Platform.uartCompatible`) | `pe_serial.c:804-811, 934`: `pl011_uart_setup`, or `geni_uart_setup` (patch 0032, `serial.md`) |
| `reg` | (u64,u64): SPCR base − `socBase`, and 4 KiB for a PL011, 16 KiB (a serial engine's register window) for a GENI UART | SPCR | `pe_serial.c:719-726` and patch 0032, asserting exactly 16 bytes; the size is what `ml_io_map` maps |
| `AAPL,phandle` | u32 3 | constant | `/defaults serial-device` |

The UART has no `interrupts`: the console is polled (`serial_keyboard_poll`). neoboot's own console switches to this UART at `ExitBootServices` when it is a PL011; it never writes to a GENI UART (below).

A GENI serial engine that isn't clocked faults when touched, and nothing in ACPI says which engines UEFI left running. The only one neoboot ever names is the SPCR console, which UEFI is using; the kernel's driver keeps UEFI's clock and bit rate, and registers no console if the engine's protocol firmware isn't the UART's (`serial.md`).

The node exists only when the kernel's console is this UART (`Platform.uartUse` is `.console`). `socBase` then counts its address; otherwise it covers the GIC alone.

### The console (v1.1)

The kernel's console is the SPCR UART when the kernel drives it, and the framebuffer in `boot_args.Video` alone otherwise. neoboot decides after it has looked for a GOP (`chooseConsole` in `Main.swift`, `Platform.uartUse`):

| SPCR UART | GOP framebuffer | Tree | neoboot says |
|---|---|---|---|
| PL011, SBSA Generic or Qualcomm GENI (the Q8B's, 0x13), in system memory, non-zero address | either: with one, both are consoles (bring-up doc §2.1.7) | `/arm-io/uart0`, `/defaults serial-device` = 3 | (the ACPI line names the UART: `UART Qualcomm GENI at 0x884000`) |
| any other type, e.g. a 16550 (0x00) | yes | neither | `neoboot: SPCR UART type 0x0 at 0x… has no kernel driver; console on the framebuffer only` |
| turned off with `uart=off` in `boot.cfg` | yes | neither | `neoboot: SPCR UART not used (uart=off); console on the framebuffer only` |
| no SPCR, not in system memory, or at address 0 | yes | neither | `neoboot: no SPCR UART; …` or `neoboot: SPCR UART unusable (…); …` |
| anything the kernel can't drive, or `uart=off` | no | none: neoboot refuses | the ACPI reason, then `neoboot: and there is no GOP framebuffer to use instead: the kernel would have no console` |

`uart=off` is for boards whose UART is unreadable, and for testing the framebuffer-only path on QEMU. Like every `boot.cfg` option, it also reaches the kernel's command line, which ignores it.

Without `serial-device`, `serial_init` finds no serial device, so `arm_init` never switches to the serial console and `cons_ops_index` stays `VC_CONS_OPS`: kernel printf, IOLog and `/dev/console` (getty, shells) draw on the video console, as upstream. `PE_init_kprintf` falls back to `console_write_unbuffered`, the same console, and patch 0021 stops `consdebug_putc` from drawing panic output twice through it. There is no input: `_vcgetc` polls `uart_getc`, which has no device, and the serial keyboard thread `serial=3` starts finds nothing. getty waits at `login:`. `/dev/console` reports 80×24 because `serial=3` asks for the serial console (`kmopen`, `bsd/dev/arm/km.c:116`), although the screen has more cells.

Once `ExitBootServices` has run, neoboot writes to the SPCR UART only if it is the kernel's console and a PL011. It never touches another UART as though it were one, and prints nothing after that point (its last line, `neoboot: entering the kernel`, is lost). That includes the Q8B's GENI UART: after UEFI, the first thing to touch it is the kernel driver's init, and one line isn't worth a transmit path in the loader that only the board can test. Everything before goes through the firmware's console, which on the Q8B is the HDMI screen and, through UEFI's own driver, the GENI UART.

### Outside the tree: `boot_args.Video`

The framebuffer is not in the tree. It travels in `boot_args.Video` (`pexpert/pexpert/arm64/boot.h`), which `PE_init_platform` copies into `PE_state.video` (`pe_init.c:425-436`). The framebuffer console is described in `arm64-sbsa-bringup.md` §2.1.7.

| Field | Value | Kernel reader |
|---|---|---|
| `v_baseAddr` | physical address of the GOP's linear framebuffer (`Mode->FrameBufferBase`); 0 with no usable GOP or with `gop=off` in `boot.cfg` | `initialize_screen` maps it with `ml_io_map_unmappable` (`video_console.c`); 0 means no video console |
| `v_display` | 0: a text console, not a boot picture | `PE_create_console` → `kPETextMode` |
| `v_rowBytes` | `PixelsPerScanLine` × 4 | `vc_*` |
| `v_width`, `v_height` | the current mode's resolution; neoboot never changes the mode | `vc_initialize`: columns = width / 8, rows = height / 16 |
| `v_depth` | 32; the rotation and scale bytes are 0 | depth 32 draws xRGB words, GOP's `PixelBlueGreenRedReserved8BitPerColor` |

The framebuffer never lies inside `[dram-base, dram-base+dram-size)`: neoboot cuts it out of the DRAM window, so the kernel maps it as memory it doesn't own.

## The checks (`DTCheck.check`)

A tree is DT-ABI v1 when:
1. **Structure.** It parses: no node or property overruns it, and the root node ends exactly at its length.
2. **Names and phandles.** Every node has a `name` string. Every `AAPL,phandle` is a u32 that is unique in the tree.
3. **`/`.** It has the name, compatible, model, target-type and cell counts listed above.
4. **`/chosen`.**
   - `dram-base` and `dram-size` are u64, the size is non-zero and the base is 16 KiB aligned.
   - `random-seed` is at least 64 bytes; `debug-enabled` is a u32; `firmware-version` is a string.
   - `psci-conduit`, if present, is `"smc"` or `"hvc"`.
   - `boot-uuid`, if present, is a UUID string: 36 characters and a NUL.
   - `acpi-rsdp` is non-zero and lies inside `acpi-tables`, which is (u64,u64).
   - `/chosen/memory-map` exists. Each entry is a non-empty (u64,u64) inside DRAM, `ACPITables` equals `acpi-tables`, a `RAMDisk` length is a multiple of 16 KiB, and a `TrustCache` is at least a segment header and a module header (32 bytes).
5. **`/arm-io`.**
   - `device_type` is `"soc"`; `ranges` is three u64 with `ranges[1]` ≠ 0; the cell counts are 2 and 2.
   - Every child `reg` is non-empty (u64,u64) pairs inside `ranges`.
   - `gic` has the compatible and interrupt properties above and a 32-byte `reg`, with GICD ≥ 64 KiB and GICR a whole number of 128 KiB frames.
   - `gic` `timer-ppi`, if present, is a u32 from 16 to 31, and `timer-group`, if present, is a u32 0 or 1.
   - `interrupt-controller` is `"master"`, with the GICD as its `reg`.
   - Exactly one node has `device_type` `"timer"`, with one `reg` pair.
6. **Console.** `/defaults` exists. Its `serial-device`, if present, is a u32 naming exactly one node, which is `"arm,pl011"` or `"qcom,geni-debug-uart"` with one `reg` pair at least as large as its register window (4 KiB, 16 KiB). It may be absent only when `boot_args` carries a framebuffer (`DTCheck.check`'s `framebuffer`, which neoboot sets from its GOP and dtdump from `--gop`).
7. **`/cpus`.**
   - `#address-cells` is 1 and `#size-cells` is 0; there are 1 to 32 cpus.
   - Each cpu has `device_type` `"cpu"` and a u32 `reg` that is unique and has no bits above Aff2 (23:0).
   - Each cpu has `die-cluster-id` 0 and `cluster-core-id` equal to its index.
   - Each `state` is `"running"` or `"waiting"`, and exactly one is running.
   - Each `timebase-frequency` is non-zero, and `interrupt-parent` is the GIC's phandle.
   - Each `interrupts` is three u32: two distinct SGIs, with a PPI between them that isn't the timer's (`timer-ppi`, else 27).
8. **Redistributors.** There is one GICR frame per cpu node, or at least one per node when the tree holds the maximum of 32.

dtdump adds cross-checks against the tables:
- one cpu node per enabled GICC, up to 32;
- GICR frames equal to the number of MADT GICCs;
- GICD, GICR and UART bases, plus `socBase`, equal to their ACPI addresses;
- `/arm-io/uart0` present exactly when the SPCR UART is the kernel's console;
- `timer-ppi` equal to the GTDT's virtual timer GSIV;
- the relocated copy parsing to the same facts as the original.

## Tools

- `\NeoDarwin\boot.cfg` containing `dump-acpi` makes neoboot print every table (RSDP, XSDT, each XSDT table, DSDT, FACS) before booting. The format is Linux `acpidump`'s text: a `SIG @ 0x…` line per table and rows of 16 bytes. Capture it from any UEFI board's serial console. `boot/neoboot/testdata/qemu-virt-smp{1,4}.acpidump` were captured this way on QEMU 11.1 `virt,gic-version=3`, `neoverse-n2`, 2 GiB, and `qemu-virt-secure-smp4.acpidump` on `virt,secure=on` with TF-A (`--machine virt-secure --smp 4`), and `qemu-sbsa-ref-smp4.acpidump` on `sbsa-ref` with TF-A and EDK2 SbsaQemu (`--machine sbsa-ref --smp 4`, 2 GiB; `qemu-sbsa-ref.md`), whose golden tree `//tools/dtdump:qemu_sbsa_ref_smp4_test` checks with DRAM at 1 TiB, a 1 GHz counter, DS=0 and the loader at EL2.
- `timer-group=1` (or `=0`) in `boot.cfg` chooses the timer's group instead of `GICD_CTLR.DS` (see `/arm-io/gic`). neoboot logs the choice: `neoboot: GIC: GICD_CTLR 0x…, DS=…; timer PPI 27 on Group 0 (FIQ)`.
- `uart=off` in `boot.cfg` makes neoboot treat the SPCR UART as absent (see "The console").
- `boot-uuid=<UUID>` in `boot.cfg` names the root (`/chosen boot-uuid`), even when there is a ramdisk; then no `rd=md0` is added (`storage.md`).
- `dtdump [--dram-base HEX] [--dram-size HEX] [--timebase HZ] [--boot-mpidr HEX] [--seed HEX] [--acpi-base HEX] [--ramdisk HEX,HEX] [--trust-cache HEX,HEX] [--gicd-ctlr HEX] [--timer-group 0|1] [--el3] [--loader-el 1|2] [--gop WxH] [--uart-off] [--boot-uuid UUID] [--write-dt FILE] ACPIDUMP` prints the tables' facts and the tree, then checks it, and exits 1 on any violation. `--gicd-ctlr` is the value neoboot would read (default 0x40, DS = 1, as on QEMU). `--el3` says the CPU implements EL3 and `--loader-el` is the EL neoboot runs at (default 1); with the FADT they choose the PSCI conduit. `--gop WxH` says neoboot found a framebuffer of that mode, and `--uart-off` models `uart=off`: together they choose the console. `--boot-uuid` adds `/chosen boot-uuid`, and `--trust-cache` a `/chosen/memory-map` `TrustCache` entry. `dtdump [--gop WxH] --dt FILE` checks an existing binary tree. The tests are `//tools/dtdump:all`:
  - golden trees: QEMU with one and four CPUs, one with a DS = 0 distributor, QEMU with TF-A and four CPUs (conduit SMC), and 18 `cortex-a76` CPUs;
  - the Radxa Dragon Q8B's own tables (`radxa-dragon-q8b.acpidump`): golden trees with eight CPUs (MPIDR 0x000–0x700), eight GICR frames at 0x17a60000, PPI 27, SMC and the GENI UART at 0x884000 (16 KiB), without and with `--gop 1920x1080`; with `--uart-off`, no UART with `--gop` and refused without;
  - `qemu-virt-spcr-geni.acpidump`, QEMU's single-CPU tables with the SPCR rewritten as the Q8B's (type 0x13 at 0x884000): golden trees with the GENI UART, without and with `--gop`; its `--uart-off` tree rejected by `--dt` without `--gop`; its tree with the UART's compatible renamed rejected by `--dt`;
  - a truncated MADT, an MPIDR with Aff3 set, and a tree with a dangling `serial-device`;
  - a golden tree with a `TrustCache` entry, and one too short for a segment refused.

## Versioning

Adding an optional property is backwards compatible and keeps v1, with a row here. Removing or retyping a property, or changing a node's meaning, makes v2. Making a property optional in a case where the kernel already copes without it is a minor revision (v1.1). Changes:
- P1-05 (added): `/arm-io/gic` `timer-ppi` and `timer-group`, from the GTDT and `GICD_CTLR`.
- P1-06 (added): `/chosen` `psci-conduit`, from FADT `ARM_BOOT_ARCH` and the CPU's EL3. neoboot caps the kernel at one CPU only when there is no conduit.
- P1-10 (added): `/chosen` `boot-uuid`, the root by UUID when there is no ramdisk (`storage.md`).
- The Tier 2 kext: `/arm-io/pcie@N`, from MCFG and IORT.
- P1-11 (added, v1.1): `/defaults serial-device` and the UART node are optional when `boot_args` carries a framebuffer. The Radxa Dragon Q8B boots this way, its console on HDMI.
- P1-12 (v1.2, the Radxa Dragon Q8B): `/arm-io/uart0` may be `"qcom,geni-debug-uart"` with a 16 KiB `reg`, for SPCR types 0x11 and 0x13 (patch 0032, `serial.md`). A kernel without patch 0032 panics on such a tree (`Unable to find serial device driver`), hence the minor version. The Q8B's GIC is v3 with 128 KiB frames, so no stride change is needed there; GICv4 boards would need the stride in the tree.
- P1-12, next: a 16550 serial node.
- P1-15 (added): `/chosen/memory-map` `TrustCache`, the static trust cache from `\NeoDarwin\trustcache` (`amfi-provider.md` §4). A kernel without P1-15's ndamfi loads it too (XNU reads it), but enforces nothing.
