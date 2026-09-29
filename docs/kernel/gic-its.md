<!-- SPDX-License-Identifier: BSD-2-Clause -->
# MSI and MSI-X through the GIC ITS (P1-09)

**P1-09, checkpoint 3.** PCI devices get MSI and MSI-X through the GICv3 Interrupt Translation Service. An MSI is a memory write of an EventID to the ITS's `GITS_TRANSLATER`. The ITS takes the DeviceID from the write's requester ID (mapped through the IORT), looks the pair up in tables in memory, and raises an LPI (INTID 8192 and up) on a redistributor. IOPCIFamily handles the PCI side: capabilities, the MSI-X table, and the order of the specifiers. The platform side is closed on Macs; NeoDarwin's lives in `NeoDarwinGICv3`, `NeoDarwinGICv3ITS` and `NeoDarwinPCIMessagedInterruptController`. On QEMU `virt`, edu's MSI and two MSI-X vectors of an NVMe controller reach their handlers with one security state, with TF-A (DS = 0), on four CPUs, and behind an SMMUv3. The boot-arg `nd_pci_msi=0` turns MSIs off, and every device then keeps INTx.

## Pieces

| Where | What |
|---|---|
| `kernel/neodarwin/platform/NeoDarwinGICv3.cpp` | LPIs: the configuration and pending tables, `GICR_PROPBASER`, `GICR_PENDBASER`, `GICR_CTLR.EnableLPIs` on every redistributor, and LPI dispatch in the IRQ loop. `ndGICTableAlloc` allocates memory the GIC reads |
| `kernel/neodarwin/platform/NeoDarwinGICv3ITS.cpp` | an ITS: `GITS_BASER` tables, the command queue, and the MAPD, MAPC, MAPTI, INV, INVALL, SYNC and DISCARD commands |
| `kernel/neodarwin/pci/nd_iort.{h,c}` | C, host-testable: the MADT's GIC ITS structures, and the IORT path from a requester ID to an ITS DeviceID, through SMMUs. `//kernel/neodarwin/pci:iort_test` runs it on QEMU's tables and the Q8B's |
| `kernel/neodarwin/pci/NeoDarwinPCIMSI.{h,cpp}` | `NeoDarwinPCIMessagedInterruptController`, IOPCIFamily's `IOPCIMessagedInterruptController` with LPIs for vectors |
| `kernel/neodarwin/pci/NeoDarwinPCIHostBridge.cpp` | answers "GetMessagedInterruptController" and "GetMessagedInterruptAddress", finds each device's DeviceID, and claims MSI in `_OSC` |
| `kernel/neodarwin/pci/NeoDarwinPCIEduTest.cpp`, `NeoDarwinPCINVMeTest.cpp` | the proofs: edu's MSI; two MSI-X vectors of an NVMe controller |
| `kernel/patches/0024-iokit-build-gic-its-and-pci-msi.patch` | adds the four new files to `files.arm64`, with their search paths and the NVMe test's personality |

**Where the ITS comes from.** The kernel reads the MADT's GIC ITS structures (type 0xF: translation ID, base) through the ACPI platform's copy of the tables (`getACPITableData("APIC")`), on the first host bridge's probe. neoboot and the device tree are not involved: only PCI needs the ITS, PCI already depends on ACPI, and the IORT that names the ITS by ID is ACPI too. Passing the ITS in the tree would have extended DT-ABI, `dtdump` and the fixtures without any consumer outside ACPI's reach. When the MADT has no ITS (GICv2m platforms, or `virt,its=off`), MSIs are off and devices use INTx.

## LPIs in the redistributors

`NeoDarwinGICv3::initLPIs` runs once, when the first ITS is set up:

