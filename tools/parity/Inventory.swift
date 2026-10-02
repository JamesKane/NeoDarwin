// SPDX-License-Identifier: BSD-2-Clause
//
// The parity inventory (tools/parity/inventory.tsv), its ratchet baseline
// (tools/parity/baseline.tsv), the checks over them and the coverage report.

import Foundation

let statuses = ["apple", "freebsd", "new", "equivalent", "port", "n/a", "todo"]

/// Statuses whose program NeoDarwin's base installs (under its FreeBSD name,
/// or the named equivalent).
let baseStatuses: Set<String> = ["apple", "freebsd", "new", "equivalent"]

struct Table {
    var comments: [String]
    var header: [String]
    var rows: [[String: String]]

    static func read(_ path: String, header expected: [String]) throws -> Table {
        guard let data = FileManager.default.contents(atPath: path) else {
            throw WalkError(description: "\(path): can't read")
        }
        var comments: [String] = []
        var header: [String]?
        var rows: [[String: String]] = []
        for (n, line) in String(decoding: data, as: UTF8.self).split(separator: "\n", omittingEmptySubsequences: false).enumerated() {
            if line.hasPrefix("#") { if header == nil { comments.append(String(line)) }; continue }
            if line.isEmpty { continue }
            let fields = line.split(separator: "\t", omittingEmptySubsequences: false).map(String.init)
            guard let h = header else {
                guard fields == expected else {
                    throw WalkError(description: "\(path):\(n + 1): header must be \(expected.joined(separator: " TAB "))")
                }
                header = fields
                continue
            }
            guard fields.count == h.count else {
                throw WalkError(description: "\(path):\(n + 1): \(fields.count) fields, want \(h.count)")
            }
            rows.append(Dictionary(uniqueKeysWithValues: zip(h, fields)))
        }
        return Table(comments: comments, header: header ?? expected, rows: rows)
    }

    func text() -> String {
        var out = comments.map { $0 + "\n" }.joined()
        out += header.joined(separator: "\t") + "\n"
        for r in rows { out += header.map { r[$0] ?? "" }.joined(separator: "\t") + "\n" }
        return out
    }
}

let inventoryHeader = ["path", "status", "source", "provides", "roadmap", "built", "note"]
let baselineHeader = ["path", "status", "built"]
let programsHeader = ["path", "cond", "aarch64", "names"]

func programsTable(_ programs: [Program], pin: String) -> String {
    var out = "# FreeBSD's base programs at \(pin), from tools/parity's walk of the pinned Makefiles.\n"
    out += "# cond: the Makefile condition that builds the directory (empty: always); aarch64: built by an arm64 build with no src.conf.\n"
    out += programsHeader.joined(separator: "\t") + "\n"
    for p in programs {
        out += [p.path, p.cond, p.aarch64 ? "yes" : "no", p.names.joined(separator: " ")].joined(separator: "\t") + "\n"
    }
    return out
}

/// The paths in a volume (one per line, relative to its root), as
/// rules/ramdisk.bzl's NAME_contents lists them.
struct Contents {
    var paths: Set<String>
    var programNames: Set<String>  // basenames in the program directories

    static let programDirectories = ["bin", "sbin", "usr/bin", "usr/sbin", "usr/libexec"]

    init(_ path: String) throws {
        guard let data = FileManager.default.contents(atPath: path) else {
            throw WalkError(description: "\(path): can't read")
        }
        paths = Set(String(decoding: data, as: UTF8.self).split(separator: "\n").map {
            String($0.drop { $0 == "/" })
        })
        programNames = Set(paths.compactMap { p in
            let dir = (p as NSString).deletingLastPathComponent
            return Contents.programDirectories.contains(dir) ? (p as NSString).lastPathComponent : nil
        })
    }

    /// Whether a row's program is installed: every `provides` path, or else
    /// its key name (keyName) in one of the program directories.
    func built(path: String, provides: String, names: [String]) -> Bool {
        let wanted = provides.split(separator: ",").map { String($0.trimmingCharacters(in: .whitespaces).drop { $0 == "/" }) }
        if !wanted.isEmpty { return wanted.allSatisfy { paths.contains($0) } }
        guard let key = keyName(path: path, names: names) else { return false }
        return programNames.contains(key)
    }
}

/// The name a row is known by: the installed name that matches its
/// directory (vi, not nvi; tar, not bsdtar), or else the first one.
func keyName(path: String, names: [String]) -> String? {
    let base = names.map { ($0 as NSString).lastPathComponent }
    let dir = (path as NSString).lastPathComponent
    return base.contains(dir) ? dir : base.first
}

struct Inputs {
    var programs: Table
    var inventory: Table
    var baseline: Table?
    var contents: Contents?
    var roadmapIDs: Set<String>?

