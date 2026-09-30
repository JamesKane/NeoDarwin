// SPDX-License-Identifier: BSD-2-Clause
//
// The code signature of an arm64 Mach-O, as XNU reads it (osfmk/kern/cs_blobs.h,
// bsd/kern/ubc_subr.c): the embedded SuperBlob that LC_CODE_SIGNATURE points
// to, its code directories, and the cdhash the kernel computes, which a trust
// cache lists. XNU uses the code directory whose hash type ranks highest
// (hashPriorities: SHA-1 < SHA-256 truncated < SHA-256 < SHA-384) and hashes
// its bytes with that type, keeping the first 20 bytes. Before a binary is
// listed, its signature is verified here as the kernel will verify it page by
// page at run time: every code page, and every embedded special blob, against
// the directory's hashes. A binary whose signature doesn't match its contents
// would be killed on its first bad page, so the tool refuses it at build time.

public enum CS {
    public static let superBlobMagic: UInt32 = 0xfade_0cc0
    public static let codeDirectoryMagic: UInt32 = 0xfade_0c02
    public static let slotCodeDirectory: UInt32 = 0
    public static let slotAlternateCodeDirectories: UInt32 = 0x1000
    public static let slotAlternateLimit: UInt32 = 0x1005
    public static let slotSignature: UInt32 = 0x10000
    public static let cdhashLength = 20

    public static let hashSHA1: UInt8 = 1
    public static let hashSHA256: UInt8 = 2
    public static let hashSHA256Truncated: UInt8 = 3
    public static let hashSHA384: UInt8 = 4

    public static let supportsScatter: UInt32 = 0x20100
    public static let supportsCodeLimit64: UInt32 = 0x20300

    public static let cpuTypeARM64: UInt32 = 0x0100_000c
    public static let fatMagic: UInt32 = 0xcafe_babe
    public static let fatMagic64: UInt32 = 0xcafe_babf

    static func rank(_ type: UInt8) -> Int {
        switch type {
        case hashSHA1: return 1
        case hashSHA256Truncated: return 2
        case hashSHA256: return 3
        case hashSHA384: return 4
        default: return 0
        }
    }
}

public enum SignatureError: Error, CustomStringConvertible, Equatable {
    case notMachO
    case unsigned
    case malformed(String)
    case unsupported(String)
    case mismatch(String)

    public var description: String {
        switch self {
        case .notMachO: return "not an arm64 Mach-O"
        case .unsigned: return "no code signature (LC_CODE_SIGNATURE)"
        case .malformed(let why): return "malformed code signature: \(why)"
        case .unsupported(let why): return "unsupported code signature: \(why)"
        case .mismatch(let why): return "code signature does not match the file: \(why)"
        }
    }
}

extension Array where Element == UInt8 {
    func be32(_ offset: Int) throws(SignatureError) -> UInt32 {
        guard offset >= 0, offset <= count - 4 else { throw .malformed("read past the end at \(offset)") }
        return UInt32(self[offset]) << 24 | UInt32(self[offset + 1]) << 16 | UInt32(self[offset + 2]) << 8 | UInt32(self[offset + 3])
    }

    func be64(_ offset: Int) throws(SignatureError) -> UInt64 {
        UInt64(try be32(offset)) << 32 | UInt64(try be32(offset + 4))
    }

    func le32(_ offset: Int) throws(SignatureError) -> UInt32 {
        guard offset >= 0, offset <= count - 4 else { throw .malformed("read past the end at \(offset)") }
        return UInt32(self[offset]) | UInt32(self[offset + 1]) << 8 | UInt32(self[offset + 2]) << 16 | UInt32(self[offset + 3]) << 24
    }
}

/// One signed arm64 image: a thin file or a slice of a universal one.
public struct SignedImage: Sendable {
    /// The slice's offset in the file.
    public let offset: Int
    public let cdhash: [UInt8]
    public let hashType: UInt8
    public let identifier: String
    /// CodeDirectory flags (CS_ADHOC 0x2, CS_LINKER_SIGNED 0x20000, ...).
    public let flags: UInt32
    public let hasEntitlements: Bool
}

