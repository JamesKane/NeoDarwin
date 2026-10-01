// SPDX-License-Identifier: BSD-2-Clause
//
// hfsowners: ownership and modes in an HFS+ volume image's catalog, which
// hdiutil(1) can't give (docs/base/session.md, "PAM"). hdiutil create
// -srcfolder, run as the build user, records that user's uid and gid as every
// file's owner and drops set-user-ID bits, so a setuid-root program such as
// su(1) ran as the build user. This tool rewrites the catalog records'
// HFSPlusBSDInfo in place: every file and folder becomes root:wheel (0:0),
// as a macOS system volume's are, then --owner and --mode set single paths.
//
// Usage:
//     hfsowners set IMAGE [--owner PATH UID:GID]... [--mode PATH OCTAL]...
//         PATH is relative to the volume's root, without a leading slash.
//         --mode sets the permission bits (07777), keeping the file type.
//     hfsowners list IMAGE
//         Prints "UID:GID MODE PATH" for every file and folder (MODE octal,
//         with the file type), sorted by path.
//
// The image is a bare HFS+ (or journaled HFS+) volume, as tools/ramdisk/
// mkhfs.sh writes: the volume header at byte 1024, the catalog B-tree in
// the extents its fork data lists (a catalog in the extents overflow file is
// refused). Layouts from Apple's TN1150 (hfs_format.h).

import Foundation

func fail(_ message: String, code: Int32 = 1) -> Never {
    FileHandle.standardError.write(Data("hfsowners: \(message)\n".utf8))
    exit(code)
}

let usage = """
    usage: hfsowners set IMAGE [--owner PATH UID:GID]... [--mode PATH OCTAL]...
           hfsowners list IMAGE
    """

func be16(_ b: [UInt8], _ o: Int) -> UInt16 { UInt16(b[o]) << 8 | UInt16(b[o + 1]) }
func be32(_ b: [UInt8], _ o: Int) -> UInt32 { UInt32(be16(b, o)) << 16 | UInt32(be16(b, o + 2)) }
func put16(_ b: inout [UInt8], _ o: Int, _ v: UInt16) { b[o] = UInt8(v >> 8); b[o + 1] = UInt8(v & 0xff) }
func put32(_ b: inout [UInt8], _ o: Int, _ v: UInt32) { put16(&b, o, UInt16(v >> 16)); put16(&b, o + 2, UInt16(v & 0xffff)) }

/// The catalog file's bytes, read from and written back to its extents.
struct Catalog {
    let image: FileHandle
    let extents: [(offset: UInt64, length: Int)]
    var bytes: [UInt8]

    init(path: String) {
        guard let h = FileHandle(forUpdatingAtPath: path) else { fail("cannot open \(path)") }
        image = h
        try? h.seek(toOffset: 1024)
        let vh = [UInt8](h.readData(ofLength: 512))
        guard vh.count == 512, be16(vh, 0) == 0x482B || be16(vh, 0) == 0x4858 else {
            fail("\(path): no HFS+ volume header at byte 1024")
        }
        let blockSize = UInt64(be32(vh, 40))
        let fork = 272  // HFSPlusVolumeHeader.catalogFile
        let totalBlocks = be32(vh, fork + 12)
        var ext: [(UInt64, Int)] = []
        var blocks: UInt32 = 0
        for i in 0..<8 {
            let start = be32(vh, fork + 16 + 8 * i), count = be32(vh, fork + 20 + 8 * i)
            if count == 0 { break }
            ext.append((UInt64(start) * blockSize, Int(UInt64(count) * blockSize)))
            blocks += count
        }
        guard blocks == totalBlocks else { fail("\(path): the catalog continues in the extents overflow file") }
        extents = ext
        var all: [UInt8] = []
        for (offset, length) in ext {
            try? h.seek(toOffset: offset)
            let d = h.readData(ofLength: length)
            guard d.count == length else { fail("\(path): short catalog read") }
            all += [UInt8](d)
        }
        bytes = all
    }

    func write() {
        var at = 0
        for (offset, length) in extents {
            try? image.seek(toOffset: offset)
            image.write(Data(bytes[at..<at + length]))
            at += length
        }
        try? image.synchronize()
    }
}