| | |
|---|---|
| LPI count | `GICD_TYPER.LPIS` must be set. The tables cover INTIDs below 2^min(`IDbits`, 14): 8192 LPIs (fewer if `GICD_TYPER.num_LPIs` says so). The MSI controller hands out the first 2048 |
| Configuration table | one byte per LPI (priority 0x80, bit 1 RES1, bit 0 enable), 8 KiB, shared by every redistributor (`GICR_PROPBASER`, IDbits 13). Every LPI starts disabled; MAPTI enables it |
| Pending tables | one per CPU the kernel runs, 2 KiB, 64 KiB aligned, zeroed (`GICR_PENDBASER.PTZ`) |
| Enable | `GICR_CTLR.EnableLPIs` on every redistributor the topology names, from any CPU. A CPU that comes up later, or whose redistributor PSCI reset, does its own again in `initCurrentCPU`, under a spin lock, since interrupts are off there. If firmware left LPIs enabled on the boot CPU's redistributor, `PROPBASER` can no longer change, so this fails and MSIs stay off |
| Dispatch | `ICC_IAR1_EL1` returns an INTID ≥ 8192: the registered LPI handler runs, and `ICC_EOIR1_EL1` drops the priority. LPIs are edge-triggered and have no active state. Only the MSI controller registers a handler |

LPIs are Non-secure Group 1 by definition, so nothing changes with TF-A and DS = 0 (`sbsa_secure_pci_boot_test`).

**Memory attributes.** Each table register (`GICR_PROPBASER`, `GICR_PENDBASER`, `GITS_BASER<n>`, `GITS_CBASER`) is written Inner Shareable, Normal Read/Write-allocate write-back, and read back. If shareability reads 0, the GIC doesn't snoop the CPU's caches. The register is then written again as non-cacheable, and every CPU write to that table (configuration bytes, level-1 device table entries, commands) is cleaned to the point of coherency (`FlushPoC_DcacheRegion`). Every table is zeroed and cleaned when it is allocated, whatever the attributes. QEMU keeps what it is given, so boot-arg `nd_gic_lpi_nc=1` forces the non-cacheable path to test it (`sbsa_pci_smmu_boot_test`).

**Memory.** `ndGICTableAlloc` returns a physically contiguous, zeroed `IOBufferMemoryDescriptor` (`kIOMemoryHostPhysicallyContiguous | kIOMemoryMapperNone`), aligned through the physical mask and below 2^48. None of it is freed while the system runs.

## The ITS

`NeoDarwinGICv3ITS::withAddress` maps 128 KiB at the MADT's base. It checks `GITS_TYPER.Physical`, disables the ITS if firmware left it on, and waits up to a second for `GITS_CTLR.Quiescent`. Then:

- **`GITS_BASER<n>`.** For each table the ITS implements (the type and entry size are read-only), the page size is the largest that sticks: 64 KiB, then 16 KiB, then 4 KiB. The **device table** needs an entry per DeviceID, 2^`Devbits` of them. If that flat table would exceed 256 KiB or 256 pages, the ITS gets a two-level table, if `Indirect` sticks: a level-1 array of 8-byte pointers, with each level-2 page allocated when the first DeviceID in it is mapped. The **collection table** gets a page, which holds far more collections than there are CPUs. vPE tables (GICv4) are left invalid. Boot-arg `nd_its_flat=1` forces a flat device table.
- **Command queue.** 64 KiB (2048 commands) at `GITS_CBASER`, with the same attributes treatment. A command is written at `GITS_CWRITER`'s offset and cleaned or barriered, then `CWRITER` moves on and the driver polls `GITS_CREADR` until it catches up. It waits at most a second, and logs the command and `CREADR` if the queue stalls (`CREADR.Stalled`, a command error) or times out. Commands are serialised by a lock and never issued from interrupt context.
- **Enable** (`GITS_CTLR.Enabled`), then **collections**: MAPC maps collection N to logical CPU N's redistributor (by physical address if `GITS_TYPER.PTA` is 1, else by `GICR_TYPER.Processor_Number`), followed by INVALL for each CPU and a SYNC on the boot CPU.

**Per device** (`mapDevice`, `mapEvent`): MAPD with an ITT (Interrupt Translation Table) for 2^n EventIDs, 256-byte aligned, of `GITS_TYPER.ITT_entry_size` entries. Then, for each vector, the LPI's configuration byte is enabled and cleaned, followed by MAPTI (EventID → LPI, on the boot CPU's collection), INV and SYNC. Freeing a vector issues DISCARD and SYNC and disables the LPI. If a device is mapped again with more events, it is unmapped (MAPD V=0) and given a bigger ITT, and its old events are lost; that doesn't happen today, since a device's vectors are allocated once.

