// SPDX-License-Identifier: BSD-2-Clause
//
// ndpkg: NeoDarwin's package manager (docs/architecture/packaging.md §5,
// §8). Embedded Swift (language policy T1 tool, built as T3) over NDPkgShim.
//
//     ndpkg [-r REPO]... install NAME|NAME=VERSION|FILE.ndpkg...
//     ndpkg [-r REPO]... remove NAME...
//     ndpkg [-r REPO]... upgrade [NAME...]
//         Solve (libsolv), then apply the plan.
//     ndpkg [-r REPO]... plan install|remove|upgrade ARGS...
//         Print the plan as JSON without changing anything.
//     ndpkg apply PLAN.json
//         Apply a plan `ndpkg plan` printed, if the installed state hasn't
//         changed since.
//     ndpkg list [--json]
//     ndpkg info [--json] NAME
//     ndpkg index DIR
//         Verify DIR/*.ndpkg and write DIR/index.json, a local repository.
//     ndpkg activate
//         Make the installed state live: trust caches, mounts, links.
//     ndpkg gc
//         Remove store entries no kept generation refers to.
//     ndpkg activate-trust PKG.ndpkg
//         (P2-01) Verify a package's manifest and trust cache and load the
//         trust cache.
//     ndpkg load-trust MODULE GRANT
//         (P2-01) Hand a trust-cache module and grant to the kernel unchecked.
//
// REPO directories default to the lines of /etc/ndpkg/repositories. Every
// package is verified against the package roots the kernel trusts (boot-arg
// nd_pkg_root) before it enters the store, and its trust cache is loaded
// only from a verified store entry; the loads need root and the
// entitlement com.apple.private.pmap.load-trust-cache =
// neodarwin.trust-cache.load, the nullfs mounts com.apple.private.nullfs_allow.

import NDPkgShim

func activateTrust(_ path: String) {
    let pkg = openPackage(path)
    let m = pkg.manifest
    guard let module = pkg.module, let grant = pkg.grant else { fail("\(path): \(m.name) has no trust cache") }
    print("ndpkg: \(m.name) \(m.version): manifest verified")
    loadModule(module, grant, "\(m.name) \(m.version)")
}

func loadModule(_ module: [UInt8], _ grant: [UInt8], _ what: String) {
    let r = module.withUnsafeBufferPointer { m in
        grant.withUnsafeBufferPointer { g in nd_pkg_load_trust_cache(m.baseAddress!, m.count, g.baseAddress!, g.count) }
    }
    switch r {
    case 0: print("ndpkg: \(what): trust cache loaded")
    case EEXIST: print("ndpkg: \(what): trust cache already loaded")
    case EAUTH: fail("\(what): the kernel refused the trust cache's grant (EAUTH)")
    case EPERM: fail("\(what): not permitted (root and the load-trust-cache entitlement are required) (EPERM)")
    default: fail("\(what): the kernel refused the trust cache: \(errnoText(r))")
    }
}

// Commands that change the state take the lock and need root.
func lockState() {
    guard nd_pkg_is_root() else { fail("must be run as root") }
    mkdirs(dbDir)
    let e = nd_pkg_lock(dbDir + "/lock")
    if e == EWOULDBLOCK { fail("another ndpkg holds \(dbDir)/lock") }
    check(e, dbDir + "/lock")
}

func listInstalled(_ asJSON: Bool) {
    let set = readInstalled()
    if asJSON {
        let items = set.map { jsonObject([("name", json($0.name)), ("version", json($0.version)), ("reason", json($0.reason)), ("entry", json($0.entry))]) }
        print("[" + items.joined(separator: ", ") + "]")
    } else {
        for i in set { print("\(i.name) \(i.version)") }
    }
}

