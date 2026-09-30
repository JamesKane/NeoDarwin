// SPDX-License-Identifier: BSD-2-Clause
//
// Trust caches in the version 1 module format XNU publishes
// (osfmk/kern/trustcache.h), the format ndamfi's libTrustCache loads
// (kernel/neodarwin/amfi, docs/kernel/amfi-provider.md):
//
//     struct trust_cache_module1 {        // packed, little-endian
//         uint32_t version;               // 1
//         uuid_t   uuid;
//         uint32_t num_entries;
//         struct trust_cache_entry1 {
//             uint8_t cdhash[20];
//             uint8_t hash_type;          // CS_HASHTYPE_*
//             uint8_t flags;              // CS_TRUST_CACHE_*
//         } entries[];                    // strictly increasing cdhash
//     };
//
// The UUID is derived from the entries, so the same set of binaries always
// gives the same module, and two different sets never share a UUID (the
// kernel refuses a second module with a UUID it has).

public struct TrustCacheEntry: Sendable, Equatable {
    public let cdhash: [UInt8]
    public let hashType: UInt8
    public let flags: UInt8

    public init(cdhash: [UInt8], hashType: UInt8, flags: UInt8 = 0) {
        self.cdhash = cdhash
        self.hashType = hashType
        self.flags = flags
    }
}

public enum TrustCacheError: Error, CustomStringConvertible {
    case malformed(String)

    public var description: String {
        switch self {
        case .malformed(let why): return "not a version 1 trust cache module: \(why)"
        }
    }
}

public struct TrustCacheModule: Sendable {
    public static let version: UInt32 = 1
    public static let headerSize = 24
    public static let entrySize = 22

    public let uuid: [UInt8]
    public let entries: [TrustCacheEntry]

    static func less(_ a: [UInt8], _ b: [UInt8]) -> Bool {
        for i in 0..<min(a.count, b.count) where a[i] != b[i] { return a[i] < b[i] }
        return a.count < b.count
    }

    /// A module listing `entries`, sorted and without duplicates.
    public init(entries: [TrustCacheEntry]) {
        var sorted = entries.sorted { TrustCacheModule.less($0.cdhash, $1.cdhash) }
        var unique: [TrustCacheEntry] = []
        for e in sorted where unique.last?.cdhash != e.cdhash { unique.append(e) }
        sorted = unique
        var seed = Array("NeoDarwin trust cache v1".utf8)
        for e in sorted { seed += e.cdhash + [e.hashType, e.flags] }
        var u = Array(SHA256.hash(seed)[0..<16])
        u[6] = (u[6] & 0x0f) | 0x80  // RFC 9562 version 8: a custom, name-based UUID
        u[8] = (u[8] & 0x3f) | 0x80  // the RFC's variant
        self.uuid = u
        self.entries = sorted
    }

    public var bytes: [UInt8] {
        var out: [UInt8] = []
        out.reserveCapacity(TrustCacheModule.headerSize + entries.count * TrustCacheModule.entrySize)
        func le32(_ v: UInt32) { for i in 0..<4 { out.append(UInt8(truncatingIfNeeded: v >> (8 * UInt32(i)))) } }
        le32(TrustCacheModule.version)
        out += uuid
        le32(UInt32(entries.count))
        for e in entries { out += e.cdhash + [e.hashType, e.flags] }
        return out
    }

    /// Reads a module, checking it as ndamfi's loader does: version 1, its
    /// entries within the data, and in strictly increasing order.
    public init(bytes b: [UInt8]) throws(TrustCacheError) {
        guard b.count >= TrustCacheModule.headerSize else { throw .malformed("\(b.count) bytes") }
        let version = UInt32(b[0]) | UInt32(b[1]) << 8 | UInt32(b[2]) << 16 | UInt32(b[3]) << 24
        guard version == TrustCacheModule.version else { throw .malformed("version \(version)") }
        let n = Int(UInt32(b[20]) | UInt32(b[21]) << 8 | UInt32(b[22]) << 16 | UInt32(b[23]) << 24)
        guard n <= (b.count - TrustCacheModule.headerSize) / TrustCacheModule.entrySize else {
            throw .malformed("\(n) entries in \(b.count) bytes")
        }
        var entries: [TrustCacheEntry] = []
        for i in 0..<n {
            let at = TrustCacheModule.headerSize + i * TrustCacheModule.entrySize
            let e = TrustCacheEntry(cdhash: Array(b[at..<at + 20]), hashType: b[at + 20], flags: b[at + 21])
            if let last = entries.last, !TrustCacheModule.less(last.cdhash, e.cdhash) {
                throw .malformed("entry \(i) out of order")
            }
            entries.append(e)
        }
        self.uuid = Array(b[4..<20])
        self.entries = entries
    }
}

public func hex(_ bytes: [UInt8]) -> String {
    let digits = Array("0123456789abcdef".utf8)
    var out: [UInt8] = []
    for b in bytes { out += [digits[Int(b >> 4)], digits[Int(b & 0xf)]] }
    return String(decoding: out, as: UTF8.self)
}

public func uuidString(_ u: [UInt8]) -> String {
    let h = Array(hex(u).uppercased())
    let groups = [0..<8, 8..<12, 12..<16, 16..<20, 20..<32]
    return groups.map { String(h[$0]) }.joined(separator: "-")
}
