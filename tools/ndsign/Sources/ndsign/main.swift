// SPDX-License-Identifier: BSD-2-Clause
//
// ndsign: NeoDarwin's package signing tool (P2-01, docs/architecture/packaging.md §3–§4).
//
// Usage:
//     ndsign keygen OUT                       OUT.key (the seed, mode 0600) and OUT.pub
//     ndsign certify --issuer KEY --kind channel|release --name NAME --seq N
//                    [--previous CERT] --expires UNIX --scope SCOPE
//                    --usage manifest|trust-cache [--usage ...] --key PUB -o OUT
//         A certificate for PUB, issued by KEY (a root for a channel
//         certificate, a channel key for a release one). Seq 1 has no
//         previous; seq N+1 names the seq N certificate it replaces.
//     ndsign sign-tc --chain FILE... --key KEY --package NAME --version V
//                    [--issued UNIX] MODULE -o GRANT
//         A grant: the release key's statement that MODULE may be loaded
//         at run time as a trust cache (what ndamfi verifies).
//     ndsign verify-tc --root PUB... [--now UNIX] MODULE GRANT
//     ndsign pack --name N --version V [--arch A] [--license L] [--kind K]
//                 [--provides X]... [--requires X]... [--conflicts X]...
//                 [--trust-cache MODULE] --chain FILE... --key KEY
//                 [--issued UNIX] [--grant-out GRANT] -o OUT.ndpkg ROOT
//         Every regular file and symbolic link under ROOT, deterministically
//         (modes normalised to 0755 or 0644).
//     ndsign verify --root PUB... [--now UNIX] [--keyring DIR] PKG
//     ndsign unpack --root PUB... [--now UNIX] PKG DIR
//         Verifies, then writes the payload under DIR/files and the trust
//         cache and grant as DIR/trustcache and DIR/trustcache.grant.
//
// --now defaults to the current time; --issued to 0 (reproducible builds).
// A --keyring directory holds known certificates (*.cert): a chain whose
// certificate one of them supersedes is refused.

import Foundation
import NDPkg

func fail(_ message: String, code: Int32 = 1) -> Never {
    FileHandle.standardError.write(Data("ndsign: \(message)\n".utf8))
    exit(code)
}

func read(_ path: String) -> [UInt8] {
    guard let data = FileManager.default.contents(atPath: path) else { fail("cannot read \(path)") }
    return [UInt8](data)
}

func readText(_ path: String) -> String { String(decoding: read(path), as: UTF8.self) }

func write(_ path: String, _ bytes: [UInt8], mode: Int = 0o644) {
    let fm = FileManager.default
    try? fm.removeItem(atPath: path)
    guard fm.createFile(atPath: path, contents: Data(bytes), attributes: [.posixPermissions: mode]) else { fail("cannot write \(path)") }
}

func attempt<T>(_ what: String, _ body: () throws -> T) -> T {
    do { return try body() } catch { fail("\(what): \(error)") }
}

/// Options: --name VALUE (repeatable) and positional arguments.
struct Options {
    var values: [String: [String]] = [:]
    var positional: [String] = []

    init(_ args: ArraySlice<String>, flags: Set<String>) {
        var args = args
        while let a = args.popFirst() {
            if a.hasPrefix("-") && a != "-" {
                guard flags.contains(a), let v = args.popFirst() else { fail("unknown option or missing value: \(a)", code: 2) }
                values[a, default: []].append(v)
            } else {
                positional.append(a)
            }
        }
    }

    func one(_ name: String) -> String {
        guard let v = values[name], v.count == 1 else { fail("\(name) is required, once", code: 2) }
        return v[0]
    }

    func optional(_ name: String) -> String? { values[name]?.last }
    func all(_ name: String) -> [String] { values[name] ?? [] }
    func int(_ name: String, default d: UInt64) -> UInt64 {
        guard let s = optional(name) else { return d }
        guard let n = UInt64(s) else { fail("\(name): not a number: \(s)", code: 2) }
        return n
    }
}

