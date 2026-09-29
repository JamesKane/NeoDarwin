// SPDX-License-Identifier: BSD-2-Clause
//
// gptimage: a raw disk image with a GUID partition table around given
// partition images (docs/kernel/storage.md, "The disk image"). Everything is
// derived from the arguments, so the same inputs give the same bytes: the
// disk and partition GUIDs are name-based (version 5) UUIDs of a seed,
// normally the Bazel target's label, which makes each partition's unique
// GUID, and so neoboot's boot-uuid for it, stable across builds.
//
// Usage:
//     gptimage OUT --seed TEXT [--uuids FILE] --partition TYPE NAME IMAGE...
//   TYPE is esp, hfs, or a type GUID; NAME the partition's name (UTF-16 in
//   the entry, at most 36 characters). Partitions follow one another from
//   1 MiB, each starting on a MiB boundary and as long as its image rounded
//   up to one; the backup table fills the last MiB. --uuids writes
//   "disk GUID", then "N GUID TYPE NAME" per partition.
//
// The layout is UEFI 2.10 §5: a protective MBR at LBA 0, the header at
// LBA 1 and 128 entries of 128 bytes from LBA 2, their copies at the end
// with the backup header in the last block. 512-byte blocks, as virtio-blk
// and QEMU's NVMe present by default. GPT.swift (neoboot's reader) supplies
// the GUID layout and the CRC.

import Foundation

let blockSize = 512
let entryCount = 128
let entryBlocks = entryCount * GPT.entrySize / blockSize   // 32
let mib = 1 << 20

func fail(_ message: String, code: Int32 = 1) -> Never {
    FileHandle.standardError.write(Data("gptimage: \(message)\n".utf8))
    exit(code)
}

// MARK: SHA-1 (FIPS 180-4), for name-based UUIDs.

func sha1(_ message: [UInt8]) -> [UInt8] {
    var h: [UInt32] = [0x6745_2301, 0xEFCD_AB89, 0x98BA_DCFE, 0x1032_5476, 0xC3D2_E1F0]
    var m = message
    let bits = UInt64(message.count) * 8
    m.append(0x80)
    while m.count % 64 != 56 { m.append(0) }
    for i in (0..<8).reversed() { m.append(UInt8(truncatingIfNeeded: bits >> (8 * UInt64(i)))) }
    func rotl(_ x: UInt32, _ n: UInt32) -> UInt32 { x << n | x >> (32 - n) }
    for chunk in stride(from: 0, to: m.count, by: 64) {
        var w = [UInt32](repeating: 0, count: 80)
        for i in 0..<16 {
            w[i] = UInt32(m[chunk + 4 * i]) << 24 | UInt32(m[chunk + 4 * i + 1]) << 16
                | UInt32(m[chunk + 4 * i + 2]) << 8 | UInt32(m[chunk + 4 * i + 3])
        }
        for i in 16..<80 { w[i] = rotl(w[i - 3] ^ w[i - 8] ^ w[i - 14] ^ w[i - 16], 1) }
        var (a, b, c, d, e) = (h[0], h[1], h[2], h[3], h[4])
        for i in 0..<80 {
            let (f, k): (UInt32, UInt32)
            switch i {
            case 0..<20: (f, k) = ((b & c) | (~b & d), 0x5A82_7999)
            case 20..<40: (f, k) = (b ^ c ^ d, 0x6ED9_EBA1)
            case 40..<60: (f, k) = ((b & c) | (b & d) | (c & d), 0x8F1B_BCDC)
            default: (f, k) = (b ^ c ^ d, 0xCA62_C1D6)
            }
            let t = rotl(a, 5) &+ f &+ e &+ k &+ w[i]
            (e, d, c, b, a) = (d, c, rotl(b, 30), a, t)
        }
        h[0] &+= a; h[1] &+= b; h[2] &+= c; h[3] &+= d; h[4] &+= e
    }
    return h.flatMap { v in (0..<4).map { UInt8(truncatingIfNeeded: v >> (24 - 8 * UInt32($0))) } }
}

/// RFC 9562 §5.5: SHA-1 of the name space's bytes and the name, version 5.
func uuid5(_ space: UUID16, _ name: String) -> UUID16 {
    var bytes: [UInt8] = []
    for i in 0..<8 { bytes.append(UInt8(truncatingIfNeeded: space.hi >> (56 - 8 * UInt64(i)))) }
    for i in 0..<8 { bytes.append(UInt8(truncatingIfNeeded: space.lo >> (56 - 8 * UInt64(i)))) }
    var d = sha1(bytes + Array(name.utf8))
    d[6] = (d[6] & 0x0f) | 0x50
    d[8] = (d[8] & 0x3f) | 0x80
    let hi = d[0..<8].reduce(UInt64(0)) { $0 << 8 | UInt64($1) }
    let lo = d[8..<16].reduce(UInt64(0)) { $0 << 8 | UInt64($1) }
    return UUID16(hi: hi, lo: lo)
}

