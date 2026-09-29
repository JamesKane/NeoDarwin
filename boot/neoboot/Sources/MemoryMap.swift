// SPDX-License-Identifier: BSD-2-Clause
//
// The firmware memory map and the one DRAM window XNU gets. boot_args
// describes a single contiguous {physBase, memSize}; XNU cannot express
// holes, so the loader takes the largest run of memory that is free once
// boot services exit (arm64-sbsa-bringup.md §2.1, memory-map policy).

import UEFI

struct MemoryMap {
    static let bufferPages: UInt64 = 8  // 32 KiB: several hundred descriptors

    let buffer: UnsafeMutableRawPointer
    var size: UInt64 = 0
    var key: UInt64 = 0
    var descriptorSize: UInt64 = 0

    init?(_ fw: Firmware) {
        guard let at = fw.allocate(pages: Self.bufferPages) else { return nil }
        buffer = UnsafeMutableRawPointer(bitPattern: UInt(at))!
        guard refresh(fw) else { return nil }
    }

    mutating func refresh(_ fw: Firmware) -> Bool {
        var mapSize = Self.bufferPages * uefiPage
        var version: UInt32 = 0
        let status = fw.boot.pointee.GetMemoryMap(&mapSize, buffer.assumingMemoryBound(to: EFI_MEMORY_DESCRIPTOR.self),
                                                  &key, &descriptorSize, &version)
        guard status == efiSuccess, descriptorSize >= 40 else { return false }
        size = mapSize
        return true
    }

    var count: Int { Int(size / descriptorSize) }

    func descriptor(_ i: Int) -> (type: UInt32, start: UInt64, end: UInt64) {
        let d = buffer + i * Int(descriptorSize)
        let type = d.loadUnaligned(as: UInt32.self)
        let start = d.loadUnaligned(fromByteOffset: 8, as: UInt64.self)
        let pages = d.loadUnaligned(fromByteOffset: 24, as: UInt64.self)
        return (type, start, start + pages * uefiPage)
    }

    /// Memory the kernel may own after ExitBootServices.
    static func reclaimable(_ type: UInt32) -> Bool {
        switch Int(type) {
        case EfiLoaderCode, EfiLoaderData, EfiBootServicesCode, EfiBootServicesData, EfiConventionalMemory: return true
        default: return false
        }
    }

    /// The largest run of adjacent reclaimable descriptors.
    func largestWindow() -> (start: UInt64, end: UInt64) {
        var best: (start: UInt64, end: UInt64) = (0, 0)
        for i in 0..<count {
            let d = descriptor(i)
            guard Self.reclaimable(d.type) else { continue }
            // Only start runs at descriptors nothing reclaimable ends against.
            var preceded = false
            for j in 0..<count where j != i {
                let e = descriptor(j)
                if Self.reclaimable(e.type) && e.end == d.start { preceded = true; break }
            }
            if preceded { continue }
            var end = d.end
            var grew = true
            while grew {
                grew = false
                for j in 0..<count {
                    let e = descriptor(j)
                    if Self.reclaimable(e.type) && e.start == end { end = e.end; grew = true }
                }
            }
            if end - d.start > best.end - best.start { best = (d.start, end) }
        }
        return best
    }

    /// The type of the descriptor holding `address`, or nil if none does.
    func type(at address: UInt64) -> UInt32? {
        for i in 0..<count {
            let d = descriptor(i)
            if address >= d.start && address < d.end { return d.type }
        }
        return nil
    }

    /// Total RAM the firmware reports, for boot_args.memSizeActual.
    func totalRAM() -> UInt64 {
        var total: UInt64 = 0
        for i in 0..<count {
            let d = descriptor(i)
            switch Int(d.type) {
            case EfiMemoryMappedIO, EfiMemoryMappedIOPortSpace, EfiReservedMemoryType, EfiUnusableMemory: continue
            default: total += d.end - d.start
            }
        }
        return total
    }
}
