// SPDX-License-Identifier: BSD-2-Clause
//
// Reading Linux acpidump's text format, which neoboot's dump-acpi option
// prints: a "SIG @ 0xADDRESS" line per table, then rows of
// "    OFFSET: HH HH ... (up to 16)  ascii", then a blank line.

/// The tables of one dump, each at the physical address it was dumped from.
struct AcpiDump: ACPIMemory {
    struct Table {
        let signature: String
        let address: UInt64
        let bytes: UnsafeMutableRawPointer  // never freed: the tool is short-lived
        let length: Int
    }

    private(set) var tables: [Table] = []
    var rsdp: UInt64? { tables.first { $0.signature == "RSDP" }?.address }

    func bytes(at address: UInt64, count: Int) -> UnsafeRawPointer? {
        for t in tables where address >= t.address && address - t.address + UInt64(count) <= UInt64(t.length) {
            return UnsafeRawPointer(t.bytes + Int(address - t.address))
        }
        return nil
    }

    struct ParseError: Error { let line: Int; let message: String }

    init(text: String) throws(ParseError) {
        var signature: String?
        var address: UInt64 = 0
        var data: [UInt8] = []
        func flush() {
            guard let s = signature else { return }
            let p = UnsafeMutableRawPointer.allocate(byteCount: max(data.count, 1), alignment: 16)
            data.withUnsafeBytes { p.copyMemory(from: $0.baseAddress!, byteCount: data.count) }
            tables.append(Table(signature: s, address: address, bytes: p, length: data.count))
            signature = nil
            data = []
        }
        for (n, raw) in text.split(separator: "\n", omittingEmptySubsequences: false).enumerated() {
            let line = Array(raw.utf8.filter { $0 != 13 })
            if line.allSatisfy({ $0 == 32 }) { continue }
            if let at = Self.find(line, Array(" @ 0x".utf8)), at <= 4 {
                flush()
                signature = String(decoding: line[0..<at], as: UTF8.self)
                guard let a = UInt64(String(decoding: line[(at + 5)...], as: UTF8.self).trimmingSpaces, radix: 16) else {
                    throw ParseError(line: n + 1, message: "bad table address")
                }
                address = a
                continue
            }
            guard signature != nil, let colon = line.firstIndex(of: 58) else {
                throw ParseError(line: n + 1, message: "not a table header or a hex row")
            }
            guard let offset = Int(String(decoding: line[..<colon], as: UTF8.self).trimmingSpaces, radix: 16), offset == data.count else {
                throw ParseError(line: n + 1, message: "row offset is not where the previous row ended")
            }
            // Up to sixteen "HH " groups; the ASCII column follows after padding.
            var i = colon + 2
            var row = 0
            while row < 16, i + 1 < line.count, let hi = Self.hex(line[i]), let lo = Self.hex(line[i + 1]),
                  i + 2 == line.count || line[i + 2] == 32 {
                data.append(hi << 4 | lo)
                i += 3
                row += 1
            }
            guard row > 0 else { throw ParseError(line: n + 1, message: "a row with no bytes") }
        }
        flush()
    }

    static func hex(_ c: UInt8) -> UInt8? {
        switch c {
        case 48...57: return c - 48
        case 65...70: return c - 55
        case 97...102: return c - 87
        default: return nil
        }
    }

    static func find(_ hay: [UInt8], _ needle: [UInt8]) -> Int? {
        guard hay.count >= needle.count else { return nil }
        for i in 0...(hay.count - needle.count) where Array(hay[i..<(i + needle.count)]) == needle { return i }
        return nil
    }
}

/// Reads the relocated copy first, then the dump (for the FACS, which stays put).
struct LayeredMemory: ACPIMemory {
    let copyAddress: UInt64
    let copy: UnsafeRawPointer
    let copyLength: Int
    let dump: AcpiDump

    func bytes(at address: UInt64, count: Int) -> UnsafeRawPointer? {
        if address >= copyAddress, address - copyAddress + UInt64(count) <= UInt64(copyLength) {
            return copy + Int(address - copyAddress)
        }
        return dump.bytes(at: address, count: count)
    }
}

extension String {
    var trimmingSpaces: String {
        var s = Substring(self)
        while s.first == " " { s.removeFirst() }
        while s.last == " " { s.removeLast() }
        return String(s)
    }
}
