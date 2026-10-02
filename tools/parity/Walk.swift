// SPDX-License-Identifier: BSD-2-Clause
//
// The FreeBSD program set: the SUBDIR entries of bin, sbin, usr.bin and
// usr.sbin, as their Makefiles build them, with the names each installs.
//
// A row is a program directory: a top-level entry, or, under an entry that
// only collects subdirectories (usr.sbin/acpi, usr.bin/clang), each
// descendant that installs a program. Subdirectories of a program directory
// are part of its row (helpers such as bsdinstall's in /usr/libexec), and the
// programs they install in a bin or sbin directory are added to its names
// (routed's rtquery). tests directories are FreeBSD's test suite, not
// programs. Every architecture's entries are included (Makefile.<arch>, as
// bsd.arch.inc.mk includes them), with the condition that selects them.

import Foundation

let topDirectories = ["bin", "sbin", "usr.bin", "usr.sbin"]

/// Every Makefile.<arch> bsd.arch.inc.mk can include (MACHINE, MACHINE_ARCH
/// or MACHINE_CPUARCH), with arm64's first: arm64, then aarch64, as the
/// .elif chain takes them.
let architectures = [
    "arm64", "aarch64", "amd64", "i386", "arm", "armv7", "powerpc", "powerpc64", "powerpc64le",
    "powerpcspe", "riscv", "riscv64",
]

struct Program {
    var path: String
    var cond: String
    var aarch64: Bool
    var names: [String]
}

/// Reads files under a FreeBSD source root and remembers which it read, for
/// the lock file (tools/parity/lock.sh).
final class Tree {
    let root: URL
    private(set) var read: Set<String> = []

    init(_ root: String) { self.root = URL(fileURLWithPath: root) }

    func exists(_ path: String) -> Bool {
        FileManager.default.fileExists(atPath: root.appendingPathComponent(path).path)
    }

    func text(_ path: String) -> String? {
        guard let data = FileManager.default.contents(atPath: root.appendingPathComponent(path).path) else { return nil }
        read.insert(path)
        return String(decoding: data, as: UTF8.self)
    }
}

struct WalkError: Error, CustomStringConvertible {
    var description: String
}

/// One directory's Makefile: its SUBDIR entries (with conditions) and its
/// variables, with ../Makefile.inc and bsd.own.mk's defaults applied after
/// it, as bsd.prog.mk's inclusion of bsd.init.mk does.
struct DirectoryMakefile {
    var subdirs: [(name: String, cond: String, holds: Bool)] = []
    var variables = Variables()
}

func readDirectory(_ tree: Tree, _ dir: String, options: Variables) throws -> DirectoryMakefile {
    guard let text = tree.text(dir + "/Makefile") else {
        throw WalkError(description: "\(dir)/Makefile: not in the tree (re-run tools/parity/lock.sh)")
    }
    var result = DirectoryMakefile()
    result.variables.values[".CURDIR"] = dir
    result.variables.values["SRCTOP"] = ""
    var stack = ConditionStack()

    func run(_ text: String, file: String, extraCond: String, extraHolds: Bool) {
        for s in statements(text) {
            switch s {
            case let .directive(keyword, argument):
                var env = options
                env.values.merge(result.variables.values) { a, _ in a }
                if stack.apply(keyword, argument, env) { continue }
                if keyword == "include", argument.contains("bsd.arch.inc.mk") {
                    var first = true
                    for arch in architectures {
                        let path = "\(dir)/Makefile.\(arch)"
                        guard let t = tree.text(path) else { continue }
                        let holds = first && (arch == "arm64" || arch == "aarch64")
                        if arch == "arm64" || arch == "aarch64" { first = false }
                        run(t, file: path, extraCond: "arch=\(arch)", extraHolds: holds)
                    }
                }
            case let .assign(name, op, value):
                guard stack.readable else { continue }
                let key = result.variables.expand(name)
                if key == "SUBDIR" || key.hasPrefix("SUBDIR.") {
                    if op == "=" || op == ":=" { result.subdirs.removeAll() }
                    var conds = [extraCond, stack.text].filter { !$0.isEmpty }
                    var holds = extraHolds && stack.holds
                    if key.hasPrefix("SUBDIR.") {
                        // SUBDIR.${MK_X}, SUBDIR.${MK_X}.${MK_Y}: built when each is yes.
                        for option in name.dropFirst("SUBDIR.".count).split(separator: ".") {
                            conds.append(displayCondition(option + " != no"))
                            var e = ConditionEvaluator(options)
                            holds = holds && e.evaluate(option + " == yes")
                        }
                    }
                    for entry in words(result.variables.expand(value)) where entry != ".WAIT" && entry != "tests" {
                        result.subdirs.append((entry, conjunction(conds), holds))
                    }
                } else {
                    result.variables.apply(name, op, value)
                }
            }
        }
    }
    run(text, file: dir + "/Makefile", extraCond: "", extraHolds: true)

    // bsd.init.mk: ${.CURDIR}/../Makefile.inc, and the files it includes
    // ("../Makefile.inc", "${.CURDIR:H:H}/Makefile.inc").
    var seen: Set<String> = []
    func include(_ path: String) {
        guard !seen.contains(path), let t = tree.text(path) else { return }
        seen.insert(path)
        for s in statements(t) {
            switch s {
            case let .assign(name, op, value): result.variables.apply(name, op, value)
            case let .directive(keyword, argument):
                guard keyword == "include", argument.hasPrefix("\"") else { continue }
                let raw = argument.trimmingCharacters(in: CharacterSet(charactersIn: "\""))
                var target = result.variables.expand(raw)
                if !raw.contains("${") {
                    target = (path as NSString).deletingLastPathComponent + "/" + target
                }
                include(normalized(target))
            }
        }
    }
    include(normalized(dir + "/../Makefile.inc"))
    for (name, value) in [
        ("BINDIR", "/usr/bin"), ("LIBEXECDIR", "/usr/libexec"), ("SHAREDIR", "/usr/share"),
        ("LIBDIR", "/usr/lib"), ("SCRIPTSDIR", "${BINDIR}"),
    ] {
        result.variables.apply(name, "?=", value)
    }
    return result
}

