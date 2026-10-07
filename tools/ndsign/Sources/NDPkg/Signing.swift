// SPDX-License-Identifier: BSD-2-Clause
//
// ndsign's keys, documents and bundles (docs/architecture/packaging.md §4).
// The encoding and the verification are ndamfi's C (nd_ndsign.c), the code
// the kernel runs; this file builds documents and adds what needs state the
// kernel doesn't keep: a key ring of known certificates (seq and previous).

import NDSignC

public struct NDSignError: Error, CustomStringConvertible {
    public let description: String
    public init(_ description: String) { self.description = description }
}

// MARK: - bytes

public func hex(_ bytes: [UInt8]) -> String {
    let digits = Array("0123456789abcdef".utf8)
    var out: [UInt8] = []
    out.reserveCapacity(bytes.count * 2)
    for b in bytes {
        out.append(digits[Int(b >> 4)])
        out.append(digits[Int(b & 15)])
    }
    return String(decoding: out, as: UTF8.self)
}

public func unhex(_ text: String, count: Int) -> [UInt8]? {
    let chars = Array(text.utf8)
    guard chars.count == 2 * count else { return nil }
    var out = [UInt8](repeating: 0, count: count)
    let ok = out.withUnsafeMutableBufferPointer { o in
        chars.withUnsafeBufferPointer { c in
            c.baseAddress!.withMemoryRebound(to: CChar.self, capacity: c.count) {
                nd_ndsign_unhex(o.baseAddress, count, $0, c.count)
            }
        }
    }
    return ok ? out : nil
}

public func sha256(_ bytes: [UInt8]) -> [UInt8] {
    var out = [UInt8](repeating: 0, count: 32)
    out.withUnsafeMutableBufferPointer { o in bytes.withUnsafeBufferPointer { nd_sha256(o.baseAddress, $0.baseAddress, $0.count) } }
    return out
}

// MARK: - keys

/// An Ed25519 key pair. A key file holds the 32-byte seed in hex; a public
/// key file the 32-byte public key in hex (each one line).
public struct SigningKey: Sendable {
    public let seed: [UInt8]
    public let publicKey: [UInt8]
    let secret: [UInt8]  // seed || public key, as SUPERCOP keeps it

    public init(seed: [UInt8]) throws {
        guard seed.count == 32 else { throw NDSignError("a key's seed is 32 bytes") }
        var pk = [UInt8](repeating: 0, count: 32), sk = [UInt8](repeating: 0, count: 64)
        let ok = pk.withUnsafeMutableBufferPointer { p in
            sk.withUnsafeMutableBufferPointer { s in
                seed.withUnsafeBufferPointer { nd_ed25519_keypair_from_seed(p.baseAddress, s.baseAddress, $0.baseAddress) }
            }
        }
        guard ok else { throw NDSignError("cannot derive the key pair") }
        self.seed = seed
        self.publicKey = pk
        self.secret = sk
    }

    public static func generate() throws -> SigningKey {
        var seed = [UInt8](repeating: 0, count: 32)
        seed.withUnsafeMutableBytes { nd_random_bytes($0.baseAddress, 32) }
        return try SigningKey(seed: seed)
    }

    public init(keyFile text: String) throws {
        guard let seed = unhex(trimmed(text), count: 32) else { throw NDSignError("not a key file (64 hex digits)") }
        try self.init(seed: seed)
    }

    public var keyFile: String { hex(seed) + "\n" }
    public var publicKeyFile: String { hex(publicKey) + "\n" }

    public func sign(_ message: [UInt8]) throws -> [UInt8] {
        var sig = [UInt8](repeating: 0, count: 64)
        let ok = sig.withUnsafeMutableBufferPointer { s in
            message.withUnsafeBufferPointer { m in
                secret.withUnsafeBufferPointer { nd_ed25519_sign(s.baseAddress, m.baseAddress, m.count, $0.baseAddress) }
            }
        }
        guard ok else { throw NDSignError("cannot sign") }
        return sig
    }
}

public func publicKey(file text: String) throws -> [UInt8] {
    guard let key = unhex(trimmed(text), count: 32) else { throw NDSignError("not a public key file (64 hex digits)") }
    return key
}

func trimmed(_ text: String) -> String {
    var s = Substring(text)
    while let c = s.last, c == "\n" || c == " " || c == "\r" { s.removeLast() }
    return String(s)
}

// MARK: - documents

/// A field value: canonical TOML integers and strings only.
public enum Value: Sendable, Equatable {
    case int(UInt64)
    case string(String)

