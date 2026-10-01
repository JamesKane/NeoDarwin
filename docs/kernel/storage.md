<!-- SPDX-License-Identifier: BSD-2-Clause -->
# Storage: IOStorageFamily, virtio-blk, NVMe and the root by boot-uuid (P1-10)

**P1-10, checkpoint 1.** The SBSA kernel has a block storage stack: Apple's open **IOStorageFamily**, the family every macOS disk driver publishes through, with a NeoDarwin **virtio-blk** driver under it. neoboot boots from a GPT disk and names the root by **boot-uuid**; Apple's open **AppleFileSystemDriver** finds that partition and the kernel roots on `/dev/disk0s2`. The launchd/zsh session of P1-08 now boots from a virtio-blk disk on QEMU `virt` (`//kernel:sbsa_disk_boot_test`), writes to it, and finds what it wrote after a reboot. The HFS+ ramdisk roots (md0) are unchanged.

**P1-10, checkpoint 2.** A spec-driven **NVMe** driver under the same stack (below, "The NVMe driver"): the same session disk boots from QEMU's NVMe controller (`//kernel:sbsa_nvme_boot_test`), rooted on `disk0s2` by the same boot-uuid, with an I/O queue pair per CPU on its own MSI-X vector, or INTx. **On any controller that isn't QEMU's the driver is read-only unless the boot-arg `nd_nvme_rw=1` says otherwise** ("Real disks: the write policy"): the Radxa Dragon Q8B's only NVMe disk holds its other systems. On `sbsa-ref`, P1-10's exit machine (`qemu-sbsa-ref.md`), EDK2 SbsaQemu boots the same disk from NVMe and the session roots on it, twice (`//kernel:sbsa_ref_nvme_boot_test`).

## Pieces

| Where | What |
|---|---|
| `@apple_iostoragefamily` (`MODULE.bazel`) | IOStorageFamily **331**, from the macOS 26.0 release set (distribution-macOS `macos-260`), pinned by SHA-256 (`kernel/upstream.lock`). APSL 2.0 (`THIRD_PARTY_NOTICES.md`). Eleven sources, and the headers overlaid at `iokit/ndstorage/include/IOKit/storage`, where kexts find them in the SDK |
| `@apple_filesystemdriver` | AppleFileSystemDriver **31**, same release set, APSL 2.0: turns `boot-uuid` into the `boot-uuid-media` resource `IOFindBSDRoot` waits for |
| `kernel/neodarwin/storage/NeoDarwinVirtioBlock.cpp` | the virtio-blk driver, an `IOBlockStorageDevice` |
| `kernel/neodarwin/storage/nd_virtio_blk.h` | the block device's virtio layouts; the PCI transport and split virtqueue are shared with virtio-net since P1-19 (`kernel/neodarwin/virtio`: `nd_virtio.h`, `NeoDarwinVirtioPCI.h`, patch 0034; `network.md`) |
| `kernel/neodarwin/storage/NeoDarwinNVMeController.cpp`, `NeoDarwinNVMeNamespace.cpp`, `NeoDarwinNVMe.h` | the NVMe driver: the controller (an `IOService` on the `IOPCIDevice`) and one `IOBlockStorageDevice` per namespace |
| `kernel/neodarwin/storage/nd_nvme.h` | NVMe 1.4 registers, queue entries, opcodes and Identify offsets |
| `kernel/neodarwin/storage/NeoDarwinStorageDMA.h` | the DMA policy PCI storage drivers share: `dma-coherent`, `dma-address-bits`, queue memory, `IODMACommand` |
| `kernel/neodarwin/storage/compat` | `APFS/APFSConstants.h`, `uuid/namespace.h`, `hfs/hfs_format.h` for AppleFileSystemDriver |
| patches 0025–0029 | build lists, search paths and personalities (0025, 0027, 0029: NVMe, retiring the NVMe MSI-X test driver of 0024); IOBlockStorageDriver without DriverKit (0026); no sealed-root check on SBSA (0028). Patch 0012 is dropped |
| `boot/neoboot/Sources/BootDisk.swift`, `Portable/GPT.swift` | neoboot reads the boot disk's GPT and chooses the boot-uuid |
| `tools/gptimage`, `rules/disk.bzl` | the GPT disk image writer and rule; `//images:session_disk` |
| `tools/efi/qemu_efi_test.sh --disk`, `qemu_disk_reboot_test.sh` | booting a disk image as the only drive, twice |

Everything is compiled into the kernel, as IOPCIFamily, ACPICA and HFS+ are, because `kcgen` links no kexts until M5; the overlay puts NeoDarwin's files at `iokit/ndstorage`, IOStorageFamily at `iokit/ndstorage/IOStorageFamily`, AppleFileSystemDriver at `iokit/ndstorage/AppleFileSystemDriver`.

