// SPDX-License-Identifier: BSD-2-Clause
//
// Packages (docs/architecture/packaging.md §3): the manifest's canonical
// encoding, building a signed .ndpkg and verifying one offline.

import NDSignC

// MARK: - manifest

public struct FileEntry: Sendable, Equatable {
    public var mode: UInt32
    public var sha256: String?   // a regular file
    public var size: Int?
    public var link: String?     // a symbolic link
}

public struct TrustCacheEntry: Sendable, Equatable {
    public var entries: UInt64
    public var moduleSHA256: String
    public var grantSHA256: String
}

public struct Manifest: Sendable, Equatable {
    public var name = ""
    public var version = ""
    public var arch = "aarch64"
    public var license = ""
    public var kind = "app"
    public var provides: [String] = []
    public var requires: [String] = []
    public var conflicts: [String] = []
    public var files: [String: FileEntry] = [:]
    public var trustCache: TrustCacheEntry? = nil

    public init() {}

    static let kinds: Set<String> = ["app", "lib", "service", "kext", "kernel-collection", "system-set", "port"]

    func sortedPaths() -> [String] {
        files.keys.sorted { Array($0.utf8).lexicographicallyPrecedes(Array($1.utf8)) }
    }

    /// The [files] table, whose SHA-256 is the payload digest.
    public func filesTable() -> String {
        var t = "[files]\n"
        for p in sortedPaths() {
            let f = files[p]!
            let mode = "mode = \"0" + String(f.mode, radix: 8) + "\""
            if let link = f.link {
                t += "\"\(p)\" = { link = \"\(link)\", \(mode) }\n"
            } else {
                t += "\"\(p)\" = { \(mode), sha256 = \"\(f.sha256!)\", size = \(f.size!) }\n"
            }
        }
        return t
    }

    public var payloadSHA256: String { hex(sha256(Array(filesTable().utf8))) }

    static func array(_ a: [String]) -> String {
        "[" + a.sorted().map { "\"\($0)\"" }.joined(separator: ", ") + "]"
    }

    public func render() -> String {
        var s = ""
        s += "arch = \"\(arch)\"\n"
        s += "conflicts = \(Self.array(conflicts))\n"
        s += "kind = \"\(kind)\"\n"
        s += "license = \"\(license)\"\n"
        s += "name = \"\(name)\"\n"
        s += "payload-sha256 = \"\(payloadSHA256)\"\n"
        s += "provides = \(Self.array(provides))\n"
        s += "requires = \(Self.array(requires))\n"
        s += "schema = 1\n"
        s += "version = \"\(version)\"\n"
        s += "\n" + filesTable()
        if let tc = trustCache {
            s += "\n[trust-cache]\nentries = \(tc.entries)\ngrant-sha256 = \"\(tc.grantSHA256)\"\nmodule-sha256 = \"\(tc.moduleSHA256)\"\n"
        }
        return s
    }

    func check() throws {
        let plain: (String) -> Bool = { s in s.utf8.allSatisfy { $0 >= 0x20 && $0 <= 0x7e && $0 != 0x22 && $0 != 0x5c } }
        let token: (String) -> Bool = { s in !s.isEmpty && s.utf8.allSatisfy { ($0 >= 0x61 && $0 <= 0x7a) || ($0 >= 0x30 && $0 <= 0x39) || $0 == 0x2d || $0 == 0x2e || $0 == 0x2b || $0 == 0x5f } }
        guard token(name), token(version), token(arch) else { throw NDSignError("name, version and arch are [a-z0-9.+_-]+") }
        guard Self.kinds.contains(kind) else { throw NDSignError("kind \(kind) is not one of \(Self.kinds.sorted())") }
        guard ([license] + provides + requires + conflicts).allSatisfy(plain) else { throw NDSignError("values are printable ASCII without quotes or backslashes") }
        for (p, f) in files {
            guard safe(p), plain(p), f.link.map({ plain($0) && !$0.isEmpty }) ?? true else { throw NDSignError("bad path \(p)") }
        }
    }

