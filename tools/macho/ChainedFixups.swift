// SPDX-License-Identifier: BSD-2-Clause
//
// Chained fixups in the kernel-cache pointer format
// (DYLD_CHAINED_PTR_64_KERNEL_CACHE), the only format the arm64 kernel
// walks when it slides its own collection (osfmk/mach/dyld_kernel_fixups.h).
// Every location holds
//
//     target:30  cacheLevel:2  diversity:16  addrDiv:1  key:2  next:12  isAuth:1
//
// where `target` is an offset from the collection's mach header and `next`
// is the distance to the following fixup in 4-byte units (0 ends the chain).
// The kernel handles exactly one chain per page (no DYLD_CHAINED_PTR_START_MULTI).
//
// Kernel collections are flat: a segment's file offset equals its VM offset
// from the collection header, so locations and targets are both plain
// offsets into the collection's bytes.

public struct KernelCachePointer: Sendable, Equatable {
    public var target: UInt32
    public var cacheLevel: UInt8
    public var diversity: UInt16
    public var addrDiv: Bool
    public var key: UInt8
    public var next: UInt16
    public var isAuth: Bool

    public static let maxTarget: UInt64 = (1 << 30) - 1
    public static let maxNext: UInt16 = (1 << 12) - 1

    public init(target: UInt32, next: UInt16) {
        self.target = target
        cacheLevel = 0
        diversity = 0
        addrDiv = false
        key = 0
        self.next = next
        isAuth = false
    }

    public init(raw: UInt64) {
        target = UInt32(raw & 0x3fff_ffff)
        cacheLevel = UInt8((raw >> 30) & 0x3)
        diversity = UInt16((raw >> 32) & 0xffff)
        addrDiv = (raw >> 48) & 1 == 1
        key = UInt8((raw >> 49) & 0x3)
        next = UInt16((raw >> 51) & 0xfff)
        isAuth = raw >> 63 == 1
    }

    public var raw: UInt64 {
        UInt64(target) & 0x3fff_ffff
            | UInt64(cacheLevel & 0x3) << 30
            | UInt64(diversity) << 32
            | (addrDiv ? 1 : 0) << 48
            | UInt64(key & 0x3) << 49
            | UInt64(next & 0xfff) << 51
            | (isAuth ? 1 : 0) << 63
    }
}

public enum KernelCacheChains {
    public static let pointerFormat: UInt16 = 8  // DYLD_CHAINED_PTR_64_KERNEL_CACHE
    public static let pageSize = 0x4000
    public static let startNone: UInt16 = 0xffff
    public static let startMulti: UInt16 = 0x8000
    static let headerSize = 28
    static let importsFormat: UInt32 = 1  // DYLD_CHAINED_IMPORT; there are no imports

    /// A region described by one dyld_chained_starts_in_segment, normally a
    /// top-level segment of the collection. Offsets are from the header.
    public struct Region: Sendable {
        public let offset: UInt64
        public let size: UInt64
        public init(offset: UInt64, size: UInt64) {
            self.offset = offset
            self.size = size
        }
    }

    public struct Rebase: Sendable {
        public let location: UInt64
        public let target: UInt64
        public init(location: UInt64, target: UInt64) {
            self.location = location
            self.target = target
        }
    }

    /// Writes one chain per page into `image` and returns the
    /// LC_DYLD_CHAINED_FIXUPS payload. Every rebase must fall in one of
    /// `regions`; regions with no rebases get no starts entry.
    public static func encode(image: inout [UInt8], regions: [Region], rebases: [Rebase]) throws -> [UInt8] {
        let page = UInt64(pageSize)
        let sorted = rebases.sorted { $0.location < $1.location }
        for (a, b) in zip(sorted, sorted.dropFirst()) where b.location < a.location + 8 {
            throw MachOError.malformed("overlapping fixups at 0x\(String(a.location, radix: 16)) and 0x\(String(b.location, radix: 16))")
        }

        // Group by region, validating as we go.
        var perRegion = Array(repeating: [Rebase](), count: regions.count)
        for r in sorted {
            guard r.location % 4 == 0 else { throw MachOError.unsupported("fixup at 0x\(String(r.location, radix: 16)) is not 4-byte aligned") }
            guard r.location % page <= page - 8 else { throw MachOError.unsupported("fixup at 0x\(String(r.location, radix: 16)) straddles a page") }
            guard r.target <= KernelCachePointer.maxTarget else {
                throw MachOError.unsupported("fixup target 0x\(String(r.target, radix: 16)) exceeds the 30-bit kernel-cache range")
            }
            guard let i = regions.firstIndex(where: { r.location >= $0.offset && r.location + 8 <= $0.offset + $0.size }) else {
                throw MachOError.malformed("fixup at 0x\(String(r.location, radix: 16)) is outside every region")
            }
            guard regions[i].offset % page == 0 else { throw MachOError.malformed("region \(i) is not page-aligned") }
            perRegion[i].append(r)
        }

        // Link the chains in place.
        for list in perRegion {
            for (i, r) in list.enumerated() {
                var next: UInt16 = 0
                if i + 1 < list.count {
                    let n = list[i + 1]
                    if n.location / page == r.location / page {
                        next = UInt16((n.location - r.location) / 4)  // < 4096 inside a 16 KiB page
                    }
                }
                try image.put64(Int(r.location), KernelCachePointer(target: UInt32(r.target), next: next).raw)
            }
        }

        // dyld_chained_fixups_header, then dyld_chained_starts_in_image.
        var blob: [UInt8] = []
        let startsOffset = 32  // header rounded up to 8
        blob.append32(0)  // fixups_version
        blob.append32(UInt32(startsOffset))
        blob.append32(0)  // imports_offset, patched below
        blob.append32(0)  // symbols_offset, patched below
        blob.append32(0)  // imports_count
        blob.append32(importsFormat)
        blob.append32(0)  // symbols_format
        blob.pad(toMultipleOf: 8)

        blob.append32(UInt32(regions.count))
        let infoTable = blob.count
        blob.append(contentsOf: repeatElement(0, count: 4 * regions.count))
        for (i, region) in regions.enumerated() where !perRegion[i].isEmpty {
            blob.pad(toMultipleOf: 8)
            let at = blob.count
            try blob.put32(infoTable + 4 * i, UInt32(at - startsOffset))
            let pageCount = Int((region.size + page - 1) / page)
            guard pageCount <= Int(UInt16.max) else { throw MachOError.unsupported("region \(i) spans \(pageCount) pages") }
            var starts = Array(repeating: startNone, count: pageCount)
            for r in perRegion[i] {
                let p = Int((r.location - region.offset) / page)
                if starts[p] == startNone { starts[p] = UInt16((r.location - region.offset) % page) }
            }
            blob.append32(UInt32(22 + 2 * pageCount))  // size
            blob.append16(UInt16(pageSize))
            blob.append16(pointerFormat)
            blob.append64(region.offset)  // segment_offset
            blob.append32(0)  // max_valid_pointer (32-bit formats only)
            blob.append16(UInt16(pageCount))
            for s in starts { blob.append16(s) }
        }
        blob.pad(toMultipleOf: 8)
        try blob.put32(8, UInt32(blob.count))  // imports_offset: empty table
        try blob.put32(12, UInt32(blob.count))  // symbols_offset: empty pool
        return blob
    }