## The stack

```
IOPCIDevice 1af4:1042                     (IOPCIFamily, pci.md)
  NeoDarwinVirtioBlock                    IOBlockStorageDevice
    IOBlockStorageDriver                  "device-type" Generic
      IOMedia  disk0 (whole)              "VirtIO Block Device Media"
        IOGUIDPartitionScheme
          IOMedia disk0s1  C12A7328-…     EFI System Partition
          IOMedia disk0s2  48465300-…     "NeoDarwin", UUID = the partition's unique GUID
        IOMediaBSDClient (on every IOMedia): /dev/disk0, /dev/rdisk0, /dev/disk0s1, …
```

On NVMe the top is two levels, a controller and its namespaces, as in Apple's (closed) IONVMeFamily; the rest is the same:

```
IOPCIDevice 1b36:0010 (class 010802)
  NeoDarwinNVMeController                 IOService: queues, interrupts, recovery
    NeoDarwinNVMeNamespace                IOBlockStorageDevice, one per active namespace
      IOBlockStorageDriver
        IOMedia disk0 (whole)             "NVMe QEMU NVMe Ctrl Media"
          ...
```

What IOStorageFamily builds, and what it doesn't:

| Source | On NeoDarwin |
|---|---|
| `IOStorage`, `IOMedia`, `IOBlockStorageDevice`, `IOBlockStorageDriver`, `IOPartitionScheme`, `IOFilterScheme` | built |
| `IOGUIDPartitionScheme`, `IOFDiskPartitionScheme` (MBR), `IOApplePartitionScheme`, `IOAppleLabelScheme` | built, with the Info.plist's probe scores 4000/3000/2000/1000 |
| `IOMediaBSDClient` | built: `/dev/diskN`, `/dev/rdiskN`, `diskNsM`, and the `DKIOC*` ioctls HFS uses |
| `IOUserBlockStorageDevice_kext`, `IORequest`, `IORequestsPool`, `BlockStorageDeviceDriverKit/` | not built: DriverKit's block storage dexts; there is no DriverKit (patch 0026) |

The Info.plist personalities are entries in `gIOKernelConfigTables` (patch 0025), with IOFDiskPartitionScheme's `Content Table` and AppleFileSystemDriver's `media-match` as the kext has them.

| Finding | Fix |
|---|---|
| IOBlockStorageDriver asks whether its provider is an `IOUserBlockStorageDevice`, a class generated from DriverKit's `.iig` | patch 0026: never; the driver always makes its own `IOPerfControlClient`, as for any in-kernel device |
| IOMediaBSDClient declares two `register` locals: an error in C++17 even under `-w` | `-Wno-register` on the family's objects |
| AppleFileSystemDriver includes the closed APFS kext's `APFS/APFSConstants.h`, Libc's private `uuid/namespace.h` and the SDK's `hfs/hfs_format.h` | compat headers: APFS's published role values and registry keys (never matched: no APFS), the HFS UUID name space, and hfs-704's own `hfs_format.h` |
| Headers are in the archive's root, included as `<IOKit/storage/…>` | a second overlay of the same archive at `iokit/ndstorage/include/IOKit/storage` (a genrule can't be overlaid: the overlay keeps each file's path in its package, and a generated file's owner is its rule) |
| `bsd_init()` panicked "rootvp not authenticated after mounting": an Apple silicon macOS kernel requires a sealed system volume on any non-ramdisk root | patch 0028: not on `GENERIC_ARM64_PLATFORM`; the ramdisk roots never reached the check |

**Security gap (patch 0028).** Skipping the sealed-root check means NeoDarwin's root volume is not authenticated: the kernel trusts whatever HFS+ volume matches `boot-uuid`. Apple's answer is the signed APFS snapshot (SSV), which NeoDarwin can't build. A replacement (a signed root hash that neoboot verifies and the kernel checks, or ZFS with verified boot environments) belongs with ndsign (P2-01) and the ZFS root (Phase 3); until then the root is as trustworthy as the disk it's on. P1-15's trust cache narrows the gap without closing it (`amfi-provider.md` §4.6). Under enforcement the kernel runs only code whose cdhash the image's trust cache lists, so a binary or library changed or added on the root is refused, even by a process that can write the disk while the system runs. Data, configuration and scripts on the root are still unauthenticated. So is the trust cache itself, on the ESP: someone who can write the disk offline can rewrite it along with the root.

