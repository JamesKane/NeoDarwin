// SPDX-License-Identifier: BSD-2-Clause
//
// A property-list parser for launchctl: the XML format (Apple's
// PropertyList-1.0 DTD) that LaunchDaemon plists use, without Foundation.
// launchctl-842 read them with CoreFoundation, which NeoDarwin doesn't
// build (docs/base/session.md).

import Launch

enum Plist {
    case dictionary([(key: String, value: Plist)])
    case array([Plist])
    case string(String)
    case integer(Int64)
    case real(Double)
    case boolean(Bool)
    case date(String)
    case data([UInt8])

    subscript(key: String) -> Plist? {
        guard case .dictionary(let entries) = self else { return nil }
        for entry in entries where entry.key == key { return entry.value }
        return nil
    }

    var stringValue: String? {
        if case .string(let s) = self { return s }
        return nil
    }

    var integerValue: Int64? {
        if case .integer(let i) = self { return i }
        return nil
    }

    var boolValue: Bool? {
        if case .boolean(let b) = self { return b }
        return nil
    }
}

struct PlistError: Error {
    let message: String
    let offset: Int
}

/// Parses an XML property list.
func parsePlist(_ bytes: [UInt8]) throws(PlistError) -> Plist {
    var parser = PlistParser(bytes: bytes)
    return try parser.document()
}

private struct PlistParser {
    let bytes: [UInt8]
    var pos = 0

    init(bytes: [UInt8]) { self.bytes = bytes }

    func fail(_ message: String) -> PlistError { PlistError(message: message, offset: pos) }

    mutating func document() throws(PlistError) -> Plist {
        // Prolog: the XML declaration, a DOCTYPE and comments, then <plist>.
        while true {
            skipSpace()
            if starts(with: "<?") { try skip(past: "?>") }
            else if starts(with: "<!--") { try skip(past: "-->") }
            else if starts(with: "<!") { try skip(past: ">") }
            else { break }
        }
        let open = try tag()
        guard open.name == "plist", !open.closing else { throw fail("expected <plist>") }
        if open.empty { throw fail("empty <plist/>") }
        let value = try element()
        let close = try tag()
        guard close.name == "plist", close.closing else { throw fail("expected </plist>") }
        return value
    }

    // One tag: <name attrs>, </name> or <name/>, skipping comments before it.
    mutating func tag() throws(PlistError) -> (name: String, closing: Bool, empty: Bool) {
        while true {
            skipSpace()
            guard starts(with: "<!--") else { break }
            try skip(past: "-->")
        }
        guard pos < bytes.count, bytes[pos] == UInt8(ascii: "<") else { throw fail("expected a tag") }
        pos += 1
        let closing = pos < bytes.count && bytes[pos] == UInt8(ascii: "/")
        if closing { pos += 1 }
        let start = pos
        while pos < bytes.count, !isSpace(bytes[pos]), bytes[pos] != UInt8(ascii: ">"), bytes[pos] != UInt8(ascii: "/") {
            pos += 1
        }
        let name = String(decoding: bytes[start..<pos], as: UTF8.self)
        while pos < bytes.count, bytes[pos] != UInt8(ascii: ">") { pos += 1 }   // attributes
        guard pos < bytes.count else { throw fail("unterminated tag") }
        let empty = pos > start && bytes[pos - 1] == UInt8(ascii: "/")
        pos += 1
        return (name, closing, empty)
    }

