// SPDX-License-Identifier: BSD-2-Clause
//
// Split-segment info, version 2 (LC_SEGMENT_SPLIT_INFO starting with
// DYLD_CACHE_ADJ_V2_FORMAT): for every reference from one section to
// another, where it is and what kind of reference it is, so a collection
// linker can move an image's segments apart and fix the references up.
// The format and kinds are dyld's (mach-o/dyld_cache_format.h and
// kernel-collection-builder/KernelAdjustDylibSegments.cpp in the pinned
// dyld): ld writes it for kexts, and for kernels linked with
// -add_split_seg_info.
//
//     Whole         := <count> FromToSection+
//     FromToSection := <from-sect-index> <to-sect-index> <count> ToOffset+
//     ToOffset      := <to-sect-offset-delta> <count> FromOffset+
//     FromOffset    := <kind> <count> <from-sect-offset-delta>+
//
// Section index 0 is the mach header; 1 and up are the sections in load
// command order. All numbers are ULEB128.

public enum SplitSegKind {
    public static let pointer32: UInt8 = 1
    public static let pointer64: UInt8 = 2
    public static let delta32: UInt8 = 3
    public static let delta64: UInt8 = 4
    public static let arm64ADRP: UInt8 = 5
    public static let arm64Off12: UInt8 = 6
    public static let arm64BR26: UInt8 = 7
    public static let imageOff32: UInt8 = 12
    public static let threadedPointer64: UInt8 = 13
}

public struct SplitSegReference: Sendable {
    public let kind: UInt8
    public let fromSection: Int
    public let fromOffset: UInt64
    public let toSection: Int
    public let toOffset: UInt64
}

public enum SplitSegInfo {
    public static let v2Format: UInt8 = 0x7f

    /// The references in `image`'s LC_SEGMENT_SPLIT_INFO, or nil if it has none.
    public static func references(_ image: MachOImage) throws -> [SplitSegReference]? {
        guard let lc = image.command(LC.segmentSplitInfo) else { return nil }
        let off = Int(try image.bytes.u32(lc.offset + 8)) + image.base
        let size = Int(try image.bytes.u32(lc.offset + 12))
        try image.bytes.check(off, size)
        let b = image.bytes
        let end = off + size
        guard size > 0, b[off] == v2Format else { throw MachOError.unsupported("split-segment info is not version 2") }
        var p = off + 1
        func uleb() throws -> UInt64 {
            var v: UInt64 = 0, shift: UInt64 = 0
            while true {
                guard p < end else { throw MachOError.truncated("split-segment info ULEB128 at \(p)") }
                let byte = b[p]; p += 1
                guard shift < 64 else { throw MachOError.malformed("split-segment info ULEB128 overflows") }
                v |= UInt64(byte & 0x7f) << shift
                if byte & 0x80 == 0 { return v }
                shift += 7
            }
        }
        let sectionCount = image.segments.reduce(0) { $0 + $1.sections.count }
        var out: [SplitSegReference] = []
        let pairs = try uleb()
        for _ in 0..<pairs {
            let from = Int(try uleb()), to = Int(try uleb())
            guard from <= sectionCount, to <= sectionCount else {
                throw MachOError.malformed("split-segment info names section \(max(from, to)) of \(sectionCount)")
            }
            var toOffset: UInt64 = 0
            let toCount = try uleb()
            for _ in 0..<toCount {
                toOffset += try uleb()
                let fromCount = try uleb()
                for _ in 0..<fromCount {
                    let kind = try uleb()
                    guard kind <= 13 else { throw MachOError.unsupported("split-segment kind \(kind)") }
                    var fromOffset: UInt64 = 0
                    let deltaCount = try uleb()
                    for _ in 0..<deltaCount {
                        fromOffset += try uleb()
                        out.append(SplitSegReference(kind: UInt8(kind), fromSection: from, fromOffset: fromOffset,
                                                     toSection: to, toOffset: toOffset))
                    }
                }
            }
        }
        // Trailing bytes are alignment padding (zeros).
        return out
    }
}
