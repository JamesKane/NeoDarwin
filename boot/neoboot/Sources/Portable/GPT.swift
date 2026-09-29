// SPDX-License-Identifier: BSD-2-Clause
//
// GUID partition tables (UEFI 2.10 §5.3) and UUIDs in text, for the root
// by boot-uuid (docs/kernel/storage.md): neoboot reads the GPT of the disk
// it was loaded from and passes the unique GUID of its HFS+ partition as
// /chosen boot-uuid, the IOMedia "UUID" the kernel's IOGUIDPartitionScheme
// gives that partition. The host's gptimage tool writes the same tables
// from the same code. Pure functions over bytes: no firmware, no
// allocation.

/// A UUID in its canonical byte order (the order of its text, and of the
/// kernel's uuid_t): `hi` holds bytes 0-7, `lo` bytes 8-15.
struct UUID16: Equatable {
    var hi: UInt64
    var lo: UInt64
}

enum GPT {
    /// Apple HFS+ (48465300-0000-11AA-AA11-00306543ECAC): IOMedia content
    /// hint and AppleFileSystemDriver's match.
    static let hfsPlusType = UUID16(hi: 0x4846_5300_0000_11AA, lo: 0xAA11_0030_6543_ECAC)
    /// EFI System Partition (C12A7328-F81F-11D2-BA4B-00A0C93EC93B).
    static let espType = UUID16(hi: 0xC12A_7328_F81F_11D2, lo: 0xBA4B_00A0_C93E_C93B)

    static let headerSignature: UInt64 = 0x5452_4150_2049_4645  // "EFI PART", little-endian
    static let headerSize = 92
    static let entrySize = 128

    /// A GUID as GPT stores it: the first three fields little-endian, the
    /// last eight bytes as they are.
    static func guid(at p: UnsafeRawPointer) -> UUID16 {
        let d1 = UInt64(UInt32(littleEndian: p.loadUnaligned(as: UInt32.self)))
        let d2 = UInt64(UInt16(littleEndian: p.loadUnaligned(fromByteOffset: 4, as: UInt16.self)))
        let d3 = UInt64(UInt16(littleEndian: p.loadUnaligned(fromByteOffset: 6, as: UInt16.self)))
        return UUID16(hi: d1 << 32 | d2 << 16 | d3, lo: UInt64(bigEndian: p.loadUnaligned(fromByteOffset: 8, as: UInt64.self)))
    }

    static func storeGUID(_ u: UUID16, at p: UnsafeMutableRawPointer) {
        p.storeBytes(of: UInt32(truncatingIfNeeded: u.hi >> 32).littleEndian, as: UInt32.self)
        p.storeBytes(of: UInt16(truncatingIfNeeded: u.hi >> 16).littleEndian, toByteOffset: 4, as: UInt16.self)
        p.storeBytes(of: UInt16(truncatingIfNeeded: u.hi).littleEndian, toByteOffset: 6, as: UInt16.self)
        p.storeBytes(of: u.lo.bigEndian, toByteOffset: 8, as: UInt64.self)
    }

    /// CRC-32 (IEEE 802.3, reflected, as GPT's header and array CRCs are).
    static func crc32(_ p: UnsafeRawPointer, _ count: Int, _ start: UInt32 = 0) -> UInt32 {
        var c = ~start
        for i in 0..<count {
            c ^= UInt32(p.load(fromByteOffset: i, as: UInt8.self))
            for _ in 0..<8 { c = (c >> 1) ^ (0xEDB8_8320 & (0 &- (c & 1))) }
        }
        return ~c
    }

    /// Where a GPT header (the block at LBA 1) says its entries are, if it
    /// is one: its signature, size and CRC check, and its entries are 128
    /// bytes or a larger multiple of 8, at most 1024 of them.
    struct Header {
        var entriesLBA: UInt64
        var entryCount: Int
        var entrySize: Int
        var entriesCRC: UInt32
    }