    mutating func element() throws(PlistError) -> Plist {
        let open = try tag()
        guard !open.closing else { throw fail("unexpected </\(open.name)>") }
        switch open.name {
        case "dict":
            var entries: [(key: String, value: Plist)] = []
            if open.empty { return .dictionary(entries) }
            while true {
                let save = pos
                let t = try tag()
                if t.closing && t.name == "dict" { return .dictionary(entries) }
                guard t.name == "key", !t.closing else { pos = save; throw fail("expected <key>") }
                let key = t.empty ? "" : try text(until: "key")
                entries.append((key, try element()))
            }
        case "array":
            var items: [Plist] = []
            if open.empty { return .array(items) }
            while true {
                let save = pos
                let t = try tag()
                if t.closing && t.name == "array" { return .array(items) }
                pos = save
                items.append(try element())
            }
        case "true", "false":
            if !open.empty {
                let close = try tag()
                guard close.closing, close.name == open.name else { throw fail("expected </\(open.name)>") }
            }
            return .boolean(open.name == "true")
        case "string":
            return .string(open.empty ? "" : try text(until: "string"))
        case "integer":
            let s = open.empty ? "" : try text(until: "integer")
            guard let v = parseInteger(s) else { throw fail("bad <integer> \(s)") }
            return .integer(v)
        case "real":
            let s = open.empty ? "" : try text(until: "real")
            return .real(parseReal(s))
        case "date":
            return .date(open.empty ? "" : try text(until: "date"))
        case "data":
            let s = open.empty ? "" : try text(until: "data")
            guard let d = decodeBase64(s) else { throw fail("bad <data>") }
            return .data(d)
        default:
            throw fail("unknown element <\(open.name)>")
        }
    }

    // Character data up to </name>, with entities decoded.
    mutating func text(until name: String) throws(PlistError) -> String {
        var out: [UInt8] = []
        while pos < bytes.count, bytes[pos] != UInt8(ascii: "<") {
            if bytes[pos] == UInt8(ascii: "&") {
                guard let semi = bytes[pos...].firstIndex(of: UInt8(ascii: ";")) else { throw fail("bad entity") }
                let entity = String(decoding: bytes[(pos + 1)..<semi], as: UTF8.self)
                switch entity {
                case "amp": out.append(UInt8(ascii: "&"))
                case "lt": out.append(UInt8(ascii: "<"))
                case "gt": out.append(UInt8(ascii: ">"))
                case "quot": out.append(UInt8(ascii: "\""))
                case "apos": out.append(UInt8(ascii: "'"))
                default:
                    guard let scalar = characterReference(entity) else { throw fail("unknown entity &\(entity);") }
                    appendUTF8(scalar, to: &out)
                }
                pos = semi + 1
            } else {
                out.append(bytes[pos])
                pos += 1
            }
        }
        let close = try tag()
        guard close.closing, close.name == name else { throw fail("expected </\(name)>") }
        return String(decoding: out, as: UTF8.self)
    }

    func starts(with s: StaticString) -> Bool {
        let n = s.utf8CodeUnitCount
        guard pos + n <= bytes.count else { return false }
        for i in 0..<n where bytes[pos + i] != s.utf8Start[i] { return false }
        return true
    }

    mutating func skip(past s: StaticString) throws(PlistError) {
        while pos < bytes.count {
            if starts(with: s) { pos += s.utf8CodeUnitCount; return }
            pos += 1
        }
        throw fail("unterminated markup")
    }

    mutating func skipSpace() {
        while pos < bytes.count, isSpace(bytes[pos]) { pos += 1 }
    }
}

private func isSpace(_ b: UInt8) -> Bool {
    b == 0x20 || b == 0x09 || b == 0x0a || b == 0x0d
}

private func trimmed(_ s: String) -> [UInt8] {
    var u = Array(s.utf8)
    while let last = u.last, isSpace(last) { u.removeLast() }
    var i = 0
    while i < u.count, isSpace(u[i]) { i += 1 }
    return Array(u[i...])
}

