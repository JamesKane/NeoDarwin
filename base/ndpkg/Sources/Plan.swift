// SPDX-License-Identifier: BSD-2-Clause
//
// Repositories, the solver and plans (P2-02, packaging.md §5):
//
//   A repository is a directory of .ndpkg files and index.json, which
//   `ndpkg index DIR` writes after verifying each package. The solver
//   (libsolv, src/nd_solve.c) takes the installed set and the indexes'
//   packages; its transaction becomes a plan, JSON, which `ndpkg plan`
//   prints and `ndpkg apply` (or install, remove and upgrade directly)
//   executes.

import NDPkgShim

let repositoriesFile = "/etc/ndpkg/repositories"
let poolArch = "aarch64"

struct Available {
    var name: String
    var version: String
    var arch: String
    var file: String
    var manifestSHA256: String
    var provides: [String]
    var requires: [String]
    var conflicts: [String]
}

// The repositories: -r DIR options, else /etc/ndpkg/repositories (one
// directory per line; # comments).
func repositories(_ given: [String]) -> [String] {
    if !given.isEmpty { return given }
    guard let bytes = readFileIfPresent(repositoriesFile) else { return [] }
    return lines(text(bytes)).filter { !$0.hasPrefix("#") && !$0.isEmpty }
}

func availableJSON(_ a: Available) -> String {
    jsonObject([("name", json(a.name)), ("version", json(a.version)), ("arch", json(a.arch)),
                ("file", json(basename(a.file))), ("manifest-sha256", json(a.manifestSHA256)),
                ("provides", json(a.provides)), ("requires", json(a.requires)), ("conflicts", json(a.conflicts))])
}

// `ndpkg index DIR`: verifies every DIR/*.ndpkg and writes DIR/index.json.
func buildIndex(_ dir: String) {
    var entries: [String] = []
    for f in listDir(dir) where f.hasSuffix(".ndpkg") {
        let pkg = openPackage(dir + "/" + f)
        let m = pkg.manifest
        entries.append(availableJSON(Available(name: m.name, version: m.version, arch: m.arch, file: f,
                                               manifestSHA256: pkg.manifestSHA256, provides: m.provides,
                                               requires: m.requires, conflicts: m.conflicts)))
        print("ndpkg: index: \(m.name) \(m.version) (\(f))")
    }
    let body = "{\n  \"schema\": 1,\n  \"packages\": [\n" + entries.map { "    " + $0 }.joined(separator: ",\n") + "\n  ]\n}\n"
    writeAtomically(dir + "/index.json", body)
    print("ndpkg: \(dir)/index.json: \(entries.count) packages")
}

func readIndex(_ dir: String) -> [Available] {
    let path = dir + "/index.json"
    guard let bytes = readFileIfPresent(path) else { fail("\(path): no index (ndpkg index \(dir))") }
    let index = parseJSON(bytes, path)
    guard index["schema"]?.int == 1 else { fail("\(path): not schema 1") }
    var out: [Available] = []
    for p in index["packages"]?.array ?? [] {
        guard let name = p["name"]?.string, let version = p["version"]?.string, let arch = p["arch"]?.string,
              let file = p["file"]?.string, let sha = p["manifest-sha256"]?.string, !file.contains("/") else {
            fail("\(path): a package entry lacks name, version, arch, file or manifest-sha256")
        }
        out.append(Available(name: name, version: version, arch: arch, file: dir + "/" + file, manifestSHA256: sha,
                             provides: p["provides"]?.strings ?? [], requires: p["requires"]?.strings ?? [],
                             conflicts: p["conflicts"]?.strings ?? []))
    }
    return out
}

// -- plans -----------------------------------------------------------------------------

struct Action {
    var action: String        // install | upgrade | downgrade | remove
    var name: String
    var from: String?         // upgrade, downgrade: the installed version
    var version: String       // the version installed, or removed
    var arch: String?
    var file: String?         // install, upgrade, downgrade: the package
    var manifestSHA256: String?
    var reason: String?       // install: explicit | dependency
}