func key(_ path: String) -> SigningKey { attempt(path) { try SigningKey(keyFile: readText(path)) } }
func roots(_ o: Options) -> [[UInt8]] {
    let r = o.all("--root").map { p in attempt(p) { try publicKey(file: readText(p)) } }
    if r.isEmpty { fail("--root is required", code: 2) }
    return r
}
func chain(_ o: Options) -> [Document] {
    o.all("--chain").flatMap { p in attempt(p) { try documents(readText(p)) } }
}
func now(_ o: Options) -> UInt64 { o.int("--now", default: UInt64(Date().timeIntervalSince1970)) }
func keyring(_ o: Options) -> [Document] {
    guard let dir = o.optional("--keyring") else { return [] }
    let names = (try? FileManager.default.contentsOfDirectory(atPath: dir)) ?? []
    return names.filter { $0.hasSuffix(".cert") }.sorted().flatMap { n in attempt(n) { try documents(readText(dir + "/" + n)) } }
}

/// Every regular file and link under root, sorted, modes normalised.
func walk(_ root: String) -> [Member] {
    let fm = FileManager.default
    guard let e = fm.enumerator(atPath: root) else { fail("cannot list \(root)") }
    var out: [Member] = []
    while let rel = e.nextObject() as? String {
        let full = root + "/" + rel
        var st = stat()
        guard lstat(full, &st) == 0 else { fail("cannot stat \(full)") }
        switch st.st_mode & S_IFMT {
        case S_IFREG:
            out.append(Member(path: rel, mode: st.st_mode & 0o111 != 0 ? 0o755 : 0o644, kind: .file(read(full))))
        case S_IFLNK:
            guard let t = try? fm.destinationOfSymbolicLink(atPath: full) else { fail("cannot read link \(full)") }
            out.append(Member(path: rel, mode: 0o755, kind: .link(t)))
        case S_IFDIR:
            continue
        default:
            fail("\(full): only files, links and directories can be packaged")
        }
    }
    return out
}

let args = CommandLine.arguments
guard args.count >= 2 else { fail("usage: see the header of tools/ndsign/Sources/ndsign/main.swift", code: 2) }
let rest = args[2...]

