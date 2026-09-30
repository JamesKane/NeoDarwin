// SPDX-License-Identifier: BSD-2-Clause
//
// trustcache: static trust caches for NeoDarwin images (docs/kernel/amfi-provider.md,
// "P1-15"). neoboot loads one from \NeoDarwin\trustcache on the ESP; the
// kernel runs a binary as a platform binary, with its entitlements, only
// when its cdhash is listed.
//
// Usage:
//     trustcache create OUT [--manifest FILE] [--exclude PATH]... ROOT
//         Lists every signed arm64 Mach-O under ROOT (a staged image root;
//         symbolic links are not followed: their targets are in the image
//         themselves) after verifying its signature against its contents.
//         --exclude leaves out a path relative to ROOT (a file, or a
//         directory and everything under it). An unsigned Mach-O, or one
//         whose signature doesn't match, is an error unless excluded.
//         --manifest writes "CDHASH PATH" lines, one per listed binary.
//     trustcache cdhash FILE
//         Prints the cdhash of each arm64 image in FILE, as the kernel computes it.
//     trustcache dump MODULE
//         Checks a module as the kernel's loader does and prints it.

import Foundation
import TrustCache

func fail(_ message: String, code: Int32 = 1) -> Never {
    FileHandle.standardError.write(Data("trustcache: \(message)\n".utf8))
    exit(code)
}

let usage = """
    usage: trustcache create OUT [--manifest FILE] [--exclude PATH]... ROOT
           trustcache cdhash FILE
           trustcache dump MODULE
    """

func read(_ path: String) -> [UInt8] {
    guard let data = FileManager.default.contents(atPath: path) else { fail("cannot read \(path)") }
    return [UInt8](data)
}

func write(_ path: String, _ bytes: [UInt8]) {
    guard FileManager.default.createFile(atPath: path, contents: Data(bytes)) else { fail("cannot write \(path)") }
}

func excluded(_ path: String, _ exclusions: [String]) -> Bool {
    exclusions.contains { path == $0 || path.hasPrefix($0 + "/") }
}

func create(_ args: [String]) {
    var args = args
    guard !args.isEmpty else { fail(usage, code: 2) }
    let out = args.removeFirst()
    var manifestPath: String? = nil
    var exclusions: [String] = []
    var root: String? = nil
    while !args.isEmpty {
        let a = args.removeFirst()
        switch a {
        case "--manifest":
            guard !args.isEmpty else { fail(usage, code: 2) }
            manifestPath = args.removeFirst()
        case "--exclude":
            guard !args.isEmpty else { fail(usage, code: 2) }
            exclusions.append(args.removeFirst().trimmingCharacters(in: CharacterSet(charactersIn: "/")))
        default:
            guard root == nil else { fail(usage, code: 2) }
            root = a
        }
    }
    guard let root else { fail(usage, code: 2) }
    let fm = FileManager.default
    guard let walker = fm.enumerator(atPath: root) else { fail("cannot list \(root)") }
    var paths: [String] = []
    while let p = walker.nextObject() as? String { paths.append(p) }
    paths.sort()

    var entries: [TrustCacheEntry] = []
    var manifest: [String] = []
    var errors: [String] = []
    var skipped = 0
    for path in paths {
        let full = root + "/" + path
        guard let attrs = try? fm.attributesOfItem(atPath: full),
              attrs[.type] as? FileAttributeType == .typeRegular else { continue }
        let bytes = read(full)
        guard CodeSignature.isMachO(bytes) else { continue }
        if excluded(path, exclusions) {
            skipped += 1
            continue
        }
        do throws(SignatureError) {
            for image in try CodeSignature.images(bytes) {
                entries.append(TrustCacheEntry(cdhash: image.cdhash, hashType: image.hashType))
                manifest.append("\(hex(image.cdhash)) \(path)")
            }
        } catch .notMachO {
            continue  // a Mach-O for another architecture
        } catch {
            errors.append("\(path): \(error)")
        }
    }
    guard errors.isEmpty else { fail("not listed:\n  " + errors.joined(separator: "\n  ")) }
    let module = TrustCacheModule(entries: entries)
    write(out, module.bytes)
    if let manifestPath { write(manifestPath, Array((manifest.joined(separator: "\n") + "\n").utf8)) }
    print("trustcache: \(out): \(module.entries.count) entries, UUID \(uuidString(module.uuid))"
        + (skipped > 0 ? "; \(skipped) Mach-O file(s) excluded" : ""))
}

var args = Array(CommandLine.arguments.dropFirst())
guard !args.isEmpty else { fail(usage, code: 2) }
switch args.removeFirst() {
case "create":
    create(args)
case "cdhash":
    guard args.count == 1 else { fail(usage, code: 2) }
    do throws(SignatureError) {
        for image in try CodeSignature.images(read(args[0])) {
            print("\(hex(image.cdhash)) hash-type \(image.hashType) flags 0x\(String(image.flags, radix: 16)) \(image.identifier)"
                + (image.hasEntitlements ? " entitlements" : ""))
        }
    } catch {
        fail("\(args[0]): \(error)")
    }
case "dump":
    guard args.count == 1 else { fail(usage, code: 2) }
    do throws(TrustCacheError) {
        let module = try TrustCacheModule(bytes: read(args[0]))
        print("version 1, UUID \(uuidString(module.uuid)), \(module.entries.count) entries")
        for e in module.entries { print("\(hex(e.cdhash)) \(e.hashType) \(e.flags)") }
    } catch {
        fail("\(args[0]): \(error)")
    }
default:
    fail(usage, code: 2)
}
