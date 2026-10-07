// SPDX-License-Identifier: BSD-2-Clause
//
// Reading and verifying a .ndpkg (packaging.md §3.1, §4.2): the archive,
// the canonical manifest, the signature against the package roots the
// kernel trusts, and (P2-02) every payload file against the manifest.

import NDPkgShim

// -- the archive: one zstd frame over ustar (packaging.md §3.1) -----------------------

enum Member {
    case file([UInt8])
    case link(String)
}

func decompress(_ data: [UInt8], _ path: String) -> [UInt8] {
    var error = [CChar](repeating: 0, count: 128)
    var len = 0
    let p = data.withUnsafeBufferPointer { nd_pkg_zstd_decompress($0.baseAddress!, $0.count, &len, &error, error.count) }
    guard let p else { fail("\(path): \(string(error))") }
    defer { nd_pkg_free(p) }
    return Array(UnsafeBufferPointer(start: p, count: len))
}

func field(_ block: ArraySlice<UInt8>, _ offset: Int, _ length: Int) -> String {
    let start = block.startIndex + offset
    var bytes: [UInt8] = []
    for b in block[start..<start + length] {
        if b == 0 { break }
        bytes.append(b)
    }
    return String(decoding: bytes, as: UTF8.self)
}

// The members' paths and contents, in archive order. Only what nd_package
// writes: regular files and symbolic links.
func readArchive(_ tar: [UInt8], _ path: String) -> [(String, Member)] {
    var members: [(String, Member)] = []
    var at = 0
    while at + 512 <= tar.count {
        let header = tar[at..<at + 512]
        if header.allSatisfy({ $0 == 0 }) { return members }
        guard field(header, 257, 6) == "ustar" else { fail("\(path): not a ustar archive") }
        let name = field(header, 0, 100), prefix = field(header, 345, 155)
        let full = prefix.isEmpty ? name : prefix + "/" + name
        let sizeText = field(header, 124, 12).filter { $0 != " " }
        guard let size = Int(sizeText, radix: 8), at + 512 + size <= tar.count else { fail("\(path): truncated archive") }
        let type = header[header.startIndex + 156]
        if type == UInt8(ascii: "0") || type == 0 {
            members.append((full, .file(Array(tar[at + 512..<at + 512 + size]))))
        } else if type == UInt8(ascii: "2") {
            members.append((full, .link(field(header, 157, 100))))
        } else {
            fail("\(path): \(full): not a regular file or symbolic link")
        }
        at += 512 + (size + 511) / 512 * 512
    }
    fail("\(path): truncated archive")
}

// -- canonical TOML: `key = value` lines, with sections ---------------------------------

// The value of `key` in the lines of `text` under `section` ("" for the top
// level), without the quotes of a string.
func value(_ text: String, _ key: String, section: String = "") -> String? {
    var current = ""
    for line in text.split(separator: "\n") {
        if line.hasPrefix("[") {
            current = String(line.dropFirst().dropLast())
            continue
        }
        guard current == section, line.hasPrefix(key + " = ") else { continue }
        var v = line.dropFirst(key.count + 3)
        if v.hasPrefix("\""), v.hasSuffix("\""), v.count >= 2 { v = v.dropFirst().dropLast() }
        return String(v)
    }
    return nil
}

// A canonical array of strings, `["a", "b"]`.
func array(_ v: String?) -> [String] {
    guard let v, v.hasPrefix("["), v.hasSuffix("]") else { return [] }
    var out: [String] = []
    var current = ""
    var inString = false
    for c in v.dropFirst().dropLast() {
        if c == "\"" {
            if inString { out.append(current); current = "" }
            inString.toggle()
        } else if inString {
            current.append(c)
        }
    }
    return out
}

// One line of the [files] table:
//     "path" = { mode = "0644", sha256 = "…", size = N }
//     "path" = { link = "target", mode = "0755" }
struct FileEntry {
    var path: String
    var mode: Int32
    var sha256: String?
    var size: Int?
    var link: String?
}