QEMU's ITS (IIDR 0x43b): 16 DeviceID bits, 16 EventID bits, 12-byte ITT entries, PTA 0, all three page sizes, two-level device tables supported (so QEMU gets one: a flat table would take 512 KiB).

## DeviceIDs: the IORT

`NeoDarwinPCIHostBridge::msiRoute` builds the requester ID from bus, device and function, then `nd_iort_msi_route` follows it:

1. Find the root complex node whose PCI segment is the bridge's.
2. Take the ID mapping whose input range covers the ID (the count field is the number of IDs minus one). Mappings flagged "single" belong to a node's own interrupts, not to translating input IDs, so they are skipped. The output ID is the output base plus the offset into the range, and it continues at the output reference.
3. If that node is an ITS group, the output ID is the DeviceID and the group's first identifier names the ITS. If it is an SMMU (v1/v2 or v3), the SMMU's base and the stream ID are recorded and step 2 repeats at the SMMU. Four SMMUs at most.

With no IORT, or one that says nothing for the ID, the DeviceID is the requester ID on the MADT's first ITS, as Linux assumes, and the log says so. The SMMU is not programmed: MSIs, like DMA, rely on it being in bypass. The log names it, e.g. `via SMMUv3 at 0x9050000 as stream 0x20 (bypass)`.

| Machine | Path | Example |
|---|---|---|
| QEMU `virt` | root complex → ITS group (identity) | 00:04.0 → DeviceID 0x20 |
| QEMU `virt,iommu=smmuv3` | root complex → SMMUv3 0x9050000 (identity) → ITS group | 00:04.0 → stream 0x20 → DeviceID 0x20 |
| Radxa Dragon Q8B | segment s: RID → SMMUv3 0x14f80000 as stream `s << 16 \| RID`; the SMMU maps streams 0–0x6ffff to DeviceIDs from 0x80000 → ITS 0 | NVMe at 2:01:00.0 → stream 0x20100 → DeviceID 0xa0100 |

## IOPCIFamily's side, and the glue

IOPCIFamily resolves a device's interrupts lazily, the first time something reads its `IOInterruptSpecifiers` (`registerInterrupt`, `getInterruptType`) or a driver calls `IOPCIDevice::configureInterrupts`. Resolution does INTx first (source 0, `_PRT`, `pci.md`). Then `IOPCIBridge::resolveMSIInterrupts` sends "GetMessagedInterruptController" up the provider chain. The host bridge answers it with the kernel's one `NeoDarwinPCIMessagedInterruptController`, which is created on the first host bridge's probe, as long as the MADT has an ITS, LPIs come up and `nd_pci_msi` isn't 0. `allocateDeviceInterrupts` then:

1. sizes the request. MSI takes the capability's vectors, MSI-X its table size. Without `SUPPORT_MULTIPLE_MSI`, which the macOS build lacks, a device gets **one** vector unless its driver asked for more with `configureInterrupts(kIOInterruptTypePCIMessagedX, required, requested)` or set `kIOPCIMSIFlagRespect` in `pci-msi-flags`. PCI-to-PCI bridges get one only when they are hot-plug ports with no INTx line. QEMU's root ports aren't, so they stay on INTx;
2. calls `allocateInterruptVectors`. Our override takes a range from IOPCIFamily's allocator (aligned to its size, as multiple MSI needs), finds the DeviceID and ITS, and maps the device (MAPD) and each vector i to EventID i and LPI 8192 + first + i (MAPTI, INV, SYNC). If the ITS refuses, the range goes back, and IOPCIFamily retries with fewer vectors or leaves the device on INTx;
3. asks for "GetMessagedInterruptAddress". The host bridge answers with the ITS's doorbell (`GITS_TRANSLATER`, base + 0x10040) and data 0. MSI ORs the vector number into the data, and MSI-X tables get data + i;
4. programs the MSI capability or the MSI-X table, and appends the vectors to the specifiers after INTx. Registering a messaged source enables MSI or MSI-X and sets the command register's INTx disable; unregistering the last one undoes both.