/// a/b/../c → a/c, for paths relative to the tree's root.
func normalized(_ path: String) -> String {
    var parts: [Substring] = []
    for p in path.split(separator: "/") {
        if p == "." { continue }
        if p == "..", let last = parts.last, last != ".." { parts.removeLast(); continue }
        parts.append(p)
    }
    return parts.joined(separator: "/")
}

/// The paths a directory installs programs at: PROG, PROG_CXX, PROGS,
/// SCRIPTS, and the LINKS and SYMLINKS made to them.
func installedNames(_ m: DirectoryMakefile) -> [String] {
    let v = m.variables
    func value(_ name: String) -> String { v.expand(v.values[name] ?? "") }
    let bindir = value("BINDIR")
    var names: [String] = []
    for p in words(value("PROG")) + words(value("PROG_CXX")) {
        let installed = words(value("PROGNAME")).first ?? p
        names.append(bindir + "/" + installed)
    }
    for p in words(value("PROGS")) + words(value("PROGS_CXX")) {
        let dir = v.values["BINDIR.\(p)"].map { v.expand($0) } ?? bindir
        names.append(dir + "/" + p)
    }
    let scripts = words(value("SCRIPTS"))
    for s in scripts {
        let t = (s as NSString).lastPathComponent
        let name = v.values["SCRIPTSNAME_\(t)"].map { v.expand($0) }
            ?? v.values["SCRIPTSNAME"].map { v.expand($0) }
            ?? (t as NSString).deletingPathExtension
        let dir = v.values["SCRIPTSDIR_\(t)"].map { v.expand($0) } ?? value("SCRIPTSDIR")
        names.append(dir + "/" + name)
    }
    if names.isEmpty { return [] }
    let links = words(value("LINKS"))
    for i in stride(from: 1, to: links.count, by: 2) { names.append(links[i]) }
    let symlinks = words(value("SYMLINKS"))
    for i in stride(from: 1, to: symlinks.count, by: 2) where symlinks[i].hasPrefix("/") { names.append(symlinks[i]) }
    var seen: Set<String> = []
    return names.map { $0.replacingOccurrences(of: "//", with: "/") }.filter { seen.insert($0).inserted }
}

private let binDirectories: Set<String> = ["/bin", "/sbin", "/usr/bin", "/usr/sbin"]

func freebsdPrograms(_ tree: Tree) throws -> [Program] {
    var optionFiles: [String] = []
    for path in ["share/mk/bsd.opts.mk", "share/mk/src.opts.mk"] {
        guard let text = tree.text(path) else { throw WalkError(description: "\(path): not in the tree") }
        optionFiles.append(text)
    }
    let options = aarch64Options(optionFiles)
    var rows: [String: Program] = [:]

    func add(_ path: String, _ cond: String, _ holds: Bool, _ names: [String]) {
        if var r = rows[path] {
            r.cond = r.cond.isEmpty || cond.isEmpty ? "" : "\(r.cond) || \(cond)"
            r.aarch64 = r.aarch64 || holds
            rows[path] = r
        } else {
            rows[path] = Program(path: path, cond: cond, aarch64: holds, names: names)
        }
    }

    // Programs a program directory's subdirectories put in bin or sbin.
    func helperNames(_ dir: String, _ m: DirectoryMakefile) throws -> [String] {
        var out: [String] = []
        for s in m.subdirs {
            let sub = dir + "/" + s.name
            let sm = try readDirectory(tree, sub, options: options)
            out += installedNames(sm).filter { binDirectories.contains(($0 as NSString).deletingLastPathComponent) }
            out += try helperNames(sub, sm)
        }
        return out
    }

    func visit(_ dir: String, _ cond: String, _ holds: Bool, topLevel: Bool) throws {
        let m = try readDirectory(tree, dir, options: options)
        let names = installedNames(m)
        if names.isEmpty && !m.subdirs.isEmpty {
            for s in m.subdirs {
                let c = conjunction([cond, s.cond])
                try visit(dir + "/" + s.name, c, holds && s.holds, topLevel: false)
            }
        } else if !names.isEmpty || topLevel {
            var all = names
            for n in try helperNames(dir, m) where !all.contains(n) { all.append(n) }
            add(dir, cond, holds, all)
        }
    }

    for top in topDirectories {
        let m = try readDirectory(tree, top, options: options)
        for s in m.subdirs {
            try visit(top + "/" + s.name, s.cond, s.holds, topLevel: true)
        }
    }
    return rows.values.sorted { $0.path < $1.path }
}
