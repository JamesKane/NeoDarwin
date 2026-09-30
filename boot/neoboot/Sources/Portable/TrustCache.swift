// SPDX-License-Identifier: BSD-2-Clause
//
// The static trust cache neoboot hands the kernel (P1-15,
// docs/kernel/amfi-provider.md): \NeoDarwin\trustcache on the ESP is one
// version 1 trust-cache module (osfmk/kern/trustcache.h, written by
// //tools/trustcache). The kernel takes static trust caches as iBoot lays
// them out (bsd/sys/trust_caches.h): a segment that starts with
// trust_cache_offsets_t { u32 num_caches; u32 offsets[num_caches]; }, each
// offset from the segment's start to a module, named by /chosen/memory-map
// TrustCache (address, length). load_static_trust_cache() loads the first
// module as the static trust cache, and panics if it can't, so neoboot
// checks the module first, as ndamfi's loader will: version 1, its entries
// within the file, cdhashes strictly increasing. Pure functions over bytes.

enum TrustCache {
    /// trust_cache_offsets_t with one offset.
    static let segmentHeaderSize = 8
    /// version, uuid, num_entries.
    static let moduleHeaderSize = 24
    static let entrySize = 22
    static let cdhashSize = 20

    struct Module {
        var entries: UInt32
        var uuid: UUID16
    }

    /// Checks a module; nil and a reason when the kernel would refuse it.
    static func check(_ p: UnsafeRawPointer, _ length: Int) -> (Module?, StaticString) {
        guard length >= moduleHeaderSize else { return (nil, "shorter than a module header") }
        guard UInt32(littleEndian: p.loadUnaligned(as: UInt32.self)) == 1 else { return (nil, "not version 1") }
        let n = UInt32(littleEndian: p.loadUnaligned(fromByteOffset: 20, as: UInt32.self))
        guard UInt64(n) <= UInt64((length - moduleHeaderSize) / entrySize) else { return (nil, "entries past the end of the file") }
        var i = 1
        while i < Int(n) {
            let a = p + moduleHeaderSize + (i - 1) * entrySize, b = a + entrySize
            var j = 0
            while j < cdhashSize && a.load(fromByteOffset: j, as: UInt8.self) == b.load(fromByteOffset: j, as: UInt8.self) { j += 1 }
            guard j < cdhashSize, a.load(fromByteOffset: j, as: UInt8.self) < b.load(fromByteOffset: j, as: UInt8.self) else {
                return (nil, "cdhashes not in strictly increasing order")
            }
            i += 1
        }
        // The UUID's bytes in order, as UUID16 holds them.
        let uuid = UUID16(hi: UInt64(bigEndian: p.loadUnaligned(fromByteOffset: 4, as: UInt64.self)),
                          lo: UInt64(bigEndian: p.loadUnaligned(fromByteOffset: 12, as: UInt64.self)))
        return (Module(entries: n, uuid: uuid), "")
    }

    /// The segment header before a module at segmentHeaderSize: one cache.
    static func writeSegmentHeader(_ p: UnsafeMutableRawPointer) {
        p.storeBytes(of: UInt32(1).littleEndian, as: UInt32.self)
        p.storeBytes(of: UInt32(segmentHeaderSize).littleEndian, toByteOffset: 4, as: UInt32.self)
    }
}
