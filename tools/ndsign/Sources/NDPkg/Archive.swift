// SPDX-License-Identifier: BSD-2-Clause
//
// The .ndpkg container (docs/architecture/packaging.md §3): a POSIX ustar
// archive compressed as one zstd frame. Written deterministically (mtime 0,
// owner root:wheel, members in a fixed order, zstd level 19 single-threaded),
// so the same inputs give the same bytes; read strictly (only what the writer
// produces: regular files and symbolic links, valid checksums, no duplicate
// or unsafe paths).

import NDSignC

public struct Member: Sendable, Equatable {
    public enum Kind: Sendable, Equatable {
        case file([UInt8])
        case link(String)
    }
    public var path: String
    public var mode: UInt32
    public var kind: Kind

    public init(path: String, mode: UInt32, kind: Kind) {
        self.path = path; self.mode = mode; self.kind = kind
    }
}

enum Tar {
    static func field(_ value: UInt64, width: Int) -> [UInt8] {
        let digits = Array(String(value, radix: 8).utf8)
        return [UInt8](repeating: 0x30, count: width - 1 - digits.count) + digits + [0]
    }

    static func put(_ header: inout [UInt8], _ at: Int, _ bytes: [UInt8]) {
        header.replaceSubrange(at..<(at + bytes.count), with: bytes)
    }

    static func write(_ members: [Member]) throws -> [UInt8] {
        var out: [UInt8] = []
        for m in members {
            var h = [UInt8](repeating: 0, count: 512)
            let path = Array(m.path.utf8)
            if path.count <= 100 {
                put(&h, 0, path)
            } else {
                // ustar's prefix: split at a slash, name ≤ 100, prefix ≤ 155.
                guard let cut = path.indices.last(where: { path[$0] == 0x2f && $0 <= 155 && path.count - $0 - 1 <= 100 }) else {
                    throw NDSignError("path too long for ustar: \(m.path)")
                }
                put(&h, 0, Array(path[(cut + 1)...]))
                put(&h, 345, Array(path[..<cut]))
            }
            var size: UInt64 = 0
            switch m.kind {
            case .file(let data): size = UInt64(data.count); h[156] = 0x30
            case .link(let target):
                let t = Array(target.utf8)
                guard t.count <= 100 else { throw NDSignError("link target too long: \(m.path)") }
                put(&h, 157, t); h[156] = 0x32
            }
            put(&h, 100, field(UInt64(m.mode), width: 8))
            put(&h, 108, field(0, width: 8))
            put(&h, 116, field(0, width: 8))
            put(&h, 124, field(size, width: 12))
            put(&h, 136, field(0, width: 12))
            put(&h, 257, Array("ustar\0".utf8) + Array("00".utf8))
            put(&h, 265, Array("root".utf8))
            put(&h, 297, Array("wheel".utf8))
            put(&h, 148, [UInt8](repeating: 0x20, count: 8))
            let sum = h.reduce(0) { $0 + UInt64($1) }
            put(&h, 148, field(sum, width: 7) + [0x20])
            out += h
            if case .file(let data) = m.kind {
                out += data
                out += [UInt8](repeating: 0, count: (512 - data.count % 512) % 512)
            }
        }
        out += [UInt8](repeating: 0, count: 1024)
        return out
    }

    static func octal(_ b: ArraySlice<UInt8>) throws -> UInt64 {
        var v: UInt64 = 0
        var seen = false
        for c in b {
            if c == 0 || c == 0x20 { if seen { break } else { continue } }
            guard c >= 0x30 && c <= 0x37, v < (1 << 60) else { throw NDSignError("bad octal field in archive") }
            v = v * 8 + UInt64(c - 0x30); seen = true
        }
        return v
    }

    static func string(_ b: ArraySlice<UInt8>) -> String {
        String(decoding: b.prefix(while: { $0 != 0 }), as: UTF8.self)
    }