func parseFileLine(_ line: String) -> FileEntry? {
    let chars = Array(line)
    guard chars.first == "\"" else { return nil }
    var i = 1
    var path = ""
    while i < chars.count, chars[i] != "\"" { path.append(chars[i]); i += 1 }
    i += 1
    let rest = String(chars[min(i, chars.count)...])
    guard rest.hasPrefix(" = { "), rest.hasSuffix(" }") else { return nil }
    let inner = Array(rest.dropFirst(5).dropLast(2))
    var e = FileEntry(path: path, mode: -1, sha256: nil, size: nil, link: nil)
    var j = 0
    while j < inner.count {
        var key = ""
        while j < inner.count, inner[j] != " " { key.append(inner[j]); j += 1 }
        guard j + 3 <= inner.count, String(inner[j..<j + 3]) == " = " else { return nil }
        j += 3
        var val = ""
        if j < inner.count, inner[j] == "\"" {
            j += 1
            while j < inner.count, inner[j] != "\"" { val.append(inner[j]); j += 1 }
            j += 1
        } else {
            while j < inner.count, inner[j] != "," { val.append(inner[j]); j += 1 }
        }
        switch key {
        case "mode": e.mode = Int32(val, radix: 8) ?? -1
        case "sha256": e.sha256 = val
        case "size": e.size = Int(val)
        case "link": e.link = val
        default: return nil
        }
        if j < inner.count {
            guard j + 2 <= inner.count, inner[j] == ",", inner[j + 1] == " " else { return nil }
            j += 2
        }
    }
    guard e.mode >= 0, (e.link != nil) != (e.sha256 != nil && e.size != nil) else { return nil }
    return e
}

struct Manifest {
    var text: String
    var name: String
    var version: String
    var arch: String
    var kind: String
    var license: String
    var payload: String
    var provides: [String]
    var requires: [String]
    var conflicts: [String]
    var files: [FileEntry]
    var tcModule: String?
    var tcGrant: String?

    // The store entry's name (§5): content first, then a readable name.
    var entry: String { "\(payload)-\(name)-\(version)" }
}

func parseManifest(_ text: String, _ what: String) -> Manifest {
    guard let name = value(text, "name"), let version = value(text, "version"), let arch = value(text, "arch"),
          let kind = value(text, "kind"), let payload = value(text, "payload-sha256"), value(text, "schema") == "1" else {
        fail("\(what): manifest.toml lacks name, version, arch, kind, payload-sha256 or schema = 1")
    }
    // The [files] table, whose SHA-256 is payload-sha256.
    var table = "", section = "", sawFiles = false
    var files: [FileEntry] = []
    for line in lines(text) {
        if line.hasPrefix("[") {
            section = line
            if line == "[files]" { sawFiles = true; table += line + "\n" }
            continue
        }
        guard section == "[files]" else { continue }
        guard let e = parseFileLine(line) else { fail("\(what): bad [files] line: \(line)") }
        files.append(e)
        table += line + "\n"
    }
    guard sawFiles else { fail("\(what): manifest.toml has no [files]") }
    guard sha256Hex(Array(table.utf8)) == payload else { fail("\(what): payload-sha256 isn't its [files] table's digest") }
    return Manifest(text: text, name: name, version: version, arch: arch, kind: kind, license: value(text, "license") ?? "",
                    payload: payload, provides: array(value(text, "provides")), requires: array(value(text, "requires")),
                    conflicts: array(value(text, "conflicts")), files: files,
                    tcModule: value(text, "module-sha256", section: "trust-cache"),
                    tcGrant: value(text, "grant-sha256", section: "trust-cache"))
}

// -- verification --------------------------------------------------------------------

// The package roots the kernel trusts (boot-arg nd_pkg_root).
func kernelRoots(_ what: String) -> ([UInt8], Int) {
    var roots = [UInt8](repeating: 0, count: 4 * 32)
    let n = nd_pkg_kernel_roots(&roots, 4)
    if n == 0 { fail("\(what): the kernel trusts no package root (boot-arg nd_pkg_root)") }
    return (roots, n)
}