func text(_ u: UUID16) -> String {
    var buf = [UInt8](repeating: 0, count: GPT.textLength)
    buf.withUnsafeMutableBytes { GPT.writeText(u, into: $0.baseAddress!) }
    return String(decoding: buf, as: UTF8.self)
}

/// NeoDarwin's name space for disk images: the DNS name space's UUID of
/// "gptimage.neodarwin".
let dnsSpace = UUID16(hi: 0x6BA7_B810_9DAD_11D1, lo: 0x80B4_00C0_4FD4_30C8)
let imageSpace = uuid5(dnsSpace, "gptimage.neodarwin")

// MARK: arguments

struct Partition {
    var type: UUID16
    var typeName: String
    var name: String
    var path: String
    var bytes: Int = 0
    var firstLBA = 0, lastLBA = 0
    var unique = UUID16(hi: 0, lo: 0)
}

let usage = "usage: gptimage OUT --seed TEXT [--uuids FILE] --partition TYPE NAME IMAGE..."
var args = Array(CommandLine.arguments.dropFirst())
guard !args.isEmpty else { fail(usage, code: 2) }
let out = args.removeFirst()
var seed: String?
var uuidsFile: String?
var partitions: [Partition] = []
while !args.isEmpty {
    switch args.removeFirst() {
    case "--seed":
        guard !args.isEmpty else { fail(usage, code: 2) }
        seed = args.removeFirst()
    case "--uuids":
        guard !args.isEmpty else { fail(usage, code: 2) }
        uuidsFile = args.removeFirst()
    case "--partition":
        guard args.count >= 3 else { fail(usage, code: 2) }
        let t = args.removeFirst(), name = args.removeFirst(), path = args.removeFirst()
        let type: UUID16
        switch t {
        case "esp": type = GPT.espType
        case "hfs": type = GPT.hfsPlusType
        default:
            let u = Array(t.utf8)
            guard let parsed = u.withUnsafeBytes({ GPT.parseText($0.baseAddress!, $0.count) }) else { fail("bad type \(t)", code: 2) }
            type = parsed
        }
        guard name.utf16.count <= 36 else { fail("partition name longer than 36 characters: \(name)", code: 2) }
        partitions.append(Partition(type: type, typeName: t, name: name, path: path))
    default:
        fail(usage, code: 2)
    }
}
guard let seed, !partitions.isEmpty else { fail(usage, code: 2) }

// MARK: layout

var lba = mib / blockSize
for i in partitions.indices {
    guard let attributes = try? FileManager.default.attributesOfItem(atPath: partitions[i].path),
          let size = attributes[.size] as? Int, size > 0 else { fail("cannot read \(partitions[i].path)") }
    let rounded = (size + mib - 1) / mib * mib
    partitions[i].bytes = size
    partitions[i].firstLBA = lba
    partitions[i].lastLBA = lba + rounded / blockSize - 1
    partitions[i].unique = uuid5(imageSpace, "\(seed)#partition-\(i + 1)")
    lba += rounded / blockSize
}
let totalBlocks = lba + mib / blockSize
let lastLBA = totalBlocks - 1
let diskGUID = uuid5(imageSpace, "\(seed)#disk")

// Partition entries, once for both tables.
var entries = [UInt8](repeating: 0, count: entryCount * GPT.entrySize)
entries.withUnsafeMutableBytes { e in
    for (i, p) in partitions.enumerated() {
        let at = e.baseAddress! + i * GPT.entrySize
        GPT.storeGUID(p.type, at: at)
        GPT.storeGUID(p.unique, at: at + 16)
        at.storeBytes(of: UInt64(p.firstLBA).littleEndian, toByteOffset: 32, as: UInt64.self)
        at.storeBytes(of: UInt64(p.lastLBA).littleEndian, toByteOffset: 40, as: UInt64.self)
        for (j, unit) in p.name.utf16.enumerated() {
            at.storeBytes(of: unit.littleEndian, toByteOffset: 56 + 2 * j, as: UInt16.self)
        }
    }
}
let entriesCRC = entries.withUnsafeBytes { GPT.crc32($0.baseAddress!, $0.count) }