    static func read(_ bytes: [UInt8]) throws -> [Member] {
        var members: [Member] = []
        var at = 0
        var seen = Set<String>()
        while true {
            guard at + 512 <= bytes.count else { throw NDSignError("archive truncated") }
            let h = bytes[at..<(at + 512)]
            if h.allSatisfy({ $0 == 0 }) {
                guard at + 1024 <= bytes.count, bytes[(at + 512)...].allSatisfy({ $0 == 0 }) else {
                    throw NDSignError("data after the archive's end")
                }
                return members
            }
            var check = Array(h)
            check.replaceSubrange(148..<156, with: [UInt8](repeating: 0x20, count: 8))
            guard try octal(h[(at + 148)..<(at + 156)]) == check.reduce(0, { $0 + UInt64($1) }) else {
                throw NDSignError("archive header checksum mismatch")
            }
            let name = string(h[at..<(at + 100)]), prefix = string(h[(at + 345)..<(at + 500)])
            let path = prefix.isEmpty ? name : prefix + "/" + name
            guard safe(path), seen.insert(path).inserted else { throw NDSignError("unsafe or duplicate path in archive: \(path)") }
            let mode = UInt32(try octal(h[(at + 100)..<(at + 108)]))
            let size = Int(try octal(h[(at + 124)..<(at + 136)]))
            at += 512
            switch h[at - 512 + 156] {
            case 0x30:
                guard at + size <= bytes.count else { throw NDSignError("archive truncated") }
                members.append(Member(path: path, mode: mode, kind: .file(Array(bytes[at..<(at + size)]))))
                at += (size + 511) / 512 * 512
            case 0x32:
                members.append(Member(path: path, mode: mode, kind: .link(string(h[(at - 512 + 157)..<(at - 512 + 257)]))))
            default:
                throw NDSignError("unsupported archive member type: \(path)")
            }
        }
    }
}

/// A relative path without empty, "." or ".." components.
func safe(_ path: String) -> Bool {
    !path.isEmpty && !path.hasPrefix("/") &&
        path.split(separator: "/", omittingEmptySubsequences: false).allSatisfy { !$0.isEmpty && $0 != "." && $0 != ".." }
}

/// The archive layer alone (no signature checks), for tools and tests.
public func readArchive(_ ndpkg: [UInt8]) throws -> [Member] { try Tar.read(try Zstd.decompress(ndpkg)) }
public func writeArchive(_ members: [Member]) throws -> [UInt8] { try Zstd.compress(try Tar.write(members)) }

enum Zstd {
    static let level: Int32 = 19
    static let limit = 1 << 32

    static func compress(_ bytes: [UInt8]) throws -> [UInt8] {
        var out = [UInt8](repeating: 0, count: ZSTD_compressBound(bytes.count))
        let n = out.withUnsafeMutableBytes { o in
            bytes.withUnsafeBytes { ZSTD_compress(o.baseAddress, o.count, $0.baseAddress, $0.count, level) }
        }
        guard ZSTD_isError(n) == 0 else { throw NDSignError("zstd: \(String(cString: ZSTD_getErrorName(n)))") }
        return Array(out[0..<n])
    }

    static func decompress(_ bytes: [UInt8]) throws -> [UInt8] {
        let size = bytes.withUnsafeBytes { ZSTD_getFrameContentSize($0.baseAddress, $0.count) }
        guard size != ND_ZSTD_CONTENTSIZE_UNKNOWN, size != ND_ZSTD_CONTENTSIZE_ERROR, size <= UInt64(limit) else {
            throw NDSignError("not a zstd frame with its content size")
        }
        var out = [UInt8](repeating: 0, count: Int(size))
        let n = out.withUnsafeMutableBytes { o in
            bytes.withUnsafeBytes { ZSTD_decompress(o.baseAddress, o.count, $0.baseAddress, $0.count) }
        }
        guard ZSTD_isError(n) == 0, n == Int(size) else { throw NDSignError("zstd: corrupt frame") }
        return out
    }
}
