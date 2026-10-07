// SPDX-License-Identifier: BSD-2-Clause
//
// A small JSON reader for what ndpkg writes itself: plans (`ndpkg apply`)
// and repository indexes (index.json).

indirect enum JSON {
    case string(String)
    case number(Int)
    case bool(Bool)
    case null
    case array([JSON])
    case object([(String, JSON)])

    subscript(_ key: String) -> JSON? {
        guard case .object(let members) = self else { return nil }
        for (k, v) in members where k == key { return v }
        return nil
    }

    var string: String? {
        if case .string(let s) = self { return s }
        return nil
    }

    var int: Int? {
        if case .number(let n) = self { return n }
        return nil
    }

    var array: [JSON] {
        if case .array(let a) = self { return a }
        return []
    }

    var strings: [String] { array.compactMap { $0.string } }
}

struct JSONReader {
    var bytes: [UInt8]
    var at = 0

    init(_ text: [UInt8]) { bytes = text }

    mutating func skip() {
        while at < bytes.count, bytes[at] == 0x20 || bytes[at] == 0x0a || bytes[at] == 0x0d || bytes[at] == 0x09 { at += 1 }
    }

    mutating func expect(_ c: UInt8) -> Bool {
        skip()
        guard at < bytes.count, bytes[at] == c else { return false }
        at += 1
        return true
    }

    mutating func literal(_ word: String) -> Bool {
        let w = Array(word.utf8)
        guard at + w.count <= bytes.count, Array(bytes[at..<at + w.count]) == w else { return false }
        at += w.count
        return true
    }

    mutating func stringValue() -> String? {
        guard expect(0x22) else { return nil }
        var out: [UInt8] = []
        while at < bytes.count {
            let c = bytes[at]
            at += 1
            if c == 0x22 { return String(decoding: out, as: UTF8.self) }
            if c != 0x5c { out.append(c); continue }
            guard at < bytes.count else { return nil }
            let e = bytes[at]
            at += 1
            switch e {
            case 0x22, 0x5c, 0x2f: out.append(e)
            case UInt8(ascii: "n"): out.append(0x0a)
            case UInt8(ascii: "t"): out.append(0x09)
            case UInt8(ascii: "r"): out.append(0x0d)
            case UInt8(ascii: "u"):
                // Only what ndpkg writes: \u00XX control characters.
                guard at + 4 <= bytes.count, let v = UInt8(String(decoding: bytes[at + 2..<at + 4], as: UTF8.self), radix: 16) else { return nil }
                out.append(v)
                at += 4
            default: return nil
            }
        }
        return nil
    }

    mutating func parse() -> JSON? {
        skip()
        guard at < bytes.count else { return nil }
        switch bytes[at] {
        case 0x22:
            return stringValue().map { .string($0) }
        case UInt8(ascii: "{"):
            at += 1
            var members: [(String, JSON)] = []
            if expect(UInt8(ascii: "}")) { return .object(members) }
            repeat {
                guard let k = stringValue(), expect(UInt8(ascii: ":")), let v = parse() else { return nil }
                members.append((k, v))
            } while expect(UInt8(ascii: ","))
            return expect(UInt8(ascii: "}")) ? .object(members) : nil
        case UInt8(ascii: "["):
            at += 1
            var items: [JSON] = []
            if expect(UInt8(ascii: "]")) { return .array(items) }
            repeat {
                guard let v = parse() else { return nil }
                items.append(v)
            } while expect(UInt8(ascii: ","))
            return expect(UInt8(ascii: "]")) ? .array(items) : nil
        case UInt8(ascii: "t"): return literal("true") ? .bool(true) : nil
        case UInt8(ascii: "f"): return literal("false") ? .bool(false) : nil
        case UInt8(ascii: "n"): return literal("null") ? .null : nil
        default:
            var digits = ""
            while at < bytes.count, bytes[at] == UInt8(ascii: "-") || (bytes[at] >= 0x30 && bytes[at] <= 0x39) {
                digits.append(Character(Unicode.Scalar(bytes[at])))
                at += 1
            }
            return Int(digits).map { .number($0) }
        }
    }
}

func parseJSON(_ bytes: [UInt8], _ what: String) -> JSON {
    var r = JSONReader(bytes)
    guard let v = r.parse() else { fail("\(what): not JSON ndpkg can read") }
    r.skip()
    guard r.at == bytes.count else { fail("\(what): trailing data after the JSON") }
    return v
}