func info(_ name: String, _ asJSON: Bool) {
    guard let i = readInstalled().first(where: { $0.name == name }) else { fail("\(name): not installed") }
    let m = i.manifest
    if asJSON {
        print(jsonObject([("name", json(m.name)), ("version", json(m.version)), ("arch", json(m.arch)), ("kind", json(m.kind)),
                          ("license", json(m.license)), ("reason", json(i.reason)), ("entry", json(i.entry)),
                          ("provides", json(m.provides)), ("requires", json(m.requires)), ("conflicts", json(m.conflicts)),
                          ("files", json(m.files.map { "/" + $0.path })), ("trust-cache", m.tcModule == nil ? "false" : "true")]))
        return
    }
    print("name: \(m.name)\nversion: \(m.version)\narch: \(m.arch)\nkind: \(m.kind)\nlicense: \(m.license)")
    print("reason: \(i.reason)\nentry: \(storeDir)/\(i.entry)")
    print("provides: \(m.provides.joined(separator: ", "))\nrequires: \(m.requires.joined(separator: ", "))")
    print("conflicts: \(m.conflicts.joined(separator: ", "))\ntrust cache: \(m.tcModule == nil ? "no" : "yes")")
    for f in m.files { print("file: /\(f.path)") }
}

func usage() -> Never {
    nd_pkg_warn("""
        usage: ndpkg [-r REPO]... install|remove|upgrade ARGS...
               ndpkg [-r REPO]... plan install|remove|upgrade ARGS...
               ndpkg apply PLAN.json
               ndpkg list [--json] | info [--json] NAME
               ndpkg index DIR | activate | gc
               ndpkg activate-trust PKG.ndpkg | load-trust MODULE GRANT
               ndpkg [-r REPO]... system upgrade [VERSION]
               ndpkg system status [--json] | list | confirm | rollback
               ndpkg boot
        """)
    exit(2)
}

@_cdecl("main")
func ndpkgMain(_ argc: Int32, _ argv: UnsafeMutablePointer<UnsafeMutablePointer<CChar>?>) -> Int32 {
    var args: [String] = []
    for i in 1..<Int(argc) {
        if let p = argv[i] { args.append(String(cString: p)) }
    }
    var repos: [String] = []
    while args.count >= 2, args[0] == "-r" {
        repos.append(args[1])
        args.removeFirst(2)
    }
    let asJSON = args.contains("--json")
    args = args.filter { $0 != "--json" }
    guard let command = args.first else { usage() }
    let rest = Array(args.dropFirst())
    // On a root on ZFS, the store is POOL/pkg (§5.2), shared by every BE.
    if nd_pkg_is_root() { mountPackageDataset() }
    switch command {
    case "system":
        guard let sub = rest.first else { usage() }
        switch sub {
        case "status" where rest.count == 1: systemStatus(asJSON)
        case "list" where rest.count == 1: systemList()
        case "upgrade" where rest.count <= 2:
            lockState()
            systemUpgrade(repos, rest.count == 2 ? rest[1] : nil)
        case "confirm" where rest.count == 1:
            lockState()
            systemConfirm()
        case "rollback" where rest.count == 1:
            lockState()
            systemRollback()
        default: usage()
        }
    case "boot":
        lockState()
        bootJob()
    case "install", "remove", "upgrade":
        if command != "upgrade", rest.isEmpty { usage() }
        lockState()
        let plan = makePlan(command, rest, repos: repos)
        if asJSON { print(planJSON(plan)) }
        apply(plan)
    case "plan":
        guard let sub = rest.first, ["install", "remove", "upgrade"].contains(sub) else { usage() }
        if sub != "upgrade", rest.count < 2 { usage() }
        print(planJSON(makePlan(sub, Array(rest.dropFirst()), repos: repos)))
    case "apply":
        guard rest.count == 1 else { usage() }
        lockState()
        apply(parsePlan(rest[0]))
    case "list":
        listInstalled(asJSON)
    case "info":
        guard rest.count == 1 else { usage() }
        info(rest[0], asJSON)
    case "index":
        guard rest.count == 1 else { usage() }
        buildIndex(rest[0])
    case "activate":
        lockState()
        activateAll()
    case "gc":
        lockState()
        collectGarbage()
    case "activate-trust":
        guard rest.count == 1 else { usage() }
        activateTrust(rest[0])
    case "load-trust":
        guard rest.count == 2 else { usage() }
        loadModule(readFile(rest[0]), readFile(rest[1]), rest[0])
    default:
        usage()
    }
    return 0
}