LPIs come back through `NeoDarwinGICv3`'s IRQ loop into `IOPCIMessagedInterruptController::handleInterrupt`, which runs the vector's handler. Vectors that share one controller vector (IOPCIFamily's default for MSI-X) are dispatched from it, and a disabled vector is replayed when it is enabled again. The controller sets `msi-lpi-base`, `msi-lpi-count` and `msi-device-id` on the device.

What changed for IOPCIFamily:

| Finding | Fix |
|---|---|
| Checkpoint 2's host bridge read every device's `IOInterruptSpecifiers` to log its INTx GSIV. That resolved the interrupts at publication, before any driver could ask for more MSI-X vectors: `configureInterrupts` then returns success without allocating anything | the log asks `_PRT` itself ("ResolvePCIInterrupt", as IOPCIFamily does) and lists each device's MSI and MSI-X capabilities (`msi 1`, `msi-x 65`) without resolving anything |
| `IOPCIMessagedInterruptController::enableVector` and `disableVectorHard` index the MSI-X table by the controller vector number. That is the device's vector index only when its vectors share one controller vector. With a vector per table entry, upstream unmasked entry `first + i`: another vector's entry, or a write past the table | the subclass passes the device's index (the vector's EventID) to the superclass |
| `_OSC` claimed no MSI | the host bridge probes the MSI controller before `_OSC` and sets the MSI bit when there is one (`_OSC control 0x1d of 0x3d with MSI`) |

## On QEMU

`virt` with `gic-version=3` has an ITS at 0x8080000 by default (`its=on`), in the MADT and the IORT. The PCI tests' log (`sbsa_pci_boot_test`), abridged:

```
NeoDarwinGICv3: LPIs: 8192 LPIs, configuration table at 0x43340000 inner shareable, write-back; enabled on 1 of 1 redistributors
NeoDarwinGICv3ITS: ITS 0: GITS_BASER0: device table, 8-byte entries, two-level, 1 x 64 KiB pages at 0x42990000, inner shareable, write-back
NeoDarwinGICv3ITS: ITS 0: GITS_BASER1: collection table, 8-byte entries, 1 x 64 KiB pages at 0x429a0000, inner shareable, write-back
NeoDarwinPCIMSI: ITS 0 at 0x8080000 (IIDR 0x0000043b): 16 DeviceID bits, 16 EventID bits, ITT entries 12 bytes, two-level device table; 1 collection by processor number
NeoDarwinPCIMSI: MSI and MSI-X on LPIs 8192-10239 through 1 ITS; 8192 LPIs, configuration table at 0x43340000 inner shareable, write-back
NeoDarwinPCIHostBridge: \_SB.PCI0: _OSC control 0x1d of 0x3d with MSI; DMA coherent (_CCA, 64 address bits); requester IDs to ITS group
NeoDarwinPCIHostBridge: 0000:00:03.0 1b36:0010 class 010802 bar0 mem 0x8000104000+0x4000 INTA gsiv 38 msi-x 65
NeoDarwinPCIHostBridge: 0000:00:04.0 1234:11e8 class 00ff00 bar0 mem 0x10400000+0x100000 INTA gsiv 35 msi 1
NeoDarwinPCIMSI: 0000:00:04.0: MSI 1 vector -> LPI 8192, DeviceID 0x20 on ITS 0
NeoDarwinPCIEduTest: 00:04.0: edu 0x010000ed: INTA on GSIV 35 (level) reached its handler in 62 us: status 0x4e440000, 1 interrupt
NeoDarwinPCIEduTest: 00:04.0: edu 0x010000ed: MSI on LPI 8192 (DeviceID 0x20, EventID 0) reached its handler in 76 us: status 0x4e440000, 1 interrupt, INTx disabled
NeoDarwinPCIMSI: 0000:00:03.0: MSI-X 2 of 65 vectors -> LPIs 8194-8195, DeviceID 0x18 on ITS 0
NeoDarwinPCIMSI: 0000:02:00.0: MSI 1 vector -> LPI 8193, DeviceID 0x200 on ITS 0
NeoDarwinPCIEduTest: 02:00.0: edu 0x010000ed: MSI on LPI 8193 (DeviceID 0x200, EventID 0) reached its handler in 24 us: status 0x4e440000, 1 interrupt, INTx disabled
NeoDarwinPCINVMeTest: 00:03.0: NVMe 1.4: MSI-X 2 of 65 vectors on LPIs 8194-8195 (DeviceID 0x18): vector 0 (admin) in 64 us, vector 1 (I/O queue 1) in 163 us; queues 0x003f003f
NeoDarwinPCIHostBridge: \_SB.PCI0: segment 0: 10 devices (2 PCI-to-PCI bridges) on buses 0-2, 9 with INTx, 9 capable of MSI or MSI-X
```