private func parseInteger(_ s: String) -> Int64? {
    var u = trimmed(s)[...]
    var negative = false
    if let first = u.first, first == UInt8(ascii: "-") || first == UInt8(ascii: "+") {
        negative = first == UInt8(ascii: "-")
        u = u.dropFirst()
    }
    var radix: Int64 = 10
    if u.count > 2, u.first == UInt8(ascii: "0"), u.dropFirst().first == UInt8(ascii: "x") || u.dropFirst().first == UInt8(ascii: "X") {
        radix = 16
        u = u.dropFirst(2)
    }
    guard !u.isEmpty else { return nil }
    var value: Int64 = 0
    for c in u {
        let digit: Int64
        switch c {
        case UInt8(ascii: "0")...UInt8(ascii: "9"): digit = Int64(c - UInt8(ascii: "0"))
        case UInt8(ascii: "a")...UInt8(ascii: "f") where radix == 16: digit = Int64(c - UInt8(ascii: "a") + 10)
        case UInt8(ascii: "A")...UInt8(ascii: "F") where radix == 16: digit = Int64(c - UInt8(ascii: "A") + 10)
        default: return nil
        }
        let (m, o1) = value.multipliedReportingOverflow(by: radix)
        let (a, o2) = m.addingReportingOverflow(digit)
        if o1 || o2 { return nil }
        value = a
    }
    return negative ? -value : value
}

private func parseReal(_ s: String) -> Double {
    var u = trimmed(s)
    u.append(0)
    return u.withUnsafeBufferPointer { strtod_shim($0.baseAddress!) }
}

private func strtod_shim(_ p: UnsafePointer<UInt8>) -> Double {
    p.withMemoryRebound(to: CChar.self, capacity: 1) { strtod($0, nil) }
}

private func characterReference(_ entity: String) -> UInt32? {
    let u = Array(entity.utf8)
    guard u.count > 1, u[0] == UInt8(ascii: "#") else { return nil }
    let hex = u[1] == UInt8(ascii: "x")
    var value: UInt32 = 0
    for c in u[(hex ? 2 : 1)...] {
        let digit: UInt32
        switch c {
        case UInt8(ascii: "0")...UInt8(ascii: "9"): digit = UInt32(c - UInt8(ascii: "0"))
        case UInt8(ascii: "a")...UInt8(ascii: "f") where hex: digit = UInt32(c - UInt8(ascii: "a") + 10)
        case UInt8(ascii: "A")...UInt8(ascii: "F") where hex: digit = UInt32(c - UInt8(ascii: "A") + 10)
        default: return nil
        }
        value = value * (hex ? 16 : 10) + digit
        if value > 0x10FFFF { return nil }
    }
    return value
}

private func appendUTF8(_ v: UInt32, to out: inout [UInt8]) {
    switch v {
    case 0..<0x80:
        out.append(UInt8(v))
    case 0x80..<0x800:
        out.append(UInt8(0xC0 | (v >> 6)))
        out.append(UInt8(0x80 | (v & 0x3F)))
    case 0x800..<0x10000:
        out.append(UInt8(0xE0 | (v >> 12)))
        out.append(UInt8(0x80 | ((v >> 6) & 0x3F)))
        out.append(UInt8(0x80 | (v & 0x3F)))
    default:
        out.append(UInt8(0xF0 | (v >> 18)))
        out.append(UInt8(0x80 | ((v >> 12) & 0x3F)))
        out.append(UInt8(0x80 | ((v >> 6) & 0x3F)))
        out.append(UInt8(0x80 | (v & 0x3F)))
    }
}

private func decodeBase64(_ s: String) -> [UInt8]? {
    var out: [UInt8] = []
    var accumulator: UInt32 = 0
    var bits = 0
    for c in s.utf8 {
        let v: UInt32
        switch c {
        case UInt8(ascii: "A")...UInt8(ascii: "Z"): v = UInt32(c - UInt8(ascii: "A"))
        case UInt8(ascii: "a")...UInt8(ascii: "z"): v = UInt32(c - UInt8(ascii: "a")) + 26
        case UInt8(ascii: "0")...UInt8(ascii: "9"): v = UInt32(c - UInt8(ascii: "0")) + 52
        case UInt8(ascii: "+"): v = 62
        case UInt8(ascii: "/"): v = 63
        case UInt8(ascii: "="): continue
        default:
            if isSpace(c) { continue }
            return nil
        }
        accumulator = (accumulator << 6) | v
        bits += 6
        if bits >= 8 {
            bits -= 8
            out.append(UInt8((accumulator >> UInt32(bits)) & 0xFF))
        }
    }
    return out
}