    var encoded: String {
        switch self {
        case .int(let n): return String(n)
        case .string(let s): return "\"" + s + "\""
        }
    }
}

public let domain = Array("NeoDarwin ndsign 1\n".utf8)

/// A signed document: canonical body lines, then the signature line.
public func signDocument(_ fields: [String: Value], with key: SigningKey) throws -> String {
    var body = ""
    for name in fields.keys.sorted(by: { Array($0.utf8).lexicographicallyPrecedes(Array($1.utf8)) }) {
        guard name != "signature", !name.isEmpty,
              name.utf8.allSatisfy({ ($0 >= 0x61 && $0 <= 0x7a) || ($0 >= 0x30 && $0 <= 0x39) || $0 == 0x2d })
        else { throw NDSignError("bad field name \(name)") }
        if case .string(let s) = fields[name]!,
           !s.utf8.allSatisfy({ $0 >= 0x20 && $0 <= 0x7e && $0 != 0x22 && $0 != 0x5c }) {
            throw NDSignError("field \(name): only printable ASCII without quotes or backslashes")
        }
        body += name + " = " + fields[name]!.encoded + "\n"
    }
    let sig = try key.sign(domain + Array(body.utf8))
    let doc = body + "signature = \"" + hex(sig) + "\"\n"
    guard Document(doc) != nil else { throw NDSignError("document is not canonical") }
    return doc
}

/// A parsed document (form only; signatures are checked by bundle verification).
public struct Document: Sendable {
    public let text: String
    public let body: [UInt8]

    public init?(_ text: String) {
        let bytes = Array(text.utf8)
        var sig = [UInt8](repeating: 0, count: 64)
        var span = nd_ndsign_span()
        let ok = bytes.withUnsafeBufferPointer { b in
            b.baseAddress!.withMemoryRebound(to: CChar.self, capacity: b.count) { p in
                sig.withUnsafeMutableBufferPointer { nd_ndsign_parse_doc(p, b.count, &span, $0.baseAddress) }
            }
        }
        guard ok else { return nil }
        self.text = text
        self.body = Array(bytes[0..<span.n])
    }

    public subscript(_ name: String) -> String? {
        body.withUnsafeBufferPointer { b in
            b.baseAddress!.withMemoryRebound(to: CChar.self, capacity: b.count) { p in
                var value = nd_ndsign_span()
                guard nd_ndsign_field(nd_ndsign_span(p: p, n: b.count), name, &value) else { return nil }
                return value.p.withMemoryRebound(to: UInt8.self, capacity: value.n) {
                    String(decoding: UnsafeBufferPointer(start: $0, count: value.n), as: UTF8.self)
                }
            }
        }
    }

    public var digest: String { hex(sha256(Array(text.utf8))) }
}

// MARK: - certificates

public struct CertificateRequest: Sendable {
    public var kind: String       // "channel" (issued by a root) or "release" (by a channel key)
    public var name: String
    public var seq: UInt64
    public var previous: Document?
    public var expires: UInt64
    public var scope: String
    public var usage: [String]
    public var key: [UInt8]

    public init(kind: String, name: String, seq: UInt64, previous: Document?, expires: UInt64, scope: String,
                usage: [String], key: [UInt8]) {
        self.kind = kind; self.name = name; self.seq = seq; self.previous = previous
        self.expires = expires; self.scope = scope; self.usage = usage; self.key = key
    }
}

public func certify(_ r: CertificateRequest, issuer: SigningKey) throws -> String {
    guard r.kind == "channel" || r.kind == "release" else { throw NDSignError("kind is channel or release") }
    guard r.seq >= 1 else { throw NDSignError("seq starts at 1") }
    let usage = r.usage.sorted()
    guard !usage.isEmpty, usage.allSatisfy({ $0 == "manifest" || $0 == "trust-cache" }) else {
        throw NDSignError("usage is one or both of manifest, trust-cache")
    }
    var previous = ""
    if let p = r.previous {
        guard p["kind"] == r.kind, p["name"] == r.name, p["issuer"] == hex(issuer.publicKey) else {
            throw NDSignError("the previous certificate has another kind, name or issuer")
        }
        guard let pseq = p["seq"].flatMap({ UInt64($0) }), pseq + 1 == r.seq else {
            throw NDSignError("seq must follow the previous certificate's")
        }
        previous = p.digest
    } else if r.seq != 1 {
        throw NDSignError("seq \(r.seq) needs --previous")
    }
    return try signDocument([
        "expires": .int(r.expires), "issuer": .string(hex(issuer.publicKey)), "key": .string(hex(r.key)),
        "kind": .string(r.kind), "name": .string(r.name), "previous": .string(previous), "schema": .int(1),
        "scope": .string(r.scope), "seq": .int(r.seq), "usage": .string(usage.joined(separator: " ")),
    ], with: issuer)
}