public enum CodeSignature {
    /// Whether the file starts like a Mach-O or universal file at all.
    public static func isMachO(_ file: [UInt8]) -> Bool {
        guard file.count >= 4 else { return false }
        let le = (try? file.le32(0)) ?? 0
        let be = (try? file.be32(0)) ?? 0
        return le == 0xfeed_facf || le == 0xfeed_face || be == CS.fatMagic || be == CS.fatMagic64
    }

    /// The arm64 images in a file: one for a thin file, one per arm64 slice
    /// of a universal one. Throws .notMachO when there is none.
    public static func images(_ file: [UInt8]) throws(SignatureError) -> [SignedImage] {
        guard file.count >= 8 else { throw .notMachO }
        let magic = try file.be32(0)
        if magic == CS.fatMagic || magic == CS.fatMagic64 {
            let n = Int(try file.be32(4))
            let wide = magic == CS.fatMagic64
            let entry = wide ? 32 : 20
            var out: [SignedImage] = []
            for i in 0..<n {
                let at = 8 + i * entry
                guard try file.be32(at) == CS.cpuTypeARM64 else { continue }
                let offset = wide ? Int(try file.be64(at + 8)) : Int(try file.be32(at + 8))
                let size = wide ? Int(try file.be64(at + 16)) : Int(try file.be32(at + 12))
                guard offset >= 0, size >= 0, offset <= file.count - size else { throw .malformed("slice \(i) outside the file") }
                out.append(try image(Array(file[offset..<offset + size]), offset: offset))
            }
            guard !out.isEmpty else { throw .notMachO }
            return out
        }
        guard try file.le32(0) == 0xfeed_facf, try file.le32(4) == CS.cpuTypeARM64 else { throw .notMachO }
        return [try image(file, offset: 0)]
    }

    static func image(_ macho: [UInt8], offset: Int) throws(SignatureError) -> SignedImage {
        guard try macho.le32(0) == 0xfeed_facf, try macho.le32(4) == CS.cpuTypeARM64 else { throw .notMachO }
        let ncmds = Int(try macho.le32(16))
        var at = 32
        var signature: Range<Int>? = nil
        for _ in 0..<ncmds {
            let cmd = try macho.le32(at)
            let size = Int(try macho.le32(at + 4))
            guard size >= 8 else { throw .malformed("load command of \(size) bytes") }
            if cmd == 0x1d {  // LC_CODE_SIGNATURE
                let dataoff = Int(try macho.le32(at + 8))
                let datasize = Int(try macho.le32(at + 12))
                guard dataoff <= macho.count - datasize else { throw .malformed("LC_CODE_SIGNATURE outside the file") }
                signature = dataoff..<dataoff + datasize
            }
            at += size
        }
        guard let signature else { throw .unsigned }
        return try verify(macho, signature, offset: offset)
    }