Times run from the device's doorbell to the handler's first instruction, under TCG: tens of microseconds, the same order as INTx. LPI numbers follow the order in which drivers start. Devices with no driver yet (virtio, the NIC) have no vectors allocated: IOPCIFamily allocates when a driver first asks.

- **edu** (`NeoDarwinPCIEduTest`): after its INTA test it registers the MSI source, raises the interrupt and waits for the handler. It logs the LPI, the DeviceID and whether IOPCIFamily disabled INTx.
- **NVMe** (`NeoDarwinPCINVMeTest`, probe score 0; it drives only QEMU's NVMe model, vendor 0x1b36, unless `nd_pci_nvme_test=1` allows any controller, and `nd_pci_nvme_test=0` turns it off): it asks for two MSI-X vectors before anything resolves the device, resets the controller and builds an admin queue pair (completions on vector 0). It sends Get Features (Number of Queues), whose completion raises vector 0. It then creates I/O completion queue 1 on vector 1 and I/O submission queue 1, and sends a Flush through them, whose completion raises vector 1. Two EventIDs, two LPIs, two handlers. Then it disables the controller again.
- **virtio-blk** is MSI-X capable (2 vectors, or 1 + one per CPU queue with `-smp`). It gets vectors when P1-10's driver asks.

## Tests

| Target | Machine | Asserts |
|---|---|---|
| `//kernel/neodarwin/pci:iort_test` | host | MADT ITS and IORT routes on QEMU's and the Q8B's tables: 0x20 → 0x20 directly; Q8B 2:01:00.0 → SMMUv3 0x14f80000 stream 0x20100 → DeviceID 0xa0100; segments without a root complex and truncated tables refused |
| `//kernel:sbsa_pci_boot_test` | `virt` (DS = 1) | the checkpoint 2 lines, plus the ITS and LPI lines, `_OSC ... with MSI`, both edus' MSI, NVMe's two MSI-X vectors |
| `//kernel:sbsa_secure_pci_boot_test` | `virt,secure=on`, TF-A (DS = 0) | the same: Group 1 Non-secure LPIs |
| `//kernel:sbsa_smp_pci_boot_test` | `virt`, `-smp 4` | LPIs enabled on 4 of 4 redistributors, 4 collections, MSIs delivered on the boot CPU's collection |
| `//kernel:sbsa_pci_smmu_boot_test` | `virt,iommu=smmuv3`, `nd_gic_lpi_nc=1 nd_its_flat=1` | requester IDs through the SMMUv3 to the ITS; non-cacheable tables and command queue with cache maintenance; a flat 512 KiB device table; the same MSIs |
| `//kernel:sbsa_pci_nomsi_boot_test` | `virt`, `nd_pci_msi=0` | no ITS or LPI set up, `_OSC` without MSI, both edus and every device on INTx |

The harness grew `--machine-opt OPT` (appended to `-M`) for the SMMU test.

## Limits

- **Affinity.** Every MSI targets the boot CPU's collection. The per-CPU collections exist, but nothing moves an LPI yet (MOVI, or `IOInterruptController` affinity).
- **No GICv4.** No vPEs, and no vLPIs or doorbells; GICv4.1's vPE and vSGI tables are left invalid.
- **No DirectLPI.** Everything goes through the ITS; `GICR_INVLPIR` and friends aren't used.
- **Vectors.** 2048 LPIs are handed out, from tables that cover 8192. Multiple MSI (as opposed to MSI-X) is still one vector per device unless the driver sets `kIOPCIMSIFlagRespect`, as IOPCIFamily's macOS policy has it.
- **SMMU.** Not programmed: MSIs and DMA depend on bypass. An SMMU that firmware leaves translating or aborting would drop MSI writes, and devices would need `nd_pci_msi=0` (INTx).
- **Several ITSs** work in principle: each MADT ITS is set up, and a device goes to the one its IORT group names. Only one-ITS machines have been tried.
- **Non-coherent devices.** The NVMe test cleans its submission entries when the device isn't `dma-coherent`, but doesn't invalidate completions before reading them. P1-10's drivers must do both.

## What the Radxa Dragon Q8B will stress

- **GIC-600's ITS at 0x17a40000** (MADT translation ID 0), 8 redistributors at 0x17a60000. Whether its tables are coherent is unknown: the shareability read-back decides, and the log says which way it went (`nd_gic_lpi_nc=1` has exercised the other path on QEMU). The tables are ordinary kernel memory below 2^48. The board has 40-bit physical addresses and its RAM is below them; the GIC reads the tables itself, not through PCIe, so the 36-bit PCIe DMA limit doesn't apply to them. `GITS_TYPER.PTA` decides how collections are addressed; both encodings are implemented, but only PTA 0 (QEMU) has run.
- **DeviceIDs up to 0xeffff** (segments 0–6 through the SMMU at 0x80000 + (s << 16 | RID)): the ITS needs at least 20 DeviceID bits. A two-level device table keeps that to a level-1 page plus a 64 KiB page per 8192 DeviceIDs in use. If `Indirect` doesn't stick, the flat table is 8 MiB of contiguous memory, allocated at boot, which may fail. MAPD fails with a log line if the ITS has fewer DeviceID bits.
- **The SMMUv3 at 0x14f80000**, which the firmware reserves. If it isn't in bypass, MSI writes are translated or aborted and the MSIs vanish, while INTx keeps working. On the first boot, the NVMe test's line shows which (a timeout on vector 0 after `NeoDarwinPCIMSI: 0002:01:00.0: MSI-X 2 of ... via SMMUv3 at 0x14f80000 as stream 0x20100 (bypass)`). `nd_pci_msi=0` is the fallback, and an SMMUv3 driver (at least a bypass STE configuration) the fix.
- **`_OSC`** now claims MSI in the support field, so the Q8B may grant hot plug too, which it held back without MSI (`pci.md`).
- **NVMe on segment 2**: the NVMe test driver doesn't touch it by default, since that disk holds the board's other systems. Boot with `nd_pci_nvme_test=1` to run it there (read-only admin commands and a Flush on namespace 1); it is then the first MSI-X proof on the board.

## For P1-10 (virtio-blk and NVMe drivers)

- Before reading anything interrupt-related, call `configureInterrupts(kIOInterruptTypePCIMessagedX, required, requested)` for one vector per queue. Without it, an MSI-X device gets one shared vector, and IOPCIFamily's dispatch for shared vectors reads the PBA, which is clear once a message has been sent. Nothing else in the kernel reads a device's specifiers before its driver does.
- Sources: with `configureInterrupts`, the MSI-X vectors are sources 0 to n−1 and there is no INTx source. Otherwise INTx is source 0 and MSI or MSI-X follows. Look for `kIOInterruptTypePCIMessagedX` with `getInterruptType`.
- `IOInterruptEventSource` (not a filter) is enough for MSI-X: vectors are edge-triggered and not shared.
- Honour `dma-coherent` (cache maintenance both ways) and `dma-address-bits`, and allocate queues with `kIOMemoryHostPhysicallyContiguous | kIOMemoryMapperNone` within them, as the NVMe test does.
- Keep INTx working for `nd_pci_msi=0`: `configureInterrupts` then fails with `kIOReturnUnsupported`.