/// A bundle: the channel and release certificates, then a statement the
/// release key signs.
public func bundle(chain: [Document], statement fields: [String: Value], key: SigningKey) throws -> String {
    guard chain.count == 2 else { throw NDSignError("a chain is a channel and a release certificate") }
    guard chain[1]["key"] == hex(key.publicKey) else { throw NDSignError("the key is not the release certificate's") }
    var f = fields
    f["issuer"] = .string(hex(key.publicKey))
    f["schema"] = .int(1)
    return chain[0].text + "\n" + chain[1].text + "\n" + (try signDocument(f, with: key))
}

/// Splits a chain file (certificates separated by empty lines).
public func documents(_ text: String) throws -> [Document] {
    var docs: [Document] = []
    var current = ""
    for line in text.split(separator: "\n", omittingEmptySubsequences: false).dropLast() {
        if line.isEmpty {
            guard let d = Document(current) else { throw NDSignError("malformed document") }
            docs.append(d); current = ""
        } else {
            current += line + "\n"
        }
    }
    if !current.isEmpty {
        guard let d = Document(current) else { throw NDSignError("malformed document") }
        docs.append(d)
    }
    return docs
}

public struct VerifiedBundle: Sendable {
    public let channel: Document
    public let release: Document
    public let statement: Document
}

/// The kernel's verification (nd_ndsign_verify_bundle), then the key ring:
/// a certificate superseded by a known one of the same issuer and name
/// (a higher seq), conflicting with one (same seq, other bytes), or whose
/// `previous` doesn't name the known certificate before it, is refused.
public func verifyBundle(_ text: [UInt8], roots: [[UInt8]], now: UInt64, kind: String,
                         keyring: [Document] = []) throws -> VerifiedBundle {
    let flat = roots.flatMap { $0 }
    var b = nd_ndsign_bundle()
    let e = text.withUnsafeBufferPointer { t in
        flat.withUnsafeBufferPointer { nd_ndsign_verify_bundle(t.baseAddress, t.count, $0.baseAddress, roots.count, now, kind, &b) }
    }
    guard e == ND_NDSIGN_OK else { throw NDSignError(String(cString: nd_ndsign_error_name(e))) }
    let docs = try documents(String(decoding: text, as: UTF8.self))
    guard docs.count == 3 else { throw NDSignError("malformed") }
    for cert in docs[0..<2] {
        let seq = UInt64(cert["seq"] ?? "") ?? 0
        for known in keyring where known["issuer"] == cert["issuer"] && known["name"] == cert["name"] && known["kind"] == cert["kind"] {
            let kseq = UInt64(known["seq"] ?? "") ?? 0
            if kseq > seq {
                throw NDSignError("\(cert["kind"]!) certificate \(cert["name"]!) seq \(seq) is superseded by seq \(kseq)")
            }
            if kseq == seq && known.text != cert.text {
                throw NDSignError("\(cert["kind"]!) certificate \(cert["name"]!) seq \(seq) conflicts with a known one")
            }
            if kseq + 1 == seq && cert["previous"] != known.digest {
                throw NDSignError("\(cert["kind"]!) certificate \(cert["name"]!) seq \(seq) does not follow the known seq \(kseq)")
            }
        }
    }
    return VerifiedBundle(channel: docs[0], release: docs[1], statement: docs[2])
}

/// A trust-cache grant: the kernel's check (nd_ndsign_verify_tc_grant).
public func verifyGrant(_ grant: [UInt8], module: [UInt8], tcType: String, roots: [[UInt8]], now: UInt64) throws {
    let flat = roots.flatMap { $0 }
    let e = grant.withUnsafeBufferPointer { g in
        module.withUnsafeBufferPointer { m in
            flat.withUnsafeBufferPointer {
                nd_ndsign_verify_tc_grant(g.baseAddress, g.count, m.baseAddress, m.count, tcType, $0.baseAddress, roots.count, now)
            }
        }
    }
    guard e == ND_NDSIGN_OK else { throw NDSignError(String(cString: nd_ndsign_error_name(e))) }
}