    static func verify(_ m: [UInt8], _ sig: Range<Int>, offset: Int) throws(SignatureError) -> SignedImage {
        let base = sig.lowerBound
        guard try m.be32(base) == CS.superBlobMagic else { throw .malformed("no SuperBlob") }
        let length = Int(try m.be32(base + 4))
        guard length >= 12, length <= sig.count else { throw .malformed("SuperBlob length \(length)") }
        let count = Int(try m.be32(base + 8))
        guard count <= (length - 12) / 8 else { throw .malformed("SuperBlob index of \(count) entries") }
        var blobs: [UInt32: Range<Int>] = [:]
        for i in 0..<count {
            let type = try m.be32(base + 12 + 8 * i)
            let off = Int(try m.be32(base + 16 + 8 * i))
            guard off >= 12 + 8 * count, off <= length - 8 else { throw .malformed("blob \(type) outside the SuperBlob") }
            let blobLength = Int(try m.be32(base + off + 4))
            guard blobLength >= 8, blobLength <= length - off else { throw .malformed("blob \(type) of \(blobLength) bytes") }
            guard blobs[type] == nil else { throw .malformed("slot \(type) twice") }
            blobs[type] = (base + off)..<(base + off + blobLength)
        }
        // The best code directory, as cs_validate_csblob() chooses it.
        var best: Range<Int>? = nil
        var bestRank = 0
        for (type, range) in blobs where type == CS.slotCodeDirectory || (CS.slotAlternateCodeDirectories..<CS.slotAlternateLimit).contains(type) {
            guard try m.be32(range.lowerBound) == CS.codeDirectoryMagic else { throw .malformed("slot \(type) is not a CodeDirectory") }
            guard range.count >= 44 else { throw .malformed("short CodeDirectory") }
            let rank = CS.rank(m[range.lowerBound + 37])
            if rank == bestRank && best != nil { throw .malformed("two CodeDirectories of one hash type") }
            if best == nil || rank > bestRank {
                best = range
                bestRank = rank
            }
        }
        guard let cd = best else { throw .malformed("no CodeDirectory") }
        let c = cd.lowerBound
        let version = try m.be32(c + 8)
        let flags = try m.be32(c + 12)
        let hashOffset = Int(try m.be32(c + 16))
        let identOffset = Int(try m.be32(c + 20))
        let nSpecial = Int(try m.be32(c + 24))
        let nCode = Int(try m.be32(c + 28))
        var codeLimit = Int(try m.be32(c + 32))
        let hashSize = Int(m[c + 36])
        let hashType = m[c + 37]
        let pageShift = Int(m[c + 39])
        guard hashType == CS.hashSHA256 || hashType == CS.hashSHA256Truncated else {
            throw .unsupported("hash type \(hashType); only SHA-256 code directories are listed")
        }
        guard hashSize == (hashType == CS.hashSHA256 ? 32 : 20) else { throw .malformed("hash size \(hashSize) for type \(hashType)") }
        if version >= CS.supportsScatter, cd.count >= 48, try m.be32(c + 44) != 0 {
            throw .unsupported("scatter lists")
        }
        if version >= CS.supportsCodeLimit64, cd.count >= 64 {
            let limit64 = try m.be64(c + 56)
            if limit64 != 0 { codeLimit = Int(limit64) }
        }
        guard hashOffset <= cd.count, nSpecial * hashSize <= hashOffset, nCode <= (cd.count - hashOffset) / hashSize else {
            throw .malformed("hash slots outside the CodeDirectory")
        }
        guard codeLimit <= sig.lowerBound else { throw .malformed("code limit \(codeLimit) past the signature") }
        func digest(_ range: Range<Int>) -> ArraySlice<UInt8> { SHA256.hash(m, range)[0..<hashSize] }

        // Code pages.
        let pageSize = pageShift == 0 ? max(codeLimit, 1) : 1 << pageShift
        guard pageShift == 0 || pageShift >= 9, (codeLimit + pageSize - 1) / pageSize == nCode else {
            throw .malformed("\(nCode) code slots for a code limit of \(codeLimit)")
        }
        for i in 0..<nCode {
            let start = i * pageSize
            let page = start..<min(start + pageSize, codeLimit)
            let slot = c + hashOffset + i * hashSize
            guard digest(page) == m[slot..<slot + hashSize] else { throw .mismatch("page \(i) at offset \(start)") }
        }
        // Embedded special blobs: requirements (2), entitlements (5), DER entitlements (7).
        for (type, range) in blobs where type >= 1 && type <= 7 {
            let index = Int(type)
            guard index <= nSpecial else { throw .mismatch("slot \(type) has no hash") }
            let slot = c + hashOffset - index * hashSize
            guard digest(range) == m[slot..<slot + hashSize] else { throw .mismatch("special slot \(type)") }
        }
        var identifier = ""
        if identOffset > 0, identOffset < cd.count {
            let start = c + identOffset
            if let nul = m[start..<cd.upperBound].firstIndex(of: 0) { identifier = String(decoding: m[start..<nul], as: UTF8.self) }
        }
        let cdhash = Array(SHA256.hash(m, cd)[0..<CS.cdhashLength])
        return SignedImage(offset: offset, cdhash: cdhash, hashType: hashType, identifier: identifier, flags: flags,
                           hasEntitlements: blobs[5] != nil || blobs[7] != nil)
    }
}