// Checks manifest.sig against the kernel's roots and that it signs this
// manifest, for the package and version the manifest names.
func verifyManifest(_ manifestBytes: [UInt8], _ sig: [UInt8], _ what: String) -> Manifest {
    let (roots, nroots) = kernelRoots(what)
    var statement = [CChar](repeating: 0, count: 4096)
    let e = sig.withUnsafeBufferPointer { nd_pkg_verify_bundle($0.baseAddress!, $0.count, roots, nroots, "manifest", &statement, statement.count) }
    if e != 0 { fail("\(what): manifest signature refused: \(String(cString: nd_pkg_error_name(e)))") }
    let signed = string(statement)
    guard value(signed, "manifest-sha256") == sha256Hex(manifestBytes) else { fail("\(what): manifest.sig signs another manifest") }
    let m = parseManifest(text(manifestBytes), what)
    guard value(signed, "package") == m.name, value(signed, "version") == m.version else {
        fail("\(what): manifest.sig names another package")
    }
    return m
}

func verifyTrustCache(_ m: Manifest, module: [UInt8]?, grant: [UInt8]?, _ what: String) {
    switch (m.tcModule, m.tcGrant, module, grant) {
    case (nil, nil, nil, nil):
        return
    case (let mh?, let gh?, let module?, let grant?):
        guard mh == sha256Hex(module), gh == sha256Hex(grant) else {
            fail("\(what): the trust cache or its grant isn't the one the manifest names")
        }
    default:
        fail("\(what): the trust cache and grant don't match the manifest's [trust-cache]")
    }
}

// A path a userland package may install: relative, under the live prefix
// (usr/local), without empty, `.` or `..` components.
func payloadPathOK(_ p: String) -> Bool {
    guard p.hasPrefix(prefixRelative + "/") else { return false }
    for c in p.split(separator: "/", omittingEmptySubsequences: false) {
        if c.isEmpty || c == "." || c == ".." { return false }
    }
    return true
}

struct Package {
    var path: String
    var manifest: Manifest
    var manifestBytes: [UInt8]
    var sig: [UInt8]
    var module: [UInt8]?
    var grant: [UInt8]?
    var payload: [String: Member]
    var manifestSHA256: String
}

// Opens and fully verifies a package: its signature (kernel roots), its
// trust cache, and every payload member against the manifest's [files]:
// the digest and size of each file, the target of each link, nothing
// missing and nothing extra.
func openPackage(_ path: String) -> Package {
    let members = readArchive(decompress(readFile(path), path), path)
    var named: [String: Member] = [:]
    var payload: [String: Member] = [:]
    for (name, member) in members {
        if name.hasPrefix("files/") {
            let p = String(name.dropFirst(6))
            if payload[p] != nil { fail("\(path): \(name) appears twice") }
            payload[p] = member
        } else if name == "manifest.toml" || name == "manifest.sig" || name == "trustcache" || name == "trustcache.grant" {
            if named[name] != nil { fail("\(path): \(name) appears twice") }
            named[name] = member
        } else {
            fail("\(path): unexpected member \(name)")
        }
    }
    func bytes(_ n: String) -> [UInt8]? {
        guard let m = named[n] else { return nil }
        guard case .file(let b) = m else { fail("\(path): \(n) is a link") }
        return b
    }
    guard let manifestBytes = bytes("manifest.toml"), let sig = bytes("manifest.sig") else {
        fail("\(path): no manifest.toml or manifest.sig")
    }
    let m = verifyManifest(manifestBytes, sig, path)
    let module = bytes("trustcache"), grant = bytes("trustcache.grant")
    verifyTrustCache(m, module: module, grant: grant, path)
    var listed = 0
    for e in m.files {
        guard payloadPathOK(e.path) else { fail("\(path): \(e.path): not a path under /\(prefixRelative)") }
        guard let member = payload[e.path] else { fail("\(path): files/\(e.path) is missing") }
        switch member {
        case .file(let data):
            guard e.link == nil, data.count == e.size, sha256Hex(data) == e.sha256 else {
                fail("\(path): files/\(e.path) does not match the manifest")
            }
        case .link(let target):
            guard e.link == target else { fail("\(path): files/\(e.path): link does not match the manifest") }
        }
        listed += 1
    }
    if listed != payload.count { fail("\(path): the archive has files the manifest doesn't list") }
    return Package(path: path, manifest: m, manifestBytes: manifestBytes, sig: sig, module: module, grant: grant,
                   payload: payload, manifestSHA256: sha256Hex(manifestBytes))
}
