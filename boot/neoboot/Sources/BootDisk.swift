// SPDX-License-Identifier: BSD-2-Clause
//
// The root by boot-uuid (docs/kernel/storage.md): when there is no ramdisk,
// neoboot reads the GUID partition table of the disk it was loaded from
// and passes the unique GUID of that disk's first HFS+ partition as
// /chosen boot-uuid. The kernel's IOGUIDPartitionScheme gives the
// partition's IOMedia the same GUID as its "UUID", AppleFileSystemDriver
// publishes that IOMedia as boot-uuid-media, and IOFindBSDRoot roots on its
// /dev/diskNsM.
//
// The disk is the device whose path is the ESP's without its last node,
// the Media/Hard Drive node of the partition neoboot came from; its Block
// I/O protocol reads LBA 1 (the header) and the partition entries.

import UEFI

/// What the boot disk's GPT says about the root.
enum BootDiskRoot {
    case found(index: Int, uuid: UUID16)
    /// neoboot wasn't loaded from a partition (a whole-disk file system).
    case notPartition
    case noDisk
    /// The disk has no valid GPT (an MBR disk, such as QEMU's vvfat).
    case noGPT
    case noHFS
}

extension Firmware {
    private static let devicePathGUID = EFI_GUID(Data1: 0x0957_6e91, Data2: 0x6d3f, Data3: 0x11d2,
                                                 Data4: (0x8e, 0x39, 0x00, 0xa0, 0xc9, 0x69, 0x72, 0x3b))
    private static let blockIOGUID = EFI_GUID(Data1: 0x964e_5b21, Data2: 0x6459, Data3: 0x11d2,
                                              Data4: (0x8e, 0x39, 0x00, 0xa0, 0xc9, 0x69, 0x72, 0x3b))

    private func devicePath(_ handle: EFI_HANDLE?) -> UnsafeRawPointer? {
        var guid = Self.devicePathGUID
        var path: UnsafeMutableRawPointer? = nil
        guard boot.pointee.HandleProtocol(handle, &guid, &path) == efiSuccess, let path else { return nil }
        return UnsafeRawPointer(path)
    }

    /// A device path's length up to its end node, and the offset of its last
    /// node before that; nil if it is malformed.
    private static func measure(_ path: UnsafeRawPointer) -> (length: Int, last: Int)? {
        var at = 0, last = -1
        for _ in 0..<64 {
            let type = path.load(fromByteOffset: at, as: UInt8.self)
            let length = Int(path.load(fromByteOffset: at + 2, as: UInt8.self)) | Int(path.load(fromByteOffset: at + 3, as: UInt8.self)) << 8
            if type == 0x7f { return (at, last) }
            guard length >= 4 else { return nil }
            last = at
            at += length
        }
        return nil
    }

    private static func same(_ a: UnsafeRawPointer, _ b: UnsafeRawPointer, _ count: Int) -> Bool {
        for i in 0..<count where a.load(fromByteOffset: i, as: UInt8.self) != b.load(fromByteOffset: i, as: UInt8.self) {
            return false
        }
        return true
    }

    func bootDiskRoot() -> BootDiskRoot {
        var loadedImageGUID = EFI_GUID(Data1: 0x5b1b_31a1, Data2: 0x9562, Data3: 0x11d2,
                                       Data4: (0x8e, 0x3f, 0x00, 0xa0, 0xc9, 0x69, 0x72, 0x3b))
        var loaded: UnsafeMutableRawPointer? = nil
        guard boot.pointee.HandleProtocol(image, &loadedImageGUID, &loaded) == efiSuccess, let loaded else { return .noDisk }
        let device = loaded.assumingMemoryBound(to: EFI_LOADED_IMAGE_PROTOCOL.self).pointee.DeviceHandle
        guard let esp = devicePath(device), let espShape = Self.measure(esp), espShape.last >= 0 else { return .notPartition }
        // Media (4) / Hard Drive (1): a partition of the disk before it.
        guard esp.load(fromByteOffset: espShape.last, as: UInt8.self) == 4,
              esp.load(fromByteOffset: espShape.last + 1, as: UInt8.self) == 1 else { return .notPartition }
        let prefix = espShape.last

        var blockIOGUID = Self.blockIOGUID
        var count: UInt64 = 0
        var handles: UnsafeMutablePointer<EFI_HANDLE?>? = nil
        guard boot.pointee.LocateHandleBuffer(UInt32(ByProtocol), &blockIOGUID, nil, &count, &handles) == efiSuccess,
              let handles else { return .noDisk }
        defer { _ = boot.pointee.FreePool(handles) }
        var disk: UnsafeMutablePointer<EFI_BLOCK_IO_PROTOCOL>? = nil
        for i in 0..<Int(count) {
            guard let path = devicePath(handles[i]), let shape = Self.measure(path), shape.length == prefix,
                  Self.same(path, esp, prefix) else { continue }
            var bio: UnsafeMutableRawPointer? = nil
            if boot.pointee.HandleProtocol(handles[i], &blockIOGUID, &bio) == efiSuccess, let bio {
                disk = bio.assumingMemoryBound(to: EFI_BLOCK_IO_PROTOCOL.self)
            }
            break
        }
        guard let disk, let media = disk.pointee.Media else { return .noDisk }
        let blockSize = Int(media.pointee.BlockSize)
        guard blockSize >= 512, blockSize <= Int(uefiPage), blockSize & (blockSize - 1) == 0 else { return .noGPT }

        // LBA 1, then the entry array: at most 1024 entries of up to 4 KiB.
        guard let headerPage = allocate(pages: 1) else { return .noDisk }
        defer { free(headerPage, pages: 1) }
        let header = UnsafeMutableRawPointer(bitPattern: UInt(headerPage))!
        guard disk.pointee.ReadBlocks(disk, media.pointee.MediaId, 1, UInt64(blockSize), header) == efiSuccess,
              let h = GPT.header(header, blockSize: blockSize) else { return .noGPT }
        let bytes = (h.entryCount * h.entrySize + blockSize - 1) / blockSize * blockSize
        let pages = (UInt64(bytes) + uefiPage - 1) / uefiPage
        guard let entryPages = allocate(pages: pages) else { return .noDisk }
        defer { free(entryPages, pages: pages) }
        let entries = UnsafeMutableRawPointer(bitPattern: UInt(entryPages))!
        guard disk.pointee.ReadBlocks(disk, media.pointee.MediaId, h.entriesLBA, UInt64(bytes), entries) == efiSuccess else {
            return .noGPT
        }
        guard let found = GPT.find(GPT.hfsPlusType, entries: entries, h) else { return .noHFS }
        return .found(index: found.index, uuid: found.unique)
    }
}