    /// Parses a manifest and requires it to be canonical: rendering it again
    /// must give the same bytes.
    public static func parse(_ text: String) throws -> Manifest {
        var m = Manifest()
        var table = ""
        var payload: String? = nil
        var tc: [String: String] = [:]
        for raw in text.split(separator: "\n", omittingEmptySubsequences: true) {
            let line = String(raw)
            if line.hasPrefix("[") { table = line; continue }
            switch table {
            case "":
                guard let eq = line.firstRange(of: " = ") else { throw NDSignError("manifest: bad line \(line)") }
                let key = String(line[..<eq.lowerBound]), value = String(line[eq.upperBound...])
                switch key {
                case "arch": m.arch = try str(value)
                case "kind": m.kind = try str(value)
                case "license": m.license = try str(value)
                case "name": m.name = try str(value)
                case "version": m.version = try str(value)
                case "payload-sha256": payload = try str(value)
                case "provides": m.provides = try arr(value)
                case "requires": m.requires = try arr(value)
                case "conflicts": m.conflicts = try arr(value)
                case "schema": guard value == "1" else { throw NDSignError("manifest: schema \(value) is not 1") }
                default: throw NDSignError("manifest: unknown key \(key)")
                }
            case "[files]":
                guard line.hasPrefix("\""), let close = line.dropFirst().firstIndex(of: "\""),
                      line[close...].hasPrefix("\" = { "), line.hasSuffix(" }") else {
                    throw NDSignError("manifest: bad file line \(line)")
                }
                let path = String(line[line.index(after: line.startIndex)..<close])
                let inner = line[line.index(close, offsetBy: 6)..<line.index(line.endIndex, offsetBy: -2)]
                var e = FileEntry(mode: 0, sha256: nil, size: nil, link: nil)
                for part in inner.split(separator: ",").map({ $0.trimmingPrefix(" ") }) {
                    guard let eq = part.firstRange(of: " = ") else { throw NDSignError("manifest: bad file line \(line)") }
                    let k = part[..<eq.lowerBound], v = String(part[eq.upperBound...])
                    switch k {
                    case "mode": e.mode = UInt32(try str(v), radix: 8) ?? 0
                    case "sha256": e.sha256 = try str(v)
                    case "size": e.size = Int(v)
                    case "link": e.link = try str(v)
                    default: throw NDSignError("manifest: unknown file key \(k)")
                    }
                }
                guard m.files.updateValue(e, forKey: path) == nil else { throw NDSignError("manifest: duplicate \(path)") }
            case "[trust-cache]":
                guard let eq = line.firstRange(of: " = ") else { throw NDSignError("manifest: bad line \(line)") }
                tc[String(line[..<eq.lowerBound])] = String(line[eq.upperBound...])
            default:
                throw NDSignError("manifest: unknown table \(table)")
            }
        }
        if !tc.isEmpty {
            guard let n = tc["entries"].flatMap({ UInt64($0) }), let g = tc["grant-sha256"], let mod = tc["module-sha256"] else {
                throw NDSignError("manifest: incomplete [trust-cache]")
            }
            m.trustCache = TrustCacheEntry(entries: n, moduleSHA256: try str(mod), grantSHA256: try str(g))
        }
        try m.check()
        guard m.render() == text else { throw NDSignError("manifest is not canonical") }
        guard payload == m.payloadSHA256 else { throw NDSignError("manifest: payload-sha256 does not match its [files]") }
        return m
    }

    static func str(_ v: String) throws -> String {
        guard v.count >= 2, v.hasPrefix("\""), v.hasSuffix("\"") else { throw NDSignError("manifest: not a string: \(v)") }
        return String(v.dropFirst().dropLast())
    }

    static func arr(_ v: String) throws -> [String] {
        guard v.hasPrefix("["), v.hasSuffix("]") else { throw NDSignError("manifest: not an array: \(v)") }
        let inner = v.dropFirst().dropLast()
        return inner.isEmpty ? [] : try inner.split(separator: ",").map { try str(String($0.trimmingPrefix(" "))) }
    }
}

// MARK: - building

public struct SignedBy: Sendable {
    public let chain: [Document]
    public let key: SigningKey
    public let issued: UInt64

    public init(chain: [Document], key: SigningKey, issued: UInt64) {
        self.chain = chain; self.key = key; self.issued = issued
    }
}

/// Run-time trust caches are loaded as XNU's kTCTypeLTRS; the grant names it.
public let loadableTCType = "ltrs"

/// The entry count a version 1 module declares (osfmk/kern/trustcache.h:
/// version, UUID, count).
func moduleEntries(_ module: [UInt8]) throws -> UInt64 {
    guard module.count >= 24, module[0] == 1, module[1] == 0, module[2] == 0, module[3] == 0 else {
        throw NDSignError("the trust cache is not a version 1 module")
    }
    return UInt64(module[20]) | UInt64(module[21]) << 8 | UInt64(module[22]) << 16 | UInt64(module[23]) << 24
}

public struct BuiltPackage: Sendable {
    public let archive: [UInt8]
    public let manifest: String
    public let grant: String?
}