    var namesByPath: [String: [String]] {
        Dictionary(uniqueKeysWithValues: programs.rows.map { ($0["path"]!, words($0["names"]!)) })
    }
}

func roadmapIDs(_ backlogPath: String) throws -> Set<String> {
    guard let data = FileManager.default.contents(atPath: backlogPath) else {
        throw WalkError(description: "\(backlogPath): can't read")
    }
    var ids: Set<String> = []
    for line in String(decoding: data, as: UTF8.self).split(separator: "\n") {
        let t = line.trimmingCharacters(in: .whitespaces)
        if t.hasPrefix("- id: ") { ids.insert(String(t.dropFirst(6))) }
    }
    return ids
}

// MARK: - Checks

/// (a) the inventory has exactly the pinned program set, once each.
func checkCoverage(_ i: Inputs) -> [String] {
    var errors: [String] = []
    let want = Set(i.programs.rows.map { $0["path"]! })
    var seen: Set<String> = []
    var previous = ""
    for r in i.inventory.rows {
        let p = r["path"]!
        if !seen.insert(p).inserted { errors.append("\(p): listed twice") }
        if !want.contains(p) { errors.append("\(p): not a program directory of the pinned FreeBSD (remove the row)") }
        if p < previous { errors.append("\(p): rows must be sorted by path (after \(previous))") }
        previous = p
    }
    for p in want.subtracting(seen).sorted() { errors.append("\(p): no row (bazel run //tools/parity:update adds it as todo)") }
    return errors
}

/// (b) every row has a valid status and what that status needs.
func checkStatuses(_ i: Inputs) -> [String] {
    var errors: [String] = []
    for r in i.inventory.rows {
        let p = r["path"]!, status = r["status"]!, source = r["source"]!, note = r["note"]!
        let roadmap = r["roadmap"]!, built = r["built"]!
        guard statuses.contains(status) else {
            errors.append("\(p): status '\(status)' is not one of \(statuses.joined(separator: ", "))")
            continue
        }
        if baseStatuses.contains(status) && source.isEmpty { errors.append("\(p): \(status) needs a source") }
        if status == "n/a" && note.isEmpty { errors.append("\(p): n/a needs the reason in note") }
        if status == "port" && source.isEmpty { errors.append("\(p): port needs the port's origin in source") }
        if built != "yes" && built != "no" { errors.append("\(p): built is '\(built)', not yes or no") }
        if baseStatuses.contains(status) && built == "no" && roadmap.isEmpty {
            errors.append("\(p): \(status) and not built: name the roadmap item that builds it")
        }
        if !roadmap.isEmpty, let ids = i.roadmapIDs, !ids.contains(roadmap) {
            errors.append("\(p): roadmap item \(roadmap) is not in roadmap/backlog.yaml")
        }
        if r.values.contains(where: { $0.contains("\t") || $0.contains("\n") }) { errors.append("\(p): a field has a tab") }
    }
    return errors
}

/// (c) the ratchet: no row loses its status or stops being built unless
/// the baseline says so; and the baseline is up to date.
func checkRatchet(_ i: Inputs) -> [String] {
    guard let baseline = i.baseline else { return ["no baseline"] }
    var errors: [String] = []
    let current = Dictionary(uniqueKeysWithValues: i.inventory.rows.map { ($0["path"]!, $0) })
    var stale = false
    var regressed = false
    for b in baseline.rows {
        let p = b["path"]!
        guard let r = current[p] else {
            errors.append("\(p): row removed (baseline has it as \(b["status"]!))")
            regressed = true
            continue
        }
        if b["status"]! != "todo" && r["status"]! == "todo" {
            errors.append("\(p): lost its status (\(b["status"]!) in the baseline, now todo)")
            regressed = true
        } else if b["built"]! == "yes" && r["built"]! != "yes" {
            errors.append("\(p): no longer built (built in the baseline)")
            regressed = true
        } else if b["status"]! != r["status"]! || b["built"]! != r["built"]! {
            stale = true
        }
    }
    if Set(baseline.rows.map { $0["path"]! }) != Set(current.keys) { stale = true }
    if regressed {
        errors.append("if the regression is intended, record it: bazel run //tools/parity:accept -- --regress")
    } else if stale {
        errors.append("the baseline is behind the inventory: record the progress with bazel run //tools/parity:accept")
    }
    return errors
}

/// (d) the built column is what the image holds.
func checkBuilt(_ i: Inputs) -> [String] {
    guard let contents = i.contents else { return ["no image contents"] }
    let names = i.namesByPath
    var errors: [String] = []
    for r in i.inventory.rows {
        let p = r["path"]!
        let actual = contents.built(path: p, provides: r["provides"]!, names: names[p] ?? [])
        if (r["built"]! == "yes") != actual {
            let what = r["provides"]!.isEmpty ? (keyName(path: p, names: names[p] ?? []) ?? "nothing") : r["provides"]!
            errors.append("\(p): built says \(r["built"]!), but the image \(actual ? "has" : "lacks") \(what) (bazel run //tools/parity:update)")
        }
    }
    return errors
}

