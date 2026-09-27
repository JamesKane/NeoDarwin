// SPDX-License-Identifier: BSD-2-Clause
//
// lang_audit: enforce the NeoDarwin language policy's fallback rule.
//
// Every first-party C, C++, Objective-C or assembly file must carry, within
// its first 20 lines, a justification naming one of the three grounds in
// docs/architecture/language-policy.md §2:
//
//     NeoDarwin-Language: portability: <reason>
//     NeoDarwin-Language: performance: <reason>
//     NeoDarwin-Language: expressibility: <reason>
//
// Usage:
//     lang_audit FILE...          audit the named files
//     lang_audit --tree DIR       audit every C-family file under DIR,
//                                 skipping docs/, third_party/, bazel-* and testdata/

import Foundation

let cFamily: Set<String> = ["c", "h", "cc", "cpp", "cxx", "hh", "hpp", "hxx", "m", "mm", "s", "S"]
let skippedTopLevel: Set<String> = ["docs", "third_party", ".git"]
let grounds = ["portability", "performance", "expressibility"]
let marker = "NeoDarwin-Language:"
let headerLines = 20

enum Verdict {
    case ok(ground: String)
    case missing
    case badGround(String)
    case unreadable
}

func audit(_ path: String) -> Verdict {
    guard let text = try? String(contentsOfFile: path, encoding: .utf8) else { return .unreadable }
    for line in text.split(separator: "\n", omittingEmptySubsequences: false).prefix(headerLines) {
        guard let range = line.range(of: marker) else { continue }
        let rest = line[range.upperBound...].trimmingCharacters(in: .whitespaces)
        let ground = rest.split(separator: ":", maxSplits: 1).first.map { $0.trimmingCharacters(in: .whitespaces) } ?? ""
        let hasReason = rest.split(separator: ":", maxSplits: 1).count == 2
        if grounds.contains(ground) && hasReason { return .ok(ground: ground) }
        return .badGround(ground)
    }
    return .missing
}

func walk(_ root: String) -> [String] {
    let fm = FileManager.default
    guard let e = fm.enumerator(atPath: root) else { return [] }
    var out: [String] = []
    while let rel = e.nextObject() as? String {
        let parts = rel.split(separator: "/").map(String.init)
        let top = parts.first ?? rel
        if skippedTopLevel.contains(top) || top.hasPrefix("bazel-") || parts.last == "testdata" {
            e.skipDescendants()
            continue
        }
        let ext = (rel as NSString).pathExtension
        if cFamily.contains(ext) { out.append((root as NSString).appendingPathComponent(rel)) }
    }
    return out.sorted()
}

var args = Array(CommandLine.arguments.dropFirst())
var files: [String]
if args.first == "--tree" {
    guard args.count == 2 else {
        FileHandle.standardError.write(Data("usage: lang_audit --tree DIR\n".utf8))
        exit(2)
    }
    files = walk(args[1])
} else {
    files = args
}

var failures = 0
for f in files {
    switch audit(f) {
    case .ok(let ground):
        print("ok    \(ground.padding(toLength: 14, withPad: " ", startingAt: 0)) \(f)")
    case .missing:
        print("FAIL  missing \(marker) line   \(f)")
        failures += 1
    case .badGround(let g):
        print("FAIL  ground '\(g)' is not one of \(grounds.joined(separator: ", ")) (or no reason)   \(f)")
        failures += 1
    case .unreadable:
        print("FAIL  unreadable   \(f)")
        failures += 1
    }
}
print("\(files.count) file(s), \(failures) failure(s)")
exit(failures == 0 ? 0 : 1)