/// `payload` holds paths relative to the package root (archived under files/).
public func buildPackage(_ meta: Manifest, payload: [Member], trustCache module: [UInt8]?, signer: SignedBy) throws -> BuiltPackage {
    var m = meta
    m.files = [:]
    for p in payload {
        switch p.kind {
        case .file(let data): m.files[p.path] = FileEntry(mode: p.mode, sha256: hex(sha256(data)), size: data.count, link: nil)
        case .link(let target): m.files[p.path] = FileEntry(mode: p.mode, sha256: nil, size: nil, link: target)
        }
    }
    try m.check()
    var grant: String? = nil
    if let module {
        let g = try bundle(chain: signer.chain, statement: [
            "kind": .string("trust-cache"), "package": .string(m.name), "version": .string(m.version),
            "module-sha256": .string(hex(sha256(module))), "tc-type": .string(loadableTCType), "issued": .int(signer.issued),
        ], key: signer.key)
        grant = g
        m.trustCache = TrustCacheEntry(entries: try moduleEntries(module), moduleSHA256: hex(sha256(module)),
                                       grantSHA256: hex(sha256(Array(g.utf8))))
    }
    let manifest = m.render()
    let sig = try bundle(chain: signer.chain, statement: [
        "kind": .string("manifest"), "package": .string(m.name), "version": .string(m.version),
        "manifest-sha256": .string(hex(sha256(Array(manifest.utf8)))), "issued": .int(signer.issued),
    ], key: signer.key)
    var members = [
        Member(path: "manifest.toml", mode: 0o644, kind: .file(Array(manifest.utf8))),
        Member(path: "manifest.sig", mode: 0o644, kind: .file(Array(sig.utf8))),
    ]
    if let module, let grant {
        members.append(Member(path: "trustcache", mode: 0o644, kind: .file(module)))
        members.append(Member(path: "trustcache.grant", mode: 0o644, kind: .file(Array(grant.utf8))))
    }
    let byPath = Dictionary(uniqueKeysWithValues: payload.map { ($0.path, $0) })
    for p in m.sortedPaths() {
        var member = byPath[p]!
        member.path = "files/" + p
        members.append(member)
    }
    return BuiltPackage(archive: try Zstd.compress(try Tar.write(members)), manifest: manifest, grant: grant)
}

// MARK: - verifying

public struct VerifiedPackage: Sendable {
    public let manifest: Manifest
    public let manifestText: String
    public let signature: VerifiedBundle
    public let trustCache: [UInt8]?
    public let grant: [UInt8]?
    public let payload: [Member]   // paths relative to the package root
}

func statementMatches(_ s: Document, _ m: Manifest) throws {
    guard s["package"] == m.name, s["version"] == m.version else {
        throw NDSignError("the signature is for \(s["package"] ?? "?") \(s["version"] ?? "?"), not \(m.name) \(m.version)")
    }
}

/// Everything offline: the manifest signature and its chain to a root, the
/// manifest's form, every file against it, and the trust cache's grant.
public func verifyPackage(_ archive: [UInt8], roots: [[UInt8]], now: UInt64, keyring: [Document] = []) throws -> VerifiedPackage {
    let members = try Tar.read(try Zstd.decompress(archive))
    func file(_ i: Int, _ path: String) throws -> [UInt8] {
        guard i < members.count, members[i].path == path, case .file(let d) = members[i].kind else {
            throw NDSignError("member \(i) is not \(path)")
        }
        return d
    }
    let manifestBytes = try file(0, "manifest.toml"), sigBytes = try file(1, "manifest.sig")
    let signature = try verifyBundle(sigBytes, roots: roots, now: now, kind: "manifest", keyring: keyring)
    guard signature.statement["manifest-sha256"] == hex(sha256(manifestBytes)) else {
        throw NDSignError("manifest.toml does not match its signature")
    }
    let text = String(decoding: manifestBytes, as: UTF8.self)
    let m = try Manifest.parse(text)
    try statementMatches(signature.statement, m)

    var next = 2
    var module: [UInt8]? = nil, grant: [UInt8]? = nil
    if let tc = m.trustCache {
        module = try file(2, "trustcache"); grant = try file(3, "trustcache.grant")
        next = 4
        guard hex(sha256(module!)) == tc.moduleSHA256, hex(sha256(grant!)) == tc.grantSHA256,
              try moduleEntries(module!) == tc.entries else {
            throw NDSignError("the trust cache or its grant does not match the manifest")
        }
        try verifyGrant(grant!, module: module!, tcType: loadableTCType, roots: roots, now: now)
        let g = try verifyBundle(grant!, roots: roots, now: now, kind: "trust-cache", keyring: keyring)
        try statementMatches(g.statement, m)
    }

    var payload: [Member] = []
    let paths = m.sortedPaths()
    guard members.count - next == paths.count else { throw NDSignError("the archive holds files the manifest doesn't list, or lacks some") }
    for (i, p) in paths.enumerated() {
        var member = members[next + i]
        guard member.path == "files/" + p else { throw NDSignError("expected files/\(p), found \(member.path)") }
        let e = m.files[p]!
        guard member.mode == e.mode else { throw NDSignError("\(p): mode differs from the manifest") }
        switch member.kind {
        case .file(let d):
            guard e.link == nil, d.count == e.size, hex(sha256(d)) == e.sha256 else { throw NDSignError("\(p): does not match the manifest") }
        case .link(let t):
            guard e.link == t else { throw NDSignError("\(p): link does not match the manifest") }
        }
        member.path = p
        payload.append(member)
    }
    return VerifiedPackage(manifest: m, manifestText: text, signature: signature, trustCache: module, grant: grant, payload: payload)
}
