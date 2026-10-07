// SPDX-License-Identifier: BSD-2-Clause
//
// ndpkg: NeoDarwin's package manager (docs/architecture/packaging.md §8).
// P2-01 gives it its first commands, the run-time trust grant:
//
//     ndpkg activate-trust PKG.ndpkg
//         Verifies the package's signed manifest against the package roots
//         the kernel trusts (security.codesigning.neodarwin.pkg_roots, from
//         the boot-arg nd_pkg_root), checks that the manifest names the
//         package's trust cache and grant by digest, then hands both to the
//         kernel, which checks the grant itself (ndamfi) and loads the
//         trust cache: the package's binaries then run under enforcement.
//     ndpkg load-trust MODULE GRANT
//         Hands a trust-cache module and its grant to the kernel without
//         checking them here: the kernel's own check is the one that counts.
//
// Installing a package's files is P2-02's store; activate-trust trusts the
// binaries the trust cache lists wherever they are. The loads need root and
// the entitlement com.apple.private.pmap.load-trust-cache =
// neodarwin.trust-cache.load, which the kernel checks.
// Embedded Swift (language policy T1 tool, built as T3) over NDPkgShim.

import NDPkgShim

func fail(_ message: String) -> Never {
    nd_pkg_warn("ndpkg: " + message)
    exit(1)
}

func readFile(_ path: String) -> [UInt8] {
    var len = 0
    guard let p = nd_pkg_read_file(path, &len) else {
        fail("\(path): \(String(cString: strerror(nd_pkg_errno())))")
    }
    defer { nd_pkg_free(p) }
    return Array(UnsafeBufferPointer(start: p, count: len))
}

// A NUL-terminated C string in a buffer.
func string(_ buffer: [CChar]) -> String {
    String(decoding: buffer.prefix { $0 != 0 }.map { UInt8(bitPattern: $0) }, as: UTF8.self)
}

func sha256Hex(_ bytes: [UInt8]) -> String {
    var digest = [UInt8](repeating: 0, count: 32)
    let ok = bytes.withUnsafeBufferPointer { nd_pkg_sha256($0.baseAddress, $0.count, &digest) }
    if !ok { fail("can't load libmd for SHA-256") }
    let digits: [Character] = ["0", "1", "2", "3", "4", "5", "6", "7", "8", "9", "a", "b", "c", "d", "e", "f"]
    var s = ""
    for b in digest {
        s.append(digits[Int(b >> 4)])
        s.append(digits[Int(b & 15)])
    }
    return s
}

// -- the archive: one zstd frame over ustar (packaging.md §3.1) -----------------------

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

// The members' paths and bytes, in archive order. Only what nd_package
// writes: regular files and symbolic links (whose contents aren't needed).
func readArchive(_ tar: [UInt8], _ path: String) -> [(String, [UInt8])] {
    var members: [(String, [UInt8])] = []
    var at = 0
    while at + 512 <= tar.count {
        let header = tar[at..<at + 512]
        if header.allSatisfy({ $0 == 0 }) { return members }
        guard field(header, 257, 6) == "ustar" else { fail("\(path): not a ustar archive") }
        let name = field(header, 0, 100), prefix = field(header, 345, 155)
        let sizeText = field(header, 124, 12).filter { $0 != " " }
        guard let size = Int(sizeText, radix: 8), at + 512 + size <= tar.count else { fail("\(path): truncated archive") }
        let type = header[header.startIndex + 156]
        if type == UInt8(ascii: "0") || type == 0 {
            members.append((prefix.isEmpty ? name : prefix + "/" + name, Array(tar[at + 512..<at + 512 + size])))
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

// -- the kernel ------------------------------------------------------------------------

func load(_ module: [UInt8], _ grant: [UInt8], _ what: String) {
    let r = module.withUnsafeBufferPointer { m in
        grant.withUnsafeBufferPointer { g in nd_pkg_load_trust_cache(m.baseAddress!, m.count, g.baseAddress!, g.count) }
    }
    switch r {
    case 0: print("ndpkg: \(what): trust cache loaded")
    case EEXIST: print("ndpkg: \(what): trust cache already loaded")
    case EAUTH: fail("\(what): the kernel refused the trust cache's grant (EAUTH)")
    case EPERM: fail("\(what): not permitted (root and the load-trust-cache entitlement are required) (EPERM)")
    default: fail("\(what): the kernel refused the trust cache: \(String(cString: strerror(r)))")
    }
}

func activateTrust(_ path: String) {
    let members = readArchive(decompress(readFile(path), path), path)
    func member(_ name: String) -> [UInt8] {
        guard let m = members.first(where: { $0.0 == name }) else { fail("\(path): no \(name)") }
        return m.1
    }
    let manifestBytes = member("manifest.toml"), sig = member("manifest.sig")
    let manifest = String(decoding: manifestBytes, as: UTF8.self)
    guard let name = value(manifest, "name"), let version = value(manifest, "version") else {
        fail("\(path): manifest.toml has no name or version")
    }
    guard members.contains(where: { $0.0 == "trustcache" }) else { fail("\(path): \(name) has no trust cache") }
    let module = member("trustcache"), grant = member("trustcache.grant")

    var roots = [UInt8](repeating: 0, count: 4 * 32)
    let nroots = nd_pkg_kernel_roots(&roots, 4)
    if nroots == 0 { fail("\(path): the kernel trusts no package root (boot-arg nd_pkg_root)") }
    var statement = [CChar](repeating: 0, count: 4096)
    let e = sig.withUnsafeBufferPointer { nd_pkg_verify_bundle($0.baseAddress!, $0.count, roots, nroots, "manifest", &statement, statement.count) }
    if e != 0 { fail("\(path): manifest signature refused: \(String(cString: nd_pkg_error_name(e)))") }
    let signed = string(statement)
    guard value(signed, "manifest-sha256") == sha256Hex(manifestBytes) else { fail("\(path): manifest.sig signs another manifest") }
    guard value(signed, "package") == name, value(signed, "version") == version else {
        fail("\(path): manifest.sig names another package")
    }
    guard value(manifest, "module-sha256", section: "trust-cache") == sha256Hex(module),
          value(manifest, "grant-sha256", section: "trust-cache") == sha256Hex(grant) else {
        fail("\(path): the trust cache or its grant isn't the one the manifest names")
    }
    print("ndpkg: \(name) \(version): manifest verified")
    load(module, grant, "\(name) \(version)")
}

@_cdecl("main")
func ndpkgMain(_ argc: Int32, _ argv: UnsafeMutablePointer<UnsafeMutablePointer<CChar>?>) -> Int32 {
    var args: [String] = []
    for i in 1..<Int(argc) {
        if let p = argv[i] { args.append(String(cString: p)) }
    }
    switch (args.first ?? "", args.count) {
    case ("activate-trust", 2):
        activateTrust(args[1])
    case ("load-trust", 3):
        load(readFile(args[1]), readFile(args[2]), args[1])
    default:
        nd_pkg_warn("usage: ndpkg activate-trust PKG.ndpkg\n       ndpkg load-trust MODULE GRANT")
        exit(2)
    }
    return 0
}
