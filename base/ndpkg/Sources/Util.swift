// SPDX-License-Identifier: BSD-2-Clause
//
// ndpkg's helpers: errors, files, digests and text (P2-01, P2-02).

import NDPkgShim

func fail(_ message: String) -> Never {
    nd_pkg_warn("ndpkg: " + message)
    exit(1)
}

func warn(_ message: String) {
    nd_pkg_warn("ndpkg: " + message)
}

func errnoText(_ e: Int32) -> String {
    String(cString: strerror(e))
}

// Fails with `what` if a shim call returned an errno.
func check(_ e: Int32, _ what: String) {
    if e != 0 { fail("\(what): \(errnoText(e))") }
}

func readFile(_ path: String) -> [UInt8] {
    var len = 0
    guard let p = nd_pkg_read_file(path, &len) else {
        fail("\(path): \(errnoText(nd_pkg_errno()))")
    }
    defer { nd_pkg_free(p) }
    return Array(UnsafeBufferPointer(start: p, count: len))
}

// nil if the file can't be read.
func readFileIfPresent(_ path: String) -> [UInt8]? {
    var len = 0
    guard let p = nd_pkg_read_file(path, &len) else { return nil }
    defer { nd_pkg_free(p) }
    return Array(UnsafeBufferPointer(start: p, count: len))
}

func text(_ bytes: [UInt8]) -> String {
    String(decoding: bytes, as: UTF8.self)
}

// A NUL-terminated C string in a buffer.
func string(_ buffer: [CChar]) -> String {
    String(decoding: buffer.prefix { $0 != 0 }.map { UInt8(bitPattern: $0) }, as: UTF8.self)
}

// A malloc'd C string from the shim, freed.
func take(_ p: UnsafeMutablePointer<CChar>?) -> String? {
    guard let p else { return nil }
    defer { nd_pkg_free(p) }
    return String(cString: p)
}

let hexDigits: [Character] = ["0", "1", "2", "3", "4", "5", "6", "7", "8", "9", "a", "b", "c", "d", "e", "f"]

func sha256Hex(_ bytes: [UInt8]) -> String {
    var digest = [UInt8](repeating: 0, count: 32)
    let ok = bytes.withUnsafeBufferPointer { nd_pkg_sha256($0.baseAddress, $0.count, &digest) }
    if !ok { fail("can't load libmd for SHA-256") }
    var s = ""
    for b in digest {
        s.append(hexDigits[Int(b >> 4)])
        s.append(hexDigits[Int(b & 15)])
    }
    return s
}

func lines(_ s: String) -> [String] {
    s.split(separator: "\n").map { String($0) }
}

func kind(_ path: String) -> Int32 { nd_pkg_kind(path) }
func exists(_ path: String) -> Bool { nd_pkg_kind(path) != 0 }

func readlink(_ path: String) -> String? { take(nd_pkg_readlink(path)) }

func listDir(_ path: String) -> [String] {
    guard let names = take(nd_pkg_list_dir(path)) else { return [] }
    return lines(names).sorted()
}

func dirname(_ path: String) -> String {
    guard let slash = path.lastIndex(of: "/") else { return "." }
    return slash == path.startIndex ? "/" : String(path[..<slash])
}

func basename(_ path: String) -> String {
    guard let slash = path.lastIndex(of: "/") else { return path }
    return String(path[path.index(after: slash)...])
}

// Writes a file atomically: a temporary file beside it, then rename.
func writeAtomically(_ path: String, _ s: String, mode: Int32 = 0o644) {
    let tmp = dirname(path) + "/." + basename(path) + ".tmp\(nd_pkg_pid())"
    _ = nd_pkg_unlink(tmp)
    let bytes = Array(s.utf8)
    check(bytes.withUnsafeBufferPointer { nd_pkg_write_file(tmp, $0.baseAddress, $0.count, mode) }, tmp)
    check(nd_pkg_rename(tmp, path), path)
}

func writeNew(_ path: String, _ bytes: [UInt8], mode: Int32) {
    check(bytes.withUnsafeBufferPointer { nd_pkg_write_file(path, $0.baseAddress, $0.count, mode) }, path)
}

// Replaces a symbolic link atomically (rename over the old one).
func setLink(_ path: String, to target: String) {
    let tmp = dirname(path) + "/." + basename(path) + ".tmp\(nd_pkg_pid())"
    _ = nd_pkg_unlink(tmp)
    check(nd_pkg_symlink(target, tmp), tmp)
    check(nd_pkg_rename(tmp, path), path)
}

func mkdirs(_ path: String, mode: Int32 = 0o755) {
    check(nd_pkg_mkdirs(path, mode), path)
}

// -- JSON output ---------------------------------------------------------------------

func json(_ s: String) -> String {
    var out = "\""
    for u in s.unicodeScalars {
        switch u {
        case "\"": out += "\\\""
        case "\\": out += "\\\\"
        case "\n": out += "\\n"
        case "\t": out += "\\t"
        default:
            if u.value < 0x20 {
                out += "\\u00" + String(hexDigits[Int(u.value >> 4)]) + String(hexDigits[Int(u.value & 15)])
            } else {
                out.unicodeScalars.append(u)
            }
        }
    }
    return out + "\""
}

func json(_ a: [String]) -> String {
    "[" + a.map { json($0) }.joined(separator: ", ") + "]"
}

// An object's members, in the order given, on one line.
func jsonObject(_ members: [(String, String)]) -> String {
    "{" + members.map { json($0.0) + ": " + $0.1 }.joined(separator: ", ") + "}"
}