switch args[1] {
case "keygen":
    let o = Options(rest, flags: [])
    guard o.positional.count == 1 else { fail("usage: ndsign keygen OUT", code: 2) }
    let k = attempt("keygen") { try SigningKey.generate() }
    write(o.positional[0] + ".key", Array(k.keyFile.utf8), mode: 0o600)
    write(o.positional[0] + ".pub", Array(k.publicKeyFile.utf8))
    print(hex(k.publicKey))

case "certify":
    let o = Options(rest, flags: ["--issuer", "--kind", "--name", "--seq", "--previous", "--expires", "--scope", "--usage", "--key", "-o"])
    let previous = o.optional("--previous").map { p -> Document in
        guard let d = attempt(p, { try documents(readText(p)) }).first else { fail("\(p): no certificate") }
        return d
    }
    let request = CertificateRequest(
        kind: o.one("--kind"), name: o.one("--name"), seq: o.int("--seq", default: 1), previous: previous,
        expires: o.int("--expires", default: 0), scope: o.one("--scope"), usage: o.all("--usage"),
        key: attempt(o.one("--key")) { try publicKey(file: readText(o.one("--key"))) })
    let cert = attempt("certify") { try certify(request, issuer: key(o.one("--issuer"))) }
    write(o.one("-o"), Array(cert.utf8))

case "sign-tc":
    let o = Options(rest, flags: ["--chain", "--key", "--package", "--version", "--issued", "-o"])
    guard o.positional.count == 1 else { fail("usage: ndsign sign-tc ... MODULE -o GRANT", code: 2) }
    let module = read(o.positional[0])
    let grant = attempt("sign-tc") {
        try bundle(chain: chain(o), statement: [
            "kind": .string("trust-cache"), "package": .string(o.one("--package")), "version": .string(o.one("--version")),
            "module-sha256": .string(hex(sha256(module))), "tc-type": .string(loadableTCType),
            "issued": .int(o.int("--issued", default: 0)),
        ], key: key(o.one("--key")))
    }
    write(o.one("-o"), Array(grant.utf8))

case "verify-tc":
    let o = Options(rest, flags: ["--root", "--now", "--keyring"])
    guard o.positional.count == 2 else { fail("usage: ndsign verify-tc --root PUB MODULE GRANT", code: 2) }
    let grant = read(o.positional[1])
    attempt("grant refused") { try verifyGrant(grant, module: read(o.positional[0]), tcType: loadableTCType, roots: roots(o), now: now(o)) }
    let b = attempt("grant refused") { try verifyBundle(grant, roots: roots(o), now: now(o), kind: "trust-cache", keyring: keyring(o)) }
    print("ndsign: grant ok: \(b.statement["package"]!) \(b.statement["version"]!), release key \(b.release["name"]!) seq \(b.release["seq"]!), channel \(b.channel["name"]!) seq \(b.channel["seq"]!)")

case "pack":
    let o = Options(rest, flags: ["--name", "--version", "--arch", "--license", "--kind", "--provides", "--requires", "--conflicts",
                                  "--trust-cache", "--chain", "--key", "--issued", "-o", "--grant-out"])
    guard o.positional.count == 1 else { fail("usage: ndsign pack ... -o OUT ROOT", code: 2) }
    var m = Manifest()
    m.name = o.one("--name"); m.version = o.one("--version")
    m.arch = o.optional("--arch") ?? "aarch64"; m.license = o.optional("--license") ?? ""
    m.kind = o.optional("--kind") ?? "app"
    m.provides = o.all("--provides"); m.requires = o.all("--requires"); m.conflicts = o.all("--conflicts")
    let built = attempt("pack") {
        try buildPackage(m, payload: walk(o.positional[0]), trustCache: o.optional("--trust-cache").map(read),
                         signer: SignedBy(chain: chain(o), key: key(o.one("--key")), issued: o.int("--issued", default: 0)))
    }
    write(o.one("-o"), built.archive)
    if let g = o.optional("--grant-out") {
        guard let grant = built.grant else { fail("--grant-out without --trust-cache", code: 2) }
        write(g, Array(grant.utf8))
    }
    print("ndsign: \(o.one("-o")): \(m.name) \(m.version), \(built.archive.count) bytes")

case "verify", "unpack":
    let o = Options(rest, flags: ["--root", "--now", "--keyring"])
    guard o.positional.count == (args[1] == "verify" ? 1 : 2) else { fail("usage: ndsign \(args[1]) --root PUB PKG\(args[1] == "unpack" ? " DIR" : "")", code: 2) }
    let p = attempt("\(o.positional[0]) refused") { try verifyPackage(read(o.positional[0]), roots: roots(o), now: now(o), keyring: keyring(o)) }
    let s = p.signature
    print("ndsign: \(o.positional[0]): ok: \(p.manifest.name) \(p.manifest.version) (\(p.manifest.arch)), \(p.payload.count) file(s), payload \(p.manifest.payloadSHA256)")
    print("ndsign: signed by release key \(s.release["name"]!) seq \(s.release["seq"]!), channel \(s.channel["name"]!) seq \(s.channel["seq"]!), root \(s.channel["issuer"]!)")
    if let tc = p.manifest.trustCache { print("ndsign: trust cache: \(tc.entries) entries, granted") }
    if args[1] == "unpack" {
        let dir = o.positional[1], fm = FileManager.default
        for m in p.payload {
            let full = dir + "/files/" + m.path
            attempt("unpack") { try fm.createDirectory(atPath: (full as NSString).deletingLastPathComponent, withIntermediateDirectories: true) }
            switch m.kind {
            case .file(let d): write(full, d, mode: Int(m.mode))
            case .link(let t): attempt("unpack") { try fm.createSymbolicLink(atPath: full, withDestinationPath: t) }
            }
        }
        if let tc = p.trustCache, let g = p.grant {
            write(dir + "/trustcache", tc)
            write(dir + "/trustcache.grant", g)
        }
        write(dir + "/manifest.toml", Array(p.manifestText.utf8))
        write(dir + "/manifest.sig", p.manifestSig)
    }

default:
    fail("unknown command \(args[1])", code: 2)
}