    public struct Starts: Sendable {
        public let regionIndex: Int
        public let pageSize: UInt16
        public let pointerFormat: UInt16
        public let segmentOffset: UInt64
        public let pageStarts: [UInt16]
    }

    public struct Fixup: Sendable {
        public let regionIndex: Int
        public let location: UInt64
        public let pointer: KernelCachePointer
    }

    public struct Decoded: Sendable {
        public let regionCount: Int
        public let starts: [Starts]
        public let fixups: [Fixup]
        public let chains: Int
    }

    /// Parses an LC_DYLD_CHAINED_FIXUPS payload at `blobOffset` and walks
    /// every chain it describes, the way kernel_collection_slide() does, but
    /// rejecting anything the kernel would silently stop on.
    public static func decode(image: [UInt8], blobOffset: Int, blobSize: Int) throws -> Decoded {
        try image.check(blobOffset, blobSize)
        let blob = Array(image[blobOffset..<blobOffset + blobSize])
        guard try blob.u32(0) == 0 else { throw MachOError.unsupported("chained fixups version \(try blob.u32(0))") }
        guard try blob.u32(16) == 0 else { throw MachOError.unsupported("kernel collections have no imports") }
        let startsOffset = Int(try blob.u32(4))
        let segCount = Int(try blob.u32(startsOffset))
        var starts: [Starts] = []
        var fixups: [Fixup] = []
        var chains = 0
        for i in 0..<segCount {
            let infoOffset = Int(try blob.u32(startsOffset + 4 + 4 * i))
            if infoOffset == 0 { continue }
            let at = startsOffset + infoOffset
            let size = Int(try blob.u32(at))
            let pageSize = try blob.u16(at + 4)
            let format = try blob.u16(at + 6)
            let segmentOffset = try blob.u64(at + 8)
            let pageCount = Int(try blob.u16(at + 20))
            guard size >= 22 + 2 * pageCount else { throw MachOError.malformed("starts for region \(i) are \(size) bytes for \(pageCount) pages") }
            guard format == pointerFormat else { throw MachOError.unsupported("region \(i) uses pointer format \(format); the kernel walks only \(pointerFormat)") }
            guard pageSize > 0 else { throw MachOError.malformed("region \(i) has page size 0") }
            var pageStarts: [UInt16] = []
            for p in 0..<pageCount { pageStarts.append(try blob.u16(at + 22 + 2 * p)) }
            starts.append(Starts(regionIndex: i, pageSize: pageSize, pointerFormat: format, segmentOffset: segmentOffset, pageStarts: pageStarts))

            for (p, first) in pageStarts.enumerated() where first != startNone {
                guard first & startMulti == 0 else { throw MachOError.unsupported("region \(i) page \(p) has multiple chain starts; the kernel rejects them") }
                let pageBase = segmentOffset + UInt64(p) * UInt64(pageSize)
                var loc = pageBase + UInt64(first)
                chains += 1
                while true {
                    guard loc + 8 <= pageBase + UInt64(pageSize) else {
                        throw MachOError.malformed("chain in region \(i) page \(p) runs off the page at 0x\(String(loc, radix: 16))")
                    }
                    let ptr = KernelCachePointer(raw: try image.u64(Int(loc)))
                    fixups.append(Fixup(regionIndex: i, location: loc, pointer: ptr))
                    if ptr.next == 0 { break }
                    loc += UInt64(ptr.next) * 4
                }
            }
        }
        return Decoded(regionCount: segCount, starts: starts, fixups: fixups, chains: chains)
    }
}