    static func header(_ p: UnsafeRawPointer, blockSize: Int) -> Header? {
        guard blockSize >= 512, p.loadUnaligned(as: UInt64.self) == headerSignature else { return nil }
        let size = Int(UInt32(littleEndian: p.loadUnaligned(fromByteOffset: 12, as: UInt32.self)))
        guard size >= headerSize, size <= blockSize else { return nil }
        // The CRC is over the header with its own CRC field taken as zero.
        let stored = UInt32(littleEndian: p.loadUnaligned(fromByteOffset: 16, as: UInt32.self))
        var zero: UInt32 = 0
        var crc = crc32(p, 16)
        crc = withUnsafeBytes(of: &zero) { crc32($0.baseAddress!, 4, crc) }
        crc = crc32(p + 20, size - 20, crc)
        guard crc == stored else { return nil }
        let count = Int(UInt32(littleEndian: p.loadUnaligned(fromByteOffset: 80, as: UInt32.self)))
        let entry = Int(UInt32(littleEndian: p.loadUnaligned(fromByteOffset: 84, as: UInt32.self)))
        guard entry >= entrySize, entry % 8 == 0, entry <= 4096, count >= 1, count <= 1024 else { return nil }
        return Header(entriesLBA: UInt64(littleEndian: p.loadUnaligned(fromByteOffset: 72, as: UInt64.self)),
                      entryCount: count, entrySize: entry,
                      entriesCRC: UInt32(littleEndian: p.loadUnaligned(fromByteOffset: 88, as: UInt32.self)))
    }

    /// The first partition of `type` in the entry array (1-based index, as
    /// the kernel numbers diskNsM), with its unique GUID; nil if the array's
    /// CRC is wrong or there is none.
    static func find(_ type: UUID16, entries p: UnsafeRawPointer, _ h: Header) -> (index: Int, unique: UUID16)? {
        guard crc32(p, h.entryCount * h.entrySize) == h.entriesCRC else { return nil }
        for i in 0..<h.entryCount {
            let e = p + i * h.entrySize
            if guid(at: e) == type {
                return (i + 1, guid(at: e + 16))
            }
        }
        return nil
    }

    // MARK: UUID text

    /// 36 characters, upper case, as uuid_unparse writes them.
    static let textLength = 36

    static func writeText(_ u: UUID16, into p: UnsafeMutableRawPointer) {
        var out = 0
        for i in 0..<16 {
            if i == 4 || i == 6 || i == 8 || i == 10 {
                p.storeBytes(of: UInt8(ascii: "-"), toByteOffset: out, as: UInt8.self)
                out += 1
            }
            let byte = UInt8(truncatingIfNeeded: (i < 8 ? u.hi >> (56 - 8 * i) : u.lo >> (56 - 8 * (i - 8))))
            p.storeBytes(of: hexDigit(byte >> 4), toByteOffset: out, as: UInt8.self)
            p.storeBytes(of: hexDigit(byte & 0xf), toByteOffset: out + 1, as: UInt8.self)
            out += 2
        }
    }

    static func hexDigit(_ n: UInt8) -> UInt8 { n < 10 ? 48 + n : 55 + n }

    /// A UUID's text in either case, exactly 36 characters.
    static func parseText(_ p: UnsafeRawPointer, _ count: Int) -> UUID16? {
        guard count == textLength else { return nil }
        var hi: UInt64 = 0, lo: UInt64 = 0, digits = 0
        for i in 0..<count {
            let c = p.load(fromByteOffset: i, as: UInt8.self)
            if i == 8 || i == 13 || i == 18 || i == 23 {
                guard c == UInt8(ascii: "-") else { return nil }
                continue
            }
            let v: UInt64
            switch c {
            case 48...57: v = UInt64(c - 48)
            case 65...70: v = UInt64(c - 55)
            case 97...102: v = UInt64(c - 87)
            default: return nil
            }
            if digits < 16 { hi = hi << 4 | v } else { lo = lo << 4 | v }
            digits += 1
        }
        return UUID16(hi: hi, lo: lo)
    }
}
