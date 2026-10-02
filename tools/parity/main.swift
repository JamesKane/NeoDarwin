// SPDX-License-Identifier: BSD-2-Clause
//
// parity: the FreeBSD parity inventory (P4-20,
// docs/architecture/freebsd-parity.md §2).
//
// Usage:
//     parity programs ROOT --lock LOCK [--files]
//         Walks bin, sbin, usr.bin and usr.sbin of the FreeBSD source tree at
//         ROOT (the pinned files of tools/parity/freebsd.lock) and prints the
//         program set as TSV: path, cond, aarch64, names. --files prints the
//         files the walk read instead (tools/parity/lock.sh pins them).
//     parity check CHECK... --programs P --inventory I [--baseline B]
//             [--contents C] [--backlog Y]
//         CHECK is coverage (a), statuses (b), ratchet (c) or built (d), or
//         all. Prints every problem and exits 1 if there is one.
//     parity update --programs P --inventory I --contents C --out O
//         The inventory with a todo row for each new program directory,
//         without rows for removed ones, and its built column from the image.
//     parity accept --inventory I --baseline B --out O [--regress]
//         The baseline for the inventory; refuses a regression (a row that
//         loses its status or stops being built) without --regress.
//     parity report --programs P --inventory I --out O
//         The coverage report, in Markdown.

import Foundation

func fail(_ message: String) -> Never {
    FileHandle.standardError.write(Data("parity: \(message)\n".utf8))
    exit(2)
}

func write(_ text: String, to path: String) {
    do { try text.write(toFile: path, atomically: true, encoding: .utf8) } catch { fail("\(path): \(error)") }
}

var args = Array(CommandLine.arguments.dropFirst())
guard let command = args.first else { fail("usage: parity programs|check|update|accept|report ...") }
args.removeFirst()

var options: [String: String] = [:]
var flags: Set<String> = []
var positional: [String] = []
var k = 0
while k < args.count {
    let a = args[k]
    if ["--files", "--regress"].contains(a) {
        flags.insert(a)
    } else if a.hasPrefix("--") {
        guard k + 1 < args.count else { fail("\(a) needs a value") }
        options[a] = args[k + 1]
        k += 1
    } else {
        positional.append(a)
    }
    k += 1
}

@MainActor func option(_ name: String) -> String {
    guard let v = options[name] else { fail("\(command) needs \(name)") }
    return v
}

/// "15.1-RELEASE (tag release/15.1.0), freebsd-src 96841ea08dcf" from the
/// lock's # release: and # commit: lines.
func pin(lock path: String) -> String {
    guard let data = FileManager.default.contents(atPath: path) else { fail("\(path): can't read") }
    var release = "", commit = ""
    for line in String(decoding: data, as: UTF8.self).split(separator: "\n") where line.hasPrefix("#") {
        let t = line.dropFirst().trimmingCharacters(in: .whitespaces)
        if t.hasPrefix("release:") { release = t.dropFirst(8).trimmingCharacters(in: .whitespaces) }
        if t.hasPrefix("commit:") { commit = String(t.dropFirst(7).trimmingCharacters(in: .whitespaces).prefix(12)) }
    }
    return "\(release), freebsd-src \(commit)"
}

/// The pin line programs.tsv carries in its first comment.
func pin(programs table: Table) -> String {
    guard let first = table.comments.first, let r = first.range(of: " at "), let e = first.range(of: ", from") else {
        return "?"
    }
    return String(first[r.upperBound..<e.lowerBound])
}

@MainActor func inputs(baseline: Bool = false, contents: Bool = false, backlog: Bool = false) -> Inputs {
    do {
        return Inputs(
            programs: try Table.read(option("--programs"), header: programsHeader),
            inventory: try Table.read(option("--inventory"), header: inventoryHeader),
            baseline: baseline ? try Table.read(option("--baseline"), header: baselineHeader) : nil,
            contents: contents ? try Contents(option("--contents")) : nil,
            roadmapIDs: backlog ? try roadmapIDs(option("--backlog")) : nil)
    } catch {
        fail("\(error)")
    }
}

switch command {
case "programs":
    guard let root = positional.first else { fail("programs needs ROOT") }
    let tree = Tree(root)
    do {
        let programs = try freebsdPrograms(tree)
        if flags.contains("--files") {
            print(tree.read.sorted().joined(separator: "\n"))
        } else {
            print(programsTable(programs, pin: pin(lock: option("--lock"))), terminator: "")
        }
    } catch {
        fail("\(error)")
    }

case "check":
    var checks = positional
    if checks.isEmpty || checks == ["all"] { checks = ["coverage", "statuses", "ratchet", "built"] }
    let i = inputs(
        baseline: checks.contains("ratchet"), contents: checks.contains("built"),
        backlog: options["--backlog"] != nil)
    var failed = false
    for c in checks {
        let errors: [String]
        switch c {
        case "coverage": errors = checkCoverage(i)
        case "statuses": errors = checkStatuses(i)
        case "ratchet": errors = checkRatchet(i)
        case "built": errors = checkBuilt(i)
        default: fail("unknown check \(c)")
        }
        for e in errors { print("FAIL (\(c)): \(e)") }
        if errors.isEmpty { print("PASS (\(c)): \(i.inventory.rows.count) rows") }
        failed = failed || !errors.isEmpty
    }
    exit(failed ? 1 : 0)

case "update":
    let i = inputs(contents: true)
    write(updatedInventory(i).text(), to: option("--out"))

case "accept":
    let inventory: Table, baseline: Table
    do {
        inventory = try Table.read(option("--inventory"), header: inventoryHeader)
        baseline = try Table.read(option("--baseline"), header: baselineHeader)
    } catch {
        fail("\(error)")
    }
    let i = Inputs(programs: Table(comments: [], header: programsHeader, rows: []), inventory: inventory, baseline: baseline)
    let regressions = checkRatchet(i).filter { !$0.contains("baseline is behind") && !$0.contains("accept") }
    if !regressions.isEmpty && !flags.contains("--regress") {
        for r in regressions { print("regression: \(r)") }
        fail("refusing to record \(regressions.count) regression(s) without --regress")
    }
    write(acceptedBaseline(i, comments: baseline.comments).text(), to: option("--out"))

case "report":
    let i = inputs()
    write(coverageReport(i, pin: pin(programs: i.programs)), to: option("--out"))

default:
    fail("unknown command \(command)")
}
