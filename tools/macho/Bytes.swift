// SPDX-License-Identifier: BSD-2-Clause
//
// Little-endian access to a Mach-O image held as a byte array. Reads are
// bounds-checked and throw, because every image kcgen and kcheck open is
// untrusted input.

public enum MachOError: Error, CustomStringConvertible {
    case truncated(String)
    case malformed(String)
    case unsupported(String)

    public var description: String {
        switch self {
        case .truncated(let what): return "truncated: \(what)"
        case .malformed(let what): return "malformed: \(what)"
        case .unsupported(let what): return "unsupported: \(what)"
        }
    }
}

extension Array where Element == UInt8 {
    @inline(__always)
    public func check(_ offset: Int, _ width: Int) throws {
        guard offset >= 0, width >= 0, offset <= count - width else {
            throw MachOError.truncated("\(width) byte(s) at offset \(offset) of \(count)")
        }
    }

    public func u8(_ offset: Int) throws -> UInt8 {
        try check(offset, 1)
        return self[offset]
    }

    public func u16(_ offset: Int) throws -> UInt16 {
        try check(offset, 2)
        return UInt16(self[offset]) | UInt16(self[offset + 1]) << 8
    }

    public func u32(_ offset: Int) throws -> UInt32 {
        try check(offset, 4)
        var v: UInt32 = 0
        for i in 0..<4 { v |= UInt32(self[offset + i]) << (8 * i) }
        return v
    }

    public func u64(_ offset: Int) throws -> UInt64 {
        try check(offset, 8)
        var v: UInt64 = 0
        for i in 0..<8 { v |= UInt64(self[offset + i]) << (8 * i) }
        return v
    }

    /// A fixed-width, NUL-padded name such as `segname`.
    public func name16(_ offset: Int) throws -> String {
        try check(offset, 16)
        let raw = self[offset..<offset + 16].prefix { $0 != 0 }
        return String(decoding: raw, as: UTF8.self)
    }

    /// A NUL-terminated string starting at `offset`.
    public func cString(_ offset: Int, limit: Int) throws -> String {
        try check(offset, 1)
        let end = Swift.min(count, limit)
        guard offset < end, let nul = self[offset..<end].firstIndex(of: 0) else {
            throw MachOError.malformed("unterminated string at offset \(offset)")
        }
        return String(decoding: self[offset..<nul], as: UTF8.self)
    }

    public mutating func put16(_ offset: Int, _ v: UInt16) throws {
        try check(offset, 2)
        for i in 0..<2 { self[offset + i] = UInt8(truncatingIfNeeded: v >> (8 * i)) }
    }

    public mutating func put32(_ offset: Int, _ v: UInt32) throws {
        try check(offset, 4)
        for i in 0..<4 { self[offset + i] = UInt8(truncatingIfNeeded: v >> (8 * i)) }
    }

    public mutating func put64(_ offset: Int, _ v: UInt64) throws {
        try check(offset, 8)
        for i in 0..<8 { self[offset + i] = UInt8(truncatingIfNeeded: v >> (8 * i)) }
    }

    public mutating func putName16(_ offset: Int, _ name: String) throws {
        let utf8 = Array(name.utf8)
        guard utf8.count <= 16 else { throw MachOError.malformed("name longer than 16 bytes: \(name)") }
        try check(offset, 16)
        for i in 0..<16 { self[offset + i] = i < utf8.count ? utf8[i] : 0 }
    }

    // Appending forms, for building load commands and linkedit blobs.

    public mutating func append16(_ v: UInt16) { for i in 0..<2 { append(UInt8(truncatingIfNeeded: v >> (8 * i))) } }
    public mutating func append32(_ v: UInt32) { for i in 0..<4 { append(UInt8(truncatingIfNeeded: v >> (8 * i))) } }
    public mutating func append64(_ v: UInt64) { for i in 0..<8 { append(UInt8(truncatingIfNeeded: v >> (8 * i))) } }

    public mutating func appendName16(_ name: String) {
        let utf8 = Array(name.utf8.prefix(16))
        append(contentsOf: utf8)
        append(contentsOf: repeatElement(0, count: 16 - utf8.count))
    }

    public mutating func pad(toMultipleOf alignment: Int) {
        let rem = count % alignment
        if rem != 0 { append(contentsOf: repeatElement(0, count: alignment - rem)) }
    }
}

@inline(__always)
public func roundUp(_ value: Int, _ alignment: Int) -> Int {
    (value + alignment - 1) / alignment * alignment
}

@inline(__always)
public func roundUp(_ value: UInt64, _ alignment: UInt64) -> UInt64 {
    (value + alignment - 1) / alignment * alignment
}

/// RFC 4648 base64, for plist <data> values.
public enum Base64 {
    static let alphabet = Array("ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/".utf8)

    public static func encode(_ bytes: [UInt8]) -> String {
        var out: [UInt8] = []
        var i = 0
        while i < bytes.count {
            let n = Swift.min(3, bytes.count - i)
            var v = UInt32(bytes[i]) << 16
            if n > 1 { v |= UInt32(bytes[i + 1]) << 8 }
            if n > 2 { v |= UInt32(bytes[i + 2]) }
            for j in 0..<4 { out.append(j <= n ? alphabet[Int((v >> (18 - 6 * j)) & 0x3f)] : UInt8(ascii: "=")) }
            i += 3
        }
        return String(decoding: out, as: UTF8.self)
    }
}