// MARK: - Update and accept

/// Rows for new program directories (todo), none for removed ones, and the
/// built column from the image.
func updatedInventory(_ i: Inputs) -> Table {
    var t = i.inventory
    let names = i.namesByPath
    var byPath = Dictionary(uniqueKeysWithValues: t.rows.map { ($0["path"]!, $0) })
    for p in names.keys where byPath[p] == nil {
        byPath[p] = ["path": p, "status": "todo", "source": "", "provides": "", "roadmap": "", "built": "no", "note": ""]
    }
    t.rows = names.keys.sorted().map { p in
        var r = byPath[p]!
        if let c = i.contents { r["built"] = c.built(path: p, provides: r["provides"]!, names: names[p]!) ? "yes" : "no" }
        return r
    }
    return t
}

func acceptedBaseline(_ i: Inputs, comments: [String]) -> Table {
    Table(comments: comments, header: baselineHeader, rows: i.inventory.rows.map {
        ["path": $0["path"]!, "status": $0["status"]!, "built": $0["built"]!]
    })
}

// MARK: - Report

func coverageReport(_ i: Inputs, pin: String) -> String {
    let rows = i.inventory.rows
    let total = rows.count
    func pct(_ n: Int, _ d: Int) -> String { d == 0 ? "–" : String(format: "%.1f%%", Double(n) * 100 / Double(d)) }
    func top(_ p: String) -> String { String(p.prefix { $0 != "/" }) }
    let decided = rows.filter { $0["status"] != "todo" }.count
    let base = rows.filter { baseStatuses.contains($0["status"]!) }
    let built = base.filter { $0["built"] == "yes" }

    var out = "# FreeBSD parity coverage\n\n"
    out += "Generated by `bazel build //tools/parity:coverage` from `tools/parity/inventory.tsv` "
    out += "(docs/architecture/freebsd-parity.md §2). FreeBSD: \(pin).\n\n"
    out += "- Program directories: **\(total)**\n"
    out += "- Coverage (a status other than `todo`): **\(decided)/\(total) (\(pct(decided, total)))**\n"
    out += "- In the base (`apple`, `freebsd`, `new`, `equivalent`): \(base.count); built and installed: **\(built.count)/\(base.count) (\(pct(built.count, base.count)))**\n\n"

    out += "## By status and directory\n\n"
    out += "| Status | " + topDirectories.joined(separator: " | ") + " | Total | Built |\n"
    out += "|---|" + String(repeating: "---:|", count: topDirectories.count + 2) + "\n"
    for s in statuses {
        let rs = rows.filter { $0["status"] == s }
        out += "| `\(s)` | " + topDirectories.map { d in String(rs.filter { top($0["path"]!) == d }.count) }.joined(separator: " | ")
        out += " | \(rs.count) | \(rs.filter { $0["built"] == "yes" }.count) |\n"
    }
    out += "| **all** | " + topDirectories.map { d in String(rows.filter { top($0["path"]!) == d }.count) }.joined(separator: " | ")
    out += " | \(total) | \(rows.filter { $0["built"] == "yes" }.count) |\n\n"

    out += "## Built, by directory\n\n| Directory | In the base | Built | Share |\n|---|---:|---:|---:|\n"
    for d in topDirectories {
        let b = base.filter { top($0["path"]!) == d }
        let n = b.filter { $0["built"] == "yes" }.count
        out += "| `\(d)` | \(b.count) | \(n) | \(pct(n, b.count)) |\n"
    }

    out += "\n## Not yet built, by roadmap item\n\n"
    let missing = base.filter { $0["built"] != "yes" }
    for item in Set(missing.map { $0["roadmap"]! }).sorted() {
        let rs = missing.filter { $0["roadmap"] == item }
        out += "### \(item.isEmpty ? "(none)" : item) — \(rs.count)\n\n"
        var bySource: [String: [String]] = [:]
        for r in rs {
            // FreeBSD rows each name their own directory: one bucket for them.
            let key = r["status"] == "freebsd" ? "FreeBSD" : "\(r["source"]!) (`\(r["status"]!)`)"
            bySource[key, default: []].append((r["path"]! as NSString).lastPathComponent)
        }
        for (source, names) in bySource.sorted(by: { ($0.value.count, $1.key) > ($1.value.count, $0.key) }) {
            out += "- \(source), \(names.count): \(names.sorted().joined(separator: ", "))\n"
        }
        out += "\n"
    }
    let todo = rows.filter { $0["status"] == "todo" }
    if !todo.isEmpty {
        out += "## Undecided (`todo`)\n\n" + todo.map { "- `\($0["path"]!)`" }.joined(separator: "\n") + "\n"
    }
    return out
}