struct Plan {
    var command: String
    var arguments: [String]
    var generation: Int
    var actions: [Action]
}

func actionJSON(_ a: Action) -> String {
    var m: [(String, String)] = [("action", json(a.action)), ("name", json(a.name))]
    if let from = a.from { m.append(("from", json(from))) }
    m.append(("version", json(a.version)))
    if let arch = a.arch { m.append(("arch", json(arch))) }
    if let reason = a.reason { m.append(("reason", json(reason))) }
    if let file = a.file { m.append(("file", json(file))) }
    if let sha = a.manifestSHA256 { m.append(("manifest-sha256", json(sha))) }
    return jsonObject(m)
}

func planJSON(_ p: Plan) -> String {
    var s = "{\n  \"schema\": 1,\n  \"command\": \(json(p.command)),\n  \"arguments\": \(json(p.arguments)),\n"
    s += "  \"generation\": \(p.generation),\n  \"actions\": ["
    s += p.actions.isEmpty ? "]\n}" : "\n" + p.actions.map { "    " + actionJSON($0) }.joined(separator: ",\n") + "\n  ]\n}"
    return s
}

func parsePlan(_ path: String) -> Plan {
    let j = parseJSON(readFile(path), path)
    guard j["schema"]?.int == 1, let command = j["command"]?.string, let generation = j["generation"]?.int else {
        fail("\(path): not an ndpkg plan (schema 1)")
    }
    var actions: [Action] = []
    for a in j["actions"]?.array ?? [] {
        guard let action = a["action"]?.string, let name = a["name"]?.string, let version = a["version"]?.string else {
            fail("\(path): an action lacks action, name or version")
        }
        let act = Action(action: action, name: name, from: a["from"]?.string, version: version, arch: a["arch"]?.string,
                         file: a["file"]?.string, manifestSHA256: a["manifest-sha256"]?.string, reason: a["reason"]?.string)
        switch action {
        case "remove": break
        case "install", "upgrade", "downgrade":
            guard act.file != nil, act.manifestSHA256 != nil else { fail("\(path): \(action) \(name) lacks file or manifest-sha256") }
        default: fail("\(path): unknown action \(action)")
        }
        actions.append(act)
    }
    return Plan(command: command, arguments: j["arguments"]?.strings ?? [], generation: generation, actions: actions)
}

// -- solving ---------------------------------------------------------------------------

func solverLines(_ name: String, _ version: String, _ arch: String, _ key: String,
                 _ provides: [String], _ requires: [String], _ conflicts: [String]) -> String {
    var s = "pkg\t\(name)\t\(version)\t\(arch)\t\(key)\n"
    for d in provides { s += "dep\tprovides\t\(d)\n" }
    for d in requires { s += "dep\trequires\t\(d)\n" }
    for d in conflicts { s += "dep\tconflicts\t\(d)\n" }
    return s
}

// A job's argument: NAME, NAME=VERSION, or (install) a package file.
func jobDep(_ arg: String) -> String {
    let parts = arg.split(separator: "=", maxSplits: 1)
    return parts.count == 2 ? "\(parts[0]) = \(parts[1])" : arg
}

func isFileArgument(_ arg: String) -> Bool { arg.contains("/") || arg.hasSuffix(".ndpkg") }