func header(myLBA: Int, alternateLBA: Int, entriesLBA: Int) -> [UInt8] {
    var h = [UInt8](repeating: 0, count: blockSize)
    h.withUnsafeMutableBytes { raw in
        let p = raw.baseAddress!
        p.storeBytes(of: GPT.headerSignature.littleEndian, as: UInt64.self)
        p.storeBytes(of: UInt32(0x0001_0000).littleEndian, toByteOffset: 8, as: UInt32.self)    // revision 1.0
        p.storeBytes(of: UInt32(GPT.headerSize).littleEndian, toByteOffset: 12, as: UInt32.self)
        p.storeBytes(of: UInt64(myLBA).littleEndian, toByteOffset: 24, as: UInt64.self)
        p.storeBytes(of: UInt64(alternateLBA).littleEndian, toByteOffset: 32, as: UInt64.self)
        p.storeBytes(of: UInt64(2 + entryBlocks).littleEndian, toByteOffset: 40, as: UInt64.self)          // first usable
        p.storeBytes(of: UInt64(lastLBA - 1 - entryBlocks).littleEndian, toByteOffset: 48, as: UInt64.self) // last usable
        GPT.storeGUID(diskGUID, at: p + 56)
        p.storeBytes(of: UInt64(entriesLBA).littleEndian, toByteOffset: 72, as: UInt64.self)
        p.storeBytes(of: UInt32(entryCount).littleEndian, toByteOffset: 80, as: UInt32.self)
        p.storeBytes(of: UInt32(GPT.entrySize).littleEndian, toByteOffset: 84, as: UInt32.self)
        p.storeBytes(of: entriesCRC.littleEndian, toByteOffset: 88, as: UInt32.self)
        let crc = GPT.crc32(p, GPT.headerSize)
        p.storeBytes(of: crc.littleEndian, toByteOffset: 16, as: UInt32.self)
    }
    return h
}

// The image is written in place: a sparse file of the disk's size, then
// each structure and partition at its block.
_ = FileManager.default.createFile(atPath: out, contents: nil)
guard let file = FileHandle(forWritingAtPath: out) else { fail("cannot write \(out)") }
do {
    try file.truncate(atOffset: UInt64(totalBlocks * blockSize))
} catch {
    fail("cannot size \(out): \(error)")
}

func put(_ bytes: [UInt8], atLBA l: Int) {
    do {
        try file.seek(toOffset: UInt64(l * blockSize))
        try file.write(contentsOf: bytes)
    } catch {
        fail("cannot write \(out): \(error)")
    }
}

// Protective MBR: one partition of type 0xEE over the whole disk.
var mbr = [UInt8](repeating: 0, count: blockSize)
mbr[446 + 1] = 0x00; mbr[446 + 2] = 0x02; mbr[446 + 3] = 0x00   // CHS of LBA 1
mbr[446 + 4] = 0xEE
mbr[446 + 5] = 0xFF; mbr[446 + 6] = 0xFF; mbr[446 + 7] = 0xFF
let mbrSize = UInt32(min(totalBlocks - 1, Int(UInt32.max)))
for i in 0..<4 { mbr[446 + 8 + i] = UInt8(truncatingIfNeeded: 1 >> (8 * i)) }
for i in 0..<4 { mbr[446 + 12 + i] = UInt8(truncatingIfNeeded: mbrSize >> (8 * UInt32(i))) }
mbr[510] = 0x55; mbr[511] = 0xAA
put(mbr, atLBA: 0)
put(header(myLBA: 1, alternateLBA: lastLBA, entriesLBA: 2), atLBA: 1)
put(entries, atLBA: 2)
put(entries, atLBA: lastLBA - entryBlocks)
put(header(myLBA: lastLBA, alternateLBA: 1, entriesLBA: lastLBA - entryBlocks), atLBA: lastLBA)
for p in partitions {
    guard let data = FileManager.default.contents(atPath: p.path) else { fail("cannot read \(p.path)") }
    put([UInt8](data), atLBA: p.firstLBA)
}
try? file.close()

var report = "disk \(text(diskGUID))\n"
for (i, p) in partitions.enumerated() {
    report += "\(i + 1) \(text(p.unique)) \(p.typeName) \(p.name)\n"
}
if let uuidsFile {
    guard FileManager.default.createFile(atPath: uuidsFile, contents: Data(report.utf8)) else { fail("cannot write \(uuidsFile)") }
}
print(report, terminator: "")