**Patch 0012 is dropped.** It let `publishHiddenMedia()` run without an IOMedia class, when md0 was the root and no storage family existed. The class is now always built in, so upstream's code runs as on a Mac: with md0 there is no root IOMedia (`setRootMedia(NULL)`), and every hidden IOMedia is published. md0 itself stays a BSD memory device (`bsd/dev/memdev.c`), not an IOMedia; nothing needs it to be one.

## The virtio-blk driver

`NeoDarwinVirtioBlock` matches `IOPCIMatch` `0x10421af4 0x10011af4`: modern virtio-blk, and transitional (QEMU's default `virtio-blk-pci` on the root bus), whose modern capabilities it uses. It is an `IOBlockStorageDevice` itself, one per disk, and `registerService()`s once the device runs; IOBlockStorageDriver matches it.

**Transport: modern PCI only** (virtio 1.2 §4.1.4). The vendor-specific capabilities (ID 9) give the common configuration, notification, ISR and device configuration regions, each a BAR and an offset; each BAR is mapped once (`mapDeviceMemoryWithRegister`). A transitional device's legacy I/O BAR is not used, and a device without `VIRTIO_F_VERSION_1` is refused with a log line. The legacy interface would need a second register layout and the legacy ring alignment for no device QEMU or a board presents that lacks the modern one. Tests use `virtio-blk-pci,disable-legacy=on` (1af4:1042); the PCI tests' transitional disks (1af4:1001) go through the same modern path.

**Initialisation** (§3.1.1): reset (status 0, polled), ACKNOWLEDGE, DRIVER, features, FEATURES_OK (read back), configuration, queue 0, interrupts, queue enable, DRIVER_OK.

| Feature | Taken | Use |
|---|---|---|
| `VERSION_1` (32) | required | modern device |
| `ACCESS_PLATFORM` (33), `ORDER_PLATFORM` (36) | if offered | device addresses are physical (no IOMMU driver); barriers are already the platform's |
| `SIZE_MAX` (1), `SEG_MAX` (2) | if offered | segment size and count limits |
| `BLK_SIZE` (6) | if offered | the logical block size IOStorageFamily sees (sectors stay 512 bytes in requests) |
| `RO` (5) | if offered | `reportWriteProtection`, writes refused |
| `FLUSH` (9), `CONFIG_WCE` (11) | if offered | `doSynchronize` sends `VIRTIO_BLK_T_FLUSH`; the write cache's state is `writeback`, readable and settable |
| indirect descriptors, event index, packed ring, multiqueue, discard, write zeroes | no | |

The configuration is read until `config_generation` is stable. QEMU 11.1 offers, and the driver takes, `0x100000a44` (VERSION_1, CONFIG_WCE, FLUSH, BLK_SIZE, SEG_MAX).

**The queue.** One split virtqueue (§2.7), up to 256 entries, in physically contiguous memory: the descriptor table and the available ring on the first pages, the used ring (which the device writes) on its own page. The descriptor table is cut into fixed **chains of 32**, one per request slot (8 slots with QEMU's 256): a 16-byte header the device reads, up to 30 data segments, a status byte it writes. Each slot's header and status live in their own 128-byte line of a separate page. Nothing is allocated per request: a request takes a free slot, fills its chain, and puts the chain's head on the available ring; a request that finds none waits in a list that completions drain. IOBlockStorageDriver is told the limits (`IOMaximumSegmentCountRead/Write` 30, `IOMaximumByteCountRead/Write` 29 pages, `IOMaximumSegmentByteCount*` from `SIZE_MAX`), so every request it passes fits a chain.

**Requests** run on the driver's work loop: `doAsyncReadWrite` enters through a command gate, completions arrive through the queue's interrupt event source on the same loop, and `IOStorage::complete` is called from there. `doSynchronize` (cache flush) sleeps on the gate until its slot completes. Status `VIRTIO_BLK_S_OK` completes with the byte count; `UNSUPP` becomes `kIOReturnUnsupported`, anything else `kIOReturnIOError`. Ordering: descriptors and ring entry, `DMB OSHST` (or a clean to the point of coherency), the available index, `DSB SY`, the notification write; `DMB OSHLD` between reading the used index and its entries.

**Interrupts** (`gic-its.md`, "For P1-10"). Before anything resolves the device's interrupts, the driver asks IOPCIFamily for MSI-X (`configureInterrupts(kIOInterruptTypePCIMessagedX, 1, 2)`): with two vectors, vector 0 is configuration changes and vector 1 the queue; with one, both share it. The vectors are written to `config_msix_vector` and `queue_msix_vector` after the sources are registered (which enables MSI-X), and read back. Without MSIs (`nd_pci_msi=0`, no ITS) the queue is on INTx: level and possibly shared, through an `IOFilterInterruptEventSource` whose filter reads the ISR status (which also deasserts the line) and claims the interrupt only if a bit was set.

**DMA** (`NeoDarwinStorageDMA.h`, from what `pci.md` puts on every `IOPCIDevice`):

- `dma-address-bits` bounds queue memory (`IOBufferMemoryDescriptor::inTaskWithPhysicalMask`, `kIOMemoryHostPhysicallyContiguous | kIOMemoryMapperNone`) and is the `IODMACommand`'s address width, so data buffers above it are bounced; it is also `IOMaximumSegmentAddressableBitCount`.
- `dma-coherent` false: queue memory is cleaned after allocation; everything the CPU writes for the device (descriptors, ring entries, headers) is cleaned to the point of coherency before the notification, and the used ring and status bytes are cleaned and invalidated before they are read (their lines are never dirty, so nothing is written back over the device's data). Data buffers go through `IODMACommand::kNonCoherent`, whose `prepare` and `complete` do the maintenance. Coherent devices use `kUnmapped` commands and barriers only.
- One `IODMACommand` per slot, 64-bit segments, no system mapper (there is no IOMMU driver; `pci.md`).

**Reported to IOStorageFamily:** block size and count; not ejectable or removable; media present; write protection from `RO`; vendor "VirtIO", product "Block Device"; `Protocol Characteristics` Virtual Interface, Internal. The boot log:

```
NeoDarwinVirtioBlock: 00:02.0: virtio-blk 1af4:1042: 464896 512-byte blocks (227 MiB); features 0x100000a44; queue 256, 8 slots of 30 segments; MSI-X 2 vectors (LPIs 8192-8193); write cache on; DMA coherent, 64 address bits
```

## The NVMe driver

Written from the NVM Express Base Specification 1.4c and the NVM command set (Read, Write, Flush); no Linux or BSD code. `NeoDarwinNVMeController` takes over the personality of P1-09's NVMe MSI-X test driver (`IOPCIClassMatch 0x01080200&0xffffff00`, probe score 1000, patch 0029), which is retired: the driver's own vectors are the MSI-X proof now.

### Real disks: the write policy

The Radxa Dragon Q8B's only NVMe disk (a Kingston SNV2S1000G) holds its FreeBSD and Ubuntu roots. NeoDarwin must never write to it by accident, so:

| Controller | Default | Boot-arg |
|---|---|---|
| QEMU's model (PCI vendor 0x1b36) | read-write | `nd_nvme_rw=0` makes it read-only (the tests of the policy) |
| any other | **read-only** | `nd_nvme_rw=1` makes every controller writable |
| any | — | `nd_nvme=0`: the driver doesn't attach at all |

Read-only means three locks. Each namespace reports itself **write-protected**, so its IOMedia is not writable: a file system mounts it read-only and a write open of `/dev/diskN…` fails (`EACCES`, IOMediaBSDClient). A write that still reaches the namespace is refused (`kIOReturnNotWritable`), and `issue()` refuses any opcode but Read on a read-only controller, whoever asks. `doSynchronize` sends nothing. What a read-only controller does receive: a **controller reset** (CC.EN = 0 then 1; every OS and firmware does this at boot, and it doesn't touch the media), **Identify** (controller, active namespace list, namespace), **Get Features** (Volatile Write Cache), **Set Features Number of Queues** and **Create I/O Completion/Submission Queue** (queue set-up, lost at the next reset), and **Read**. Never Write, Flush, Write Zeroes, Dataset Management, Format, Sanitize, firmware commands or any other Set Features. The log's second controller line names the policy and why (`read-only: not QEMU's controller; nd_nvme_rw=1 allows writes`). A later item can relax it, for example writable only when the boot-uuid's partition is on that controller.

### Bring-up (§7.6.1)

1. MSI-X, before anything resolves the device's interrupts (`gic-its.md`, "For P1-10"): `configureInterrupts(kIOInterruptTypePCIMessagedX, 1, 1 + n)`, n = the CPUs up to 8. The vectors granted are counted with `getInterruptType`; each gets an `IOInterruptEventSource`. With none (`nd_pci_msi=0`, no ITS), INTx on source 0 through a filter (below).
2. BAR0 mapped; **CAP**: MQES (queue size), DSTRD (doorbell stride), TO (the ready timeout, 500 ms units), CSS (the NVM command set is required), MPSMIN (only 4 KiB pages: the driver uses CC.MPS = 0 whatever the kernel's 16 KiB page).
3. **Reset**: CC.EN = 0 (after letting a controller that is still becoming ready finish), CSTS.RDY = 0 within CAP.TO.
4. The **admin queue** (32 entries) in AQA/ASQ/ACQ; CC = NVM command set, 4 KiB pages, round robin, 64-byte SQ and 16-byte CQ entries, EN; CSTS.RDY = 1 within CAP.TO (CSTS.CFS fails at once).
5. **Identify Controller**: model, serial, firmware (also IOKit properties on the controller and in the namespace's Device Characteristics), MDTS, VER, NN, VWC; SQES/CQES must allow 64 and 16 bytes. **Get Features VWC** if there is a volatile write cache.
6. **Set Features Number of Queues**, then per pair **Create I/O Completion Queue** (physically contiguous, interrupts on, its vector) and **Create I/O Submission Queue**.
7. **Identify** the active namespace list (NVMe 1.1 and later; else 1..NN), then each **namespace**: NSZE blocks, the LBA format FLBAS selects (LBADS 9 to 16: 512 to 64 KiB blocks; 512 and 4096 are the usual ones), NSATTR's write protection. A format with metadata is skipped with a log line. Each gets a `NeoDarwinNVMeNamespace`, attached and started; it `registerService`s and IOBlockStorageDriver takes it from there.

Admin commands are one at a time, in the command gate: during start the calling thread sleeps until vector 0's handler has reaped the completion (10 s limit); during recovery (on the work loop) the admin completion queue is polled. If an admin completion is in its queue but its interrupt never came, start fails with a line saying interrupts are not being delivered: the symptom of an SMMU that isn't in bypass (below).

### Queues, PRPs and requests

- **I/O queue pairs**: one per CPU, at most 8 and at most the MSI-X vectors less one (the admin queue's); with one vector or INTx, one pair. 64 entries each (fewer if MQES is smaller) and **32 command slots**; the command ID is the slot. A request takes a free slot on the pair of the CPU submitting it (`cpu_number()`), else any pair's, else waits in a list that completions drain. Everything runs on one work loop, so more pairs spread submissions and interrupts, not locking.
- **Transfers** up to MDTS, at most 512 KiB. IOBlockStorageDriver is told `IOMaximumByteCountRead/Write`, `IOMaximumSegmentCountRead/Write` (a page per segment, plus one), `IOMinimumSegmentAlignmentByteCount` 4 and `IOMaximumSegmentAddressableBitCount` (`dma-address-bits`).
- **PRPs** (§4.3), 4 KiB granular: PRP1 is the first byte, then one entry per 4 KiB page; the second goes in PRP2, more in the slot's **preallocated PRP list** (a power of two of bytes, 2 KiB for 512 KiB, so no list crosses a page; no chaining is ever needed). Each slot's `IODMACommand` (`NDStorageDMA::newPRPCommand`) is specified so that segments fit PRPs: the first starts on a dword, every later one on a 4 KiB boundary, and IODMACommand cuts a segment at the boundary where the next one must start; what still doesn't fit (a segment before the last that ends inside a page, from a multi-range descriptor) goes through IODMACommand's page-aligned double buffer (`synchronize(kForceDoubleBuffer)`), cleaned to the point of coherency by physical address when the device isn't coherent. Buffers above `dma-address-bits` are bounced as for virtio-blk.
- **Commands**: Read (0x02) and Write (0x01) with SLBA and NLB, FUA when IOStorageFamily asks (`kIOStorageOptionForceUnitAccess`); **Flush** (0x00) for `doSynchronize` when Identify reports a volatile write cache (VWC). The write cache state is Get Features' WCE; `setWriteCacheState` is unsupported (that would be a state-changing Set Features).
- **Status**: success completes with the byte count; Namespace Write Protected becomes `kIOReturnNotWritable`, LBA out of range `kIOReturnBadArgument`, Invalid Namespace `kIOReturnNoDevice`, anything else `kIOReturnIOError`, with a log line (type, code, DNR).
- **Ordering**: the entry is copied into the submission queue and cleaned (or `DMB OSHST` when coherent), `DSB SY`, the tail doorbell. On completion: the entry is invalidated (non-coherent), its phase tag read, `DMB OSHLD`, then the rest; the head doorbell once per batch.

### Interrupts

With MSI-X, **vector 0 is the admin queue** and **vector n is I/O queue n** (with one vector, everything on vector 0). Each I/O queue logs its first completion by interrupt once, which is the tests' proof that its vector works. Without MSIs, **INTx**: level and possibly shared, and NVMe has no interrupt status register, so the filter (primary interrupt context) looks at the entry at each completion queue's head for a flipped phase tag; if there is one it masks the controller's pin (INTMS) and claims the interrupt, and the work loop reaps and unmasks (INTMC).

### Timeouts and recovery

A timer runs every second while commands are in flight. **CSTS.CFS** (controller fatal status) resets the controller; a controller that **reads all ones** is gone and everything fails. A command older than **30 s** is first looked for in its completion queue (a lost interrupt is logged as such); if it is really stuck, the driver logs it and **resets the controller**: CC.EN = 0 stops its DMA, the commands that timed out fail (`kIOReturnTimeout`; a waiting flush is aborted), the others go back to the head of the waiting list, the queues are emptied and created again (admin commands polled, on the work loop), and the waiting requests restart. Three resets without a successful completion in between, or a controller that does not come back, fail everything and stop the driver. There is no Abort command: a reset is simpler and also covers a controller that no longer processes its admin queue.

### DMA

As for virtio-blk (`NeoDarwinStorageDMA.h`): queues, PRP lists and the Identify buffer physically contiguous and below `dma-address-bits`, cleaned after allocation when not coherent; data through the slots' `IODMACommand`s with the address limit and, when not coherent, `kNonCoherent` maintenance on prepare and complete.

### The log

```
NeoDarwinNVMeController: 00:02.0: NVMe 1.4, 1b36:0010, model "QEMU NVMe Ctrl", serial "nd0", firmware "11.1.1"; NN 256; MQES 2047, DSTRD 0, TO 7500 ms, MDTS 512 KiB
NeoDarwinNVMeController: 00:02.0: 1 I/O queue pair of 64 entries, 32 commands each, transfers up to 512 KiB; MSI-X 2 vectors (LPIs 8192-8193), admin queue on vector 0 (5 completions by interrupt)
NeoDarwinNVMeController: 00:02.0: write cache on; DMA coherent, 64 address bits; read-write: QEMU's controller
NeoDarwinNVMeNamespace: 00:02.0: namespace 1: 464896 512-byte blocks (227 MiB); read-write
NeoDarwinNVMeController: 00:02.0: I/O queue 1: first completion by its interrupt (MSI-X vector 1, LPI 8193)
```

### What the Q8B will exercise

QEMU's controller is coherent, 64-bit and behind no IOMMU; the Q8B's is none of those things, so its first boot is the real test. Boot it without `nd_nvme_rw` (read-only) and read the controller lines:

- **Segment 2, bus 1** (`0002:01:00.0`), behind the **SMMUv3** at 0x14f80000 (stream 0x20100, ITS DeviceID 0xa0100, `gic-its.md`). NeoDarwin has no SMMU driver and assumes the firmware left it in **bypass**. If it didn't, DMA and MSIs are translated or aborted: the first admin command times out, or completes without its interrupt (the "interrupts are not being delivered" line). `nd_pci_msi=0` takes MSIs out of the picture (INTx); DMA through a non-bypass SMMU needs an SMMUv3 driver.
- **36-bit DMA** (IORT): queues, PRP lists and Identify buffers are allocated below 64 GiB; data above it is bounced by IODMACommand. The log line must say `36 address bits`.
- **Coherence**: `_CCA` 1 on the host bridges, so `DMA coherent`; if a board says otherwise, the non-coherent paths (cleaning, invalidating, `kNonCoherent` commands, the double buffer's clean) run for the first time there.
- **Controller facts** to record: the Kingston's MQES, CAP.TO, DSTRD, MDTS (consumer controllers often have 128 or 256 KiB, below the 512 KiB cap), the MSI-X table size (hence the number of I/O queue pairs on 8 CPUs), VWC, and the LBA format (512-byte by default on the SNV2S, 4096 if it was reformatted). No Kingston quirks are known to the driver; it implements no quirk table.
- **The partitions**: IOGUIDPartitionScheme should list the disk's GPT (the FreeBSD and Ubuntu partitions, and their ESP) as read-only IOMedia. Nothing mounts them: AppleFileSystemDriver only publishes the HFS+ partition whose UUID is the boot-uuid.

## The root by boot-uuid

```
neoboot                  GPT of the boot disk → HFS+ partition 2 → unique GUID → /chosen boot-uuid
IOFindBSDRoot            /chosen boot-uuid → publishResource("boot-uuid") → waits for IOResources "boot-uuid-media"
AppleFileSystemDriver    matches IOResources boot-uuid; watches IOMedia leaves with an HFS+ (or APFS, Apple_Boot, Recovery) content hint;
                         an IOMedia whose "UUID" equals it (or whose HFS+ volume UUID does) → publishResource("boot-uuid-media", media)
IOFindBSDRoot            "BSD root: disk0s2, major 1, minor 2" → HFS+ mounts it read-only
launchctl bootstrap      mount -uw / on the /dev block device holding / (docs/base/session.md)
```

**Which UUID.** xnu itself compares nothing: `IOFindBSDRoot` (`IOKitBSDInit.cpp`, the `/chosen` `boot-uuid` branch, as `rd=uuid` with a `boot-uuid=` boot-arg) waits for the `boot-uuid-media` resource, which on macOS AppleFileSystemDriver publishes. It accepts the IOMedia's `UUID` property first, which IOGUIDPartitionScheme sets to the GPT entry's **unique partition GUID** (`uuid_unparse`, upper case), then the HFS+ **volume UUID** (the Finder information's 64-bit identifier made a version 3 UUID in the name space B3E20F39-F292-11D6-97A4-00306543ECAC, as `diskutil` shows it). NeoDarwin uses the **partition GUID**: the booter reads it from the GPT without parsing a file system, and it is fixed when the image is built (hdiutil makes a random volume identifier each time). Either works as a `boot.cfg` value.

**neoboot** (`BootDisk.swift`), before ExitBootServices:

1. **`boot-uuid=<UUID>` in `boot.cfg`** wins: it is passed as `/chosen boot-uuid`, and no `rd=md0` is appended even with a ramdisk. Malformed values are reported and ignored.
2. **A ramdisk** (`\NeoDarwin\ramdisk`): unchanged, `rd=md0` and no boot-uuid.
3. **Otherwise**, the boot disk's GPT: the loaded image's device path minus its last node (the ESP's Media/Hard Drive node) is the disk's; the Block I/O handle with exactly that path reads LBA 1 and the entry array (header and array CRCs checked, `Portable/GPT.swift`), and the first entry whose type is Apple HFS+ (48465300-0000-11AA-AA11-00306543ECAC) gives the unique GUID. `neoboot: root: HFS+ partition 2 of the boot disk, boot-uuid 50E3894E-…`. Without a GPT (QEMU's vvfat ESP is an MBR disk) or an HFS+ partition neoboot says so and passes none; the kernel then waits for a root.

The property is a NUL-terminated 36-character string (`dt-abi.md`); `DTCheck` refuses anything else, and `dtdump --boot-uuid` models it.

**The root device and the remount.** The kernel mounts `/dev/disk0s2` read-only as `root_device`. launchctl runs `fsck -q` and then `mount -uw /` (diskdev_cmds, `docs/base/session.md`). Both take the root's device from the fstab entry that Libinfo makes up for `/`: the `/dev` block device whose `st_rdev` is the root's `st_dev`, which is `/dev/disk0s2` as it was `/dev/md0`. mount passes it to `mount_hfs`. After the update mount `f_mntfromname` is `/dev/disk0s2`, which `df /` and `mount` show. The volume is **journaled HFS+**: HFS refuses a read-write mount of a dirty volume without a journal (`hfs_mounthfsplus`: "cannot mount dirty non-journaled volumes"), and a VM that is switched off leaves it dirty; with the journal, the next boot replays it.

## The disk image

`//images:session_disk` (`rules/disk.bzl`, `tools/gptimage`), 512-byte blocks:

| LBA | Contents |
|---|---|
| 0 | protective MBR, one 0xEE partition over the disk |
| 1, 2–33 | GPT header, 128 entries of 128 bytes |
| 2048 (1 MiB) | partition 1: EFI System Partition, FAT32 (hdiutil): `\EFI\BOOT\BOOTAA64.EFI` (neoboot), `\NeoDarwin\kernelcache`; no ramdisk, no `boot.cfg` |
| next MiB | partition 2: `NeoDarwin`, type Apple HFS+, the journaled 192 MiB `session_root_volume` (the session system of `session_root`) |
| last 33 blocks | backup entries and header |

Partitions start on MiB boundaries and are their images rounded up to a MiB. **GUIDs are name-based** (version 5, SHA-1) in a NeoDarwin name space (UUIDv5 of the DNS name space and `gptimage.neodarwin`), of the target's label plus `#disk` or `#partition-N`: the root partition's GUID, and so the boot-uuid, is the same on every build. `bazel build //images:session_disk --output_groups=+uuids` writes them to `session_disk.uuids`:

```
disk FBD5E4A4-C478-5801-973B-F0B4B28C3152
1 2D110EC1-70F2-562D-A6A6-B33E49739F5F esp EFI System Partition
2 50E3894E-4559-5613-B313-CDF980E940F0 hfs NeoDarwin
```

The file system contents themselves are not byte-reproducible (hdiutil's timestamps and volume identifiers). The GPT code is neoboot's `Portable/GPT.swift`, compiled for the host as dtdump compiles the device-tree code, so the writer and the reader share the layout and the CRC.

## Tests

| Target | Machine | Asserts |
|---|---|---|
| `//kernel:sbsa_disk_boot_test` | `virt`, one `virtio-blk-pci,disable-legacy=on` drive: `//images:session_disk` | first boot: neoboot's boot-uuid line, `rooting via boot-uuid from /chosen`, the driver's line with two MSI-X vectors, `AppleFileSystemDriver: publishing boot-uuid-media=disk0s2`, `BSD root: disk0s2`, HFS mounted; after login `df /` shows `/dev/disk0s2`, a file is written and synced (zsh's `sync` builtin). Second boot of the same image: the file's contents |
| `//kernel:sbsa_secure_disk_boot_test` | `virt,secure=on` with TF-A (DS = 0) | the first boot's lines and `df` |
| `//kernel:sbsa_disk_intx_boot_test` | `virt`, `//images:session_disk_intx` (the same disk with `boot.cfg` `nd_pci_msi=0` on its ESP) | the disk on INTx; rooted on `disk0s2`; `df` |
| `//kernel:sbsa_pci_boot_test` and the other PCI tests | `virt` | also both blank virtio-blk disks driven (1af4:1001 on the root bus, 1af4:1042 behind a root port) over MSI-X; with `nd_pci_msi=0`, over INTx. The blank NVMe disk (00:03.0): the controller and namespace lines, the admin queue on vector 0 and an I/O queue's first completion on its vector (the MSI-X proof the retired test driver gave); four pairs on five vectors in `sbsa_smp_pci_boot_test`; INTx with `nd_pci_msi=0` |
| `//kernel:sbsa_ref_nvme_boot_test` | `sbsa-ref` (TF-A, SbsaQemu at EL2, DRAM at 1 TiB, four CPUs), the same disk on NVMe at 00:03.0 | EDK2's NVMe boot of neoboot, four I/O queue pairs on MSI-X through the ITS (via the SMMUv3 in bypass), rooted on `disk0s2` by boot-uuid, `df /`, a file written and read back in a second boot: P1-10's exit |
| `//kernel:sbsa_nvme_boot_test` | `virt`, `//images:session_disk` on `nvme,serial=nd0` (1b36:0010) | the controller's lines (one I/O pair, two MSI-X vectors, read-write), namespace 1, I/O queue 1's first completion on vector 1, rooted on `disk0s2` by boot-uuid, `df /`, a file written and synced; the second boot reads it back |
| `//kernel:sbsa_secure_nvme_boot_test` | `virt,secure=on` with TF-A | the same first boot, to `df` |
| `//kernel:sbsa_smp_nvme_boot_test` | `virt`, four CPUs | four I/O queue pairs on five MSI-X vectors; three trees copied at once, then synced |
| `//kernel:sbsa_nvme_intx_boot_test` | `virt`, `//images:session_disk_intx` on NVMe (`nd_pci_msi=0`) | one pair on INTx, its first completion by INTx, rooted on `disk0s2`; a file written, synced and read |
| `//kernel:sbsa_nvme_readonly_test` | `virt`, the `session_root` ramdisk, `boot.cfg` `nd_nvme_rw=0`, `//images:session_disk` as a second disk on NVMe | the real-hardware policy on QEMU's controller: `read-only: nd_nvme_rw=0`, the namespace read-only; its partitions appear and read (the HFS+ signature `H+` at byte 1024 of `disk?s2`), and opening one for writing fails (permission denied) |
| `//boot/neoboot:*`, `//tools/dtdump:*` | host and QEMU | unchanged; the ramdisk boots keep `rd=md0` and no boot-uuid |

The harness's `--disk IMAGE` boots a raw image as the only drive (no vvfat ESP; EDK2 boots the image's ESP), `--disk-in-place` lets a run write to the image itself, `--disk-device` chooses the QEMU device; `qemu_disk_reboot_test.sh` runs the harness twice on one copy. `df` joined the base (`file_cmds`' `df`, with libutil and libxo); `mount` (diskdev_cmds) is not in the base yet, so the file system's type is shown by the kernel's HFS line rather than from the shell.

## Limits

- virtio-blk: one request queue; no multiqueue, indirect descriptors, event suppression, discard or write zeroes. Eight requests of up to 116 KiB in flight on QEMU.
- No legacy (pre-1.0) virtio interface.
- No hot unplug handling beyond `stop` resetting the device.
- The root must be HFS+ on GPT; boot-uuid by HFS+ volume UUID works through AppleFileSystemDriver but isn't tested.
- The ESP is not mounted by the running system.
- NVMe: no Abort (a timeout resets the controller), no shutdown notification (CC.SHN) at power-off, no power management, no namespace attach/detach or hot plug, no metadata or end-to-end protection formats, no Dataset Management (TRIM) or Write Zeroes, no SGLs, no multiple work loops (the I/O queue pairs share one), no quirk table. Namespaces past 16 are ignored.
- NVMe on anything but QEMU is read-only unless `nd_nvme_rw=1` (by design, above).