func makePlan(_ command: String, _ args: [String], repos: [String]) -> Plan {
    let installed = readInstalled()
    var available: [Available] = []
    var jobs: [String] = []
    var explicit: Set<String> = []
    for dir in repositories(repos) { available += readIndex(dir) }
    for arg in args {
        if command == "install", isFileArgument(arg) {
            // A package file: verified now, offered to the solver at its version.
            let pkg = openPackage(arg)
            let m = pkg.manifest
            available.append(Available(name: m.name, version: m.version, arch: m.arch, file: arg,
                                       manifestSHA256: pkg.manifestSHA256, provides: m.provides,
                                       requires: m.requires, conflicts: m.conflicts))
            jobs.append("job\tinstall\t\(m.name) = \(m.version)\n")
            explicit.insert(m.name)
        } else {
            jobs.append("job\t\(command)\t\(jobDep(arg))\n")
            explicit.insert(String(arg.split(separator: "=").first ?? ""))
        }
    }
    if command == "upgrade", args.isEmpty { jobs.append("job\tupgrade\t*\n") }

    var input = "arch\t\(poolArch)\nrepo\tinstalled\n"
    for (i, p) in installed.enumerated() {
        let m = p.manifest
        input += solverLines(m.name, m.version, m.arch, "i\(i)", m.provides, m.requires, m.conflicts)
    }
    input += "repo\tavailable\n"
    for (i, a) in available.enumerated() {
        input += solverLines(a.name, a.version, a.arch, "a\(i)", a.provides, a.requires, a.conflicts)
    }
    input += jobs.joined()

    var output: UnsafeMutablePointer<CChar>? = nil
    let r = nd_pkg_solve(input, &output)
    let result = take(output) ?? ""
    if r != 0 {
        for line in lines(result) {
            let f = line.split(separator: "\t").map { String($0) }
            warn(f.dropFirst().joined(separator: ": "))
        }
        fail("\(command): no solution")
    }
    func inst(_ key: Substring) -> Installed { installed[Int(key.dropFirst())!] }
    func avail(_ key: Substring) -> Available { available[Int(key.dropFirst())!] }
    var actions: [Action] = []
    for line in lines(result) {
        let f = line.split(separator: "\t")
        switch f[0] {
        case "install":
            let a = avail(f[1])
            actions.append(Action(action: "install", name: a.name, from: nil, version: a.version, arch: a.arch, file: a.file,
                                  manifestSHA256: a.manifestSHA256, reason: explicit.contains(a.name) && command == "install" ? "explicit" : "dependency"))
        case "upgrade", "downgrade":
            let old = inst(f[1]), a = avail(f[2])
            actions.append(Action(action: String(f[0]), name: a.name, from: old.version, version: a.version, arch: a.arch,
                                  file: a.file, manifestSHA256: a.manifestSHA256, reason: nil))
        case "remove":
            let old = inst(f[1])
            actions.append(Action(action: "remove", name: old.name, from: nil, version: old.version, arch: nil, file: nil,
                                  manifestSHA256: nil, reason: nil))
        default:
            break
        }
    }
    return Plan(command: command, arguments: args, generation: currentGeneration(), actions: actions)
}

// -- applying --------------------------------------------------------------------------

