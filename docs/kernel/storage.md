<!-- SPDX-License-Identifier: BSD-2-Clause -->
# Storage: IOStorageFamily, virtio-blk and the root by boot-uuid (P1-10)

**P1-10, checkpoint 1.** The SBSA kernel has a block storage stack: Apple's open **IOStorageFamily**, the family every macOS disk driver publishes through, with a NeoDarwin **virtio-blk** driver under it. neoboot boots from a GPT disk and names the root by **boot-uuid**; Apple's open **AppleFileSystemDriver** finds that partition and the kernel roots on `/dev/disk0s2`. The launchd/zsh session of P1-08 now boots from a virtio-blk disk on QEMU `virt` (`//kernel:sbsa_disk_boot_test`), writes to it, and finds what it wrote after a reboot. The HFS+ ramdisk roots (md0) are unchanged. Checkpoint 2 adds an NVMe driver under the same stack (below, "For checkpoint 2"); `sbsa-ref`, P1-10's exit machine, has no firmware in the tree yet, so QEMU `virt` stands in.

## Pieces

| Where | What |
|---|---|
| `@apple_iostoragefamily` (`MODULE.bazel`) | IOStorageFamily **331**, from the macOS 26.0 release set (distribution-macOS `macos-260`), pinned by SHA-256 (`kernel/upstream.lock`). APSL 2.0 (`THIRD_PARTY_NOTICES.md`). Eleven sources, and the headers overlaid at `iokit/ndstorage/include/IOKit/storage`, where kexts find them in the SDK |
| `@apple_filesystemdriver` | AppleFileSystemDriver **31**, same release set, APSL 2.0: turns `boot-uuid` into the `boot-uuid-media` resource `IOFindBSDRoot` waits for |
| `kernel/neodarwin/storage/NeoDarwinVirtioBlock.cpp` | the virtio-blk driver, an `IOBlockStorageDevice` |
| `kernel/neodarwin/storage/nd_virtio.h` | virtio 1.x PCI transport, split virtqueue and block device layouts |
| `kernel/neodarwin/storage/NeoDarwinStorageDMA.h` | the DMA policy PCI storage drivers share: `dma-coherent`, `dma-address-bits`, queue memory, `IODMACommand` |
| `kernel/neodarwin/storage/compat` | `APFS/APFSConstants.h`, `uuid/namespace.h`, `hfs/hfs_format.h` for AppleFileSystemDriver |
| patches 0025–0028 | build lists, search paths and personalities (0025, 0027); IOBlockStorageDriver without DriverKit (0026); no sealed-root check on SBSA (0028). Patch 0012 is dropped |
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

**Security gap (patch 0028).** Skipping the sealed-root check means NeoDarwin's root volume is not authenticated: the kernel trusts whatever HFS+ volume matches `boot-uuid`. Apple's answer is the signed APFS snapshot (SSV), which NeoDarwin can't build. A replacement (a signed root hash that neoboot verifies and the kernel checks, or ZFS with verified boot environments) belongs with code-signing policy (P1-15) and the ZFS root (Phase 3); until then the root is as trustworthy as the disk it's on.

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

**The root device and the remount.** The kernel mounts `/dev/disk0s2` read-only as `root_device`; launchctl's `mount -uw /` finds the `/dev` block device whose `st_rdev` is the root's `st_dev` (`blockDevice(holding:)`), which is `/dev/disk0s2` as it was `/dev/md0`, and passes it to HFS. After the update mount `f_mntfromname` is `/dev/disk0s2`, which `df /` shows. The volume is **journaled HFS+**: HFS refuses a read-write mount of a dirty volume without a journal (`hfs_mounthfsplus`: "cannot mount dirty non-journaled volumes"), and a VM that is switched off leaves it dirty; with the journal, the next boot replays it.

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
| `//kernel:sbsa_pci_boot_test` and the other PCI tests | `virt` | also both blank virtio-blk disks driven (1af4:1001 on the root bus, 1af4:1042 behind a root port) over MSI-X; with `nd_pci_msi=0`, over INTx |
| `//boot/neoboot:*`, `//tools/dtdump:*` | host and QEMU | unchanged; the ramdisk boots keep `rd=md0` and no boot-uuid |

The harness's `--disk IMAGE` boots a raw image as the only drive (no vvfat ESP; EDK2 boots the image's ESP), `--disk-in-place` lets a run write to the image itself, `--disk-device` chooses the QEMU device; `qemu_disk_reboot_test.sh` runs the harness twice on one copy. `df` joined the base (`file_cmds`' `df`, with libutil and libxo); `mount` (diskdev_cmds) is not in the base yet, so the file system's type is shown by the kernel's HFS line rather than from the shell.

## Limits

- One request queue; no multiqueue, indirect descriptors, event suppression, discard or write zeroes. Eight requests of up to 116 KiB in flight on QEMU.
- No legacy (pre-1.0) virtio interface.
- No hot unplug handling beyond `stop` resetting the device.
- The root must be HFS+ on GPT; boot-uuid by HFS+ volume UUID works through AppleFileSystemDriver but isn't tested.
- The ESP is not mounted by the running system.

## For checkpoint 2 (NVMe)

- A second `IOBlockStorageDevice`, `NeoDarwinNVMe…`, matched by `IOPCIClassMatch 0x01080200&0xffffff00` with a probe score above `NeoDarwinPCINVMeTest`'s 0; likely a controller driver with one block storage nub per namespace, each registered for IOBlockStorageDriver, as Apple's NVMe family does.
- The same `NDStorageDMA` for queues and PRP lists; PRPs (4 KiB pages) instead of virtio's free-form segments, so tell IOBlockStorageDriver `IOMinimumSegmentAlignmentByteCount` and page-sized segments, or build PRP lists per slot.
- MSI-X: admin queue on vector 0, one I/O queue pair per vector (`NeoDarwinPCINVMeTest` shows the sequence), INTx fallback through a filter reading nothing (NVMe has no ISR; check the completion queue's phase bits).
- Flush (`doSynchronize`) is NVMe Flush; `getWriteCacheState` from Identify Controller's VWC.
- A test image on `-device nvme` (the same `session_disk`, `--disk-device nvme,serial=nd0`): the rest of the boot is unchanged, the root is `disk0s2` by the same boot-uuid. On the Q8B the NVMe disk holds the board's other systems: keep writes off it unless asked, as the test driver does.