/// A file or folder record: its CNID, parent, name and where its
/// HFSPlusBSDInfo is in the catalog's bytes.
struct Record {
    let cnid: UInt32
    let parent: UInt32
    let name: String
    let bsdInfo: Int
}

func records(_ cat: Catalog) -> [Record] {
    let b = cat.bytes
    // The header node: BTHeaderRec after the 14-byte node descriptor.
    let nodeSize = Int(be16(b, 14 + 18))
    var node = be32(b, 14 + 10)  // firstLeafNode
    var out: [Record] = []
    var seen = Set<UInt32>()
    while node != 0 {
        guard seen.insert(node).inserted, (Int(node) + 1) * nodeSize <= b.count else { fail("bad leaf chain at node \(node)") }
        let n = Int(node) * nodeSize
        guard Int8(bitPattern: b[n + 8]) == -1 else { fail("node \(node) in the leaf chain isn't a leaf") }
        for i in 0..<Int(be16(b, n + 10)) {
            let r = n + Int(be16(b, n + nodeSize - 2 * (i + 1)))
            let keyLength = Int(be16(b, r))
            let parent = be32(b, r + 2)
            let nameLength = Int(be16(b, r + 6))
            var units: [UInt16] = []
            for c in 0..<nameLength { units.append(be16(b, r + 8 + 2 * c)) }
            let data = r + 2 + keyLength
            let type = be16(b, data)
            guard type == 1 || type == 2 else { continue }  // folder, file; not threads
            out.append(Record(cnid: be32(b, data + 8), parent: parent, name: String(decoding: units, as: UTF16.self),
                              bsdInfo: data + 32))
        }
        node = be32(b, n)  // fLink
    }
    return out
}

/// Each record's path from the volume's root ("" for the root folder, CNID 2).
func paths(_ recs: [Record]) -> [String: Record] {
    var byID: [UInt32: Record] = [:]
    for r in recs { byID[r.cnid] = r }
    func path(_ id: UInt32, _ depth: Int) -> String {
        guard id != 2 else { return "" }
        guard depth < 256, let r = byID[id] else { fail("no folder record for CNID \(id)") }
        let p = path(r.parent, depth + 1)
        return p.isEmpty ? r.name : p + "/" + r.name
    }
    var out: [String: Record] = [:]
    for r in recs { out[path(r.cnid, 0)] = r }
    return out
}

func trimmed(_ p: String) -> String { p.trimmingCharacters(in: CharacterSet(charactersIn: "/")) }

var args = Array(CommandLine.arguments.dropFirst())
guard args.count >= 2 else { fail(usage, code: 2) }
let command = args.removeFirst(), imagePath = args.removeFirst()
var cat = Catalog(path: imagePath)
let byPath = paths(records(cat))

switch command {
case "list":
    guard args.isEmpty else { fail(usage, code: 2) }
    for p in byPath.keys.sorted() {
        let o = byPath[p]!.bsdInfo
        print("\(be32(cat.bytes, o)):\(be32(cat.bytes, o + 4)) \(String(be16(cat.bytes, o + 10), radix: 8)) /\(p)")
    }
case "set":
    for r in byPath.values {
        put32(&cat.bytes, r.bsdInfo, 0)
        put32(&cat.bytes, r.bsdInfo + 4, 0)
    }
    while !args.isEmpty {
        let opt = args.removeFirst()
        guard args.count >= 2 else { fail(usage, code: 2) }
        let p = trimmed(args.removeFirst()), value = args.removeFirst()
        guard let r = byPath[p] else { fail("\(imagePath): no /\(p)") }
        switch opt {
        case "--owner":
            let ids = value.split(separator: ":").compactMap { UInt32($0) }
            guard ids.count == 2 else { fail("bad owner \(value) (UID:GID)") }
            put32(&cat.bytes, r.bsdInfo, ids[0])
            put32(&cat.bytes, r.bsdInfo + 4, ids[1])
        case "--mode":
            guard let m = UInt16(value, radix: 8), m <= 0o7777 else { fail("bad mode \(value)") }
            put16(&cat.bytes, r.bsdInfo + 10, be16(cat.bytes, r.bsdInfo + 10) & 0o170000 | m)
        default:
            fail(usage, code: 2)
        }
    }
    cat.write()
default:
    fail(usage, code: 2)
}