func apply(_ plan: Plan) {
    let gen = currentGeneration()
    guard plan.generation == gen else {
        fail("the plan is for generation \(plan.generation) of the installed state, which is at \(gen): plan again")
    }
    if plan.actions.isEmpty {
        print("ndpkg: nothing to do")
        return
    }
    var installed: [String: Installed] = [:]
    for i in readInstalled() { installed[i.name] = i }

    // 1. Every new package verified and in the store, before anything changes.
    var entries: [Int: (Manifest, String)] = [:]
    for (n, a) in plan.actions.enumerated() where a.action != "remove" {
        let pkg = openPackage(a.file!)
        guard pkg.manifestSHA256 == a.manifestSHA256, pkg.manifest.name == a.name, pkg.manifest.version == a.version else {
            fail("\(a.file!): isn't the package the plan names (\(a.name) \(a.version))")
        }
        entries[n] = (pkg.manifest, storeAdd(pkg))
    }
    for a in plan.actions where a.action != "install" {
        guard let i = installed[a.name], i.version == (a.from ?? a.version) else {
            fail("the plan expects \(a.name) \(a.from ?? a.version) installed")
        }
    }

    // 2. File conflicts: a path another package keeps, or that isn't ours.
    var owner: [String: String] = [:]
    for i in installed.values { for f in i.manifest.files { owner[f.path] = i.name } }
    var leaving: Set<String> = []
    for a in plan.actions where a.action != "install" { leaving.insert(a.name) }
    var claimed: [String: String] = [:]
    for (n, a) in plan.actions.enumerated() where a.action != "remove" {
        for f in entries[n]!.0.files {
            if let o = owner[f.path], o != a.name, !leaving.contains(o) { fail("/\(f.path): \(a.name) conflicts with \(o)'s file") }
            if owner[f.path] == nil, !linkFree(a.name, f.path) { fail("/\(f.path): \(a.name) would replace a file no package owns") }
            if let c = claimed[f.path], c != a.name { fail("/\(f.path): in both \(c) and \(a.name)") }
            claimed[f.path] = a.name
        }
    }

    // 3. Trust caches, so the binaries run once they're linked.
    for (n, a) in plan.actions.enumerated() where a.action != "remove" {
        loadTrust(entries[n]!.1, "\(a.name) \(a.version)")
    }

    // 4. Activation, in the solver's order.
    for (n, a) in plan.actions.enumerated() {
        switch a.action {
        case "remove":
            let old = installed[a.name]!
            unlinkFiles(a.name, old.manifest.files.map { $0.path })
            _ = nd_pkg_unlink(activeDir + "/" + a.name)
            unmountEntry(old.entry)
            installed[a.name] = nil
            print("ndpkg: removed \(a.name) \(a.version)")
        case "install":
            let (m, entry) = entries[n]!
            mountEntry(entry)
            setActive(a.name, entry)
            linkFiles(a.name, m.files.map { $0.path })
            installed[a.name] = Installed(name: a.name, version: a.version, entry: entry, reason: a.reason ?? "explicit", manifest: m)
            print("ndpkg: installed \(a.name) \(a.version)")
        default:  // upgrade, downgrade: the farm's links stay; active/<name> moves
            let old = installed[a.name]!
            let (m, entry) = entries[n]!
            let oldPaths = Set(old.manifest.files.map { $0.path }), newPaths = m.files.map { $0.path }
            mountEntry(entry)
            linkFiles(a.name, newPaths.filter { !oldPaths.contains($0) })
            setActive(a.name, entry)
            let kept = Set(newPaths)
            unlinkFiles(a.name, old.manifest.files.map { $0.path }.filter { !kept.contains($0) })
            if old.entry != entry { unmountEntry(old.entry) }
            installed[a.name] = Installed(name: a.name, version: a.version, entry: entry, reason: old.reason, manifest: m)
            print("ndpkg: \(a.action)d \(a.name) \(a.from ?? "") -> \(a.version)")
        }
    }

    // 5. The new installed state, in one rename.
    let names = installed.keys.sorted()
    let newGen = writeInstalled(names.map { installed[$0]! })
    print("ndpkg: generation \(newGen): \(names.count) packages installed")
}

// `ndpkg activate`: the installed state made live again (after a boot, or
// to repair an interrupted transaction): trust caches, mounts, active
// links and the farm.
func activateAll() {
    for i in readInstalled() {
        loadTrust(i.entry, "\(i.name) \(i.version)")
        mountEntry(i.entry)
        setActive(i.name, i.entry)
        linkFiles(i.name, i.manifest.files.filter { linkFree(i.name, $0.path) }.map { $0.path })
        print("ndpkg: active: \(i.name) \(i.version)")
    }
}

// `ndpkg gc`: store entries and mounts no generation kept refers to.
func collectGarbage() {
    var keep: Set<String> = []
    for g in listDir(generationsDir) where Int(g) != nil {
        for name in listDir(generationsDir + "/" + g) {
            if let state = readFileIfPresent(generationsDir + "/" + g + "/" + name + "/state"), let e = value(text(state), "entry") {
                keep.insert(e)
            }
        }
    }
    let live = Set(readInstalled().map { $0.entry })
    for e in listDir(mntDir) where !live.contains(e) { unmountEntry(e) }
    for e in listDir(storeDir) where !keep.contains(e) {
        if nd_pkg_is_mountpoint(mountPoint(e)) { continue }
        check(nd_pkg_remove_tree(entryDir(e)), entryDir(e))
        print("ndpkg: gc: removed \(e)")
    }
}
