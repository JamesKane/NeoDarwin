// SPDX-License-Identifier: BSD-2-Clause
//
// The store, the installed state and activation (P2-02, packaging.md §5,
// §8):
//
//   /var/db/ndpkg/store/<payload-sha256>-<name>-<version>/
//       manifest.toml, manifest.sig, [trustcache, trustcache.grant,] files/…
//       A verified package, unpacked into a temporary directory, checked
//       on disk against the manifest, made read-only, renamed into place.
//   /var/db/ndpkg/mnt/<entry>/        the entry's files/ mounted (nullfs,
//       read-only, nosuid). XNU's nullfs is App Translocation's: the mount's
//       root holds one directory, d, which holds one entry, the lower
//       directory under its own name, so files/ is mnt/<entry>/d/files
//   /var/db/ndpkg/active/<name>       -> ../mnt/<entry>/d/files, swapped by rename
//   /usr/local/<path>                 -> /var/db/ndpkg/active/<name>/usr/local/<path>
//       the symlink farm, one link per payload file, the same across
//       versions of a package
//   /var/db/ndpkg/installed           -> generations/<N>, swapped by rename
//   /var/db/ndpkg/generations/<N>/<name>/{manifest,files,state}

import NDPkgShim

let dbDir = "/var/db/ndpkg"
let storeDir = dbDir + "/store"
let mntDir = dbDir + "/mnt"
let activeDir = dbDir + "/active"
let generationsDir = dbDir + "/generations"
let installedLink = dbDir + "/installed"
let prefixRelative = "usr/local"

// -- the store -------------------------------------------------------------------------

func entryDir(_ entry: String) -> String { storeDir + "/" + entry }

// Unpacks a verified package into the store; returns its entry's name. An
// entry is complete once it's in place, and identical content has the same
// name, so one already there is kept.
func storeAdd(_ pkg: Package) -> String {
    let m = pkg.manifest
    let entry = m.entry, final = entryDir(entry)
    if kind(final) == 2 {
        guard readFileIfPresent(final + "/manifest.toml") == pkg.manifestBytes else {
            fail("\(final): exists with another manifest")
        }
        return entry
    }
    mkdirs(storeDir)
    let tmp = storeDir + "/.tmp\(nd_pkg_pid())-" + entry
    check(nd_pkg_remove_tree(tmp), tmp)
    check(nd_pkg_mkdir(tmp, 0o755), tmp)
    writeNew(tmp + "/manifest.toml", pkg.manifestBytes, mode: 0o444)
    writeNew(tmp + "/manifest.sig", pkg.sig, mode: 0o444)
    if let module = pkg.module, let grant = pkg.grant {
        writeNew(tmp + "/trustcache", module, mode: 0o444)
        writeNew(tmp + "/trustcache.grant", grant, mode: 0o444)
    }
    var dirs: [String] = [tmp + "/files"]
    check(nd_pkg_mkdir(tmp + "/files", 0o755), tmp)
    for e in m.files {
        let path = tmp + "/files/" + e.path
        var d = dirname(path), missing: [String] = []
        while kind(d) == 0 { missing.append(d); d = dirname(d) }
        for d in missing.reversed() {
            check(nd_pkg_mkdir(d, 0o755), d)
            dirs.append(d)
        }
        switch pkg.payload[e.path]! {
        case .file(let data):
            // Read-only in the store: the manifest's mode without write bits.
            writeNew(path, data, mode: e.mode & ~0o222)
        case .link(let target):
            check(nd_pkg_symlink(target, path), path)
        }
    }
    // What's on disk is what the manifest lists.
    for e in m.files {
        let path = tmp + "/files/" + e.path
        if let link = e.link {
            guard readlink(path) == link else { fail("\(path): doesn't match the manifest after unpacking") }
        } else {
            guard let data = readFileIfPresent(path), data.count == e.size, sha256Hex(data) == e.sha256 else {
                fail("\(path): doesn't match the manifest after unpacking")
            }
        }
    }
    for d in dirs.reversed() { check(nd_pkg_chmod(d, 0o555), d) }
    check(nd_pkg_fsync_dir(tmp), tmp)
    check(nd_pkg_rename(tmp, final), final)
    check(nd_pkg_fsync_dir(storeDir), storeDir)
    return entry
}

// -- the installed state ---------------------------------------------------------------

struct Installed {
    var name: String
    var version: String
    var entry: String
    var reason: String      // explicit | dependency
    var manifest: Manifest
}

func currentGeneration() -> Int {
    guard let target = readlink(installedLink) else { return 0 }
    return Int(basename(target)) ?? 0
}

func readInstalled() -> [Installed] {
    let gen = currentGeneration()
    if gen == 0 { return [] }
    let dir = generationsDir + "/\(gen)"
    var out: [Installed] = []
    for name in listDir(dir) {
        let state = text(readFile(dir + "/" + name + "/state"))
        guard let version = value(state, "version"), let entry = value(state, "entry"), let reason = value(state, "reason") else {
            fail("\(dir)/\(name)/state: incomplete")
        }
        let m = parseManifest(text(readFile(dir + "/" + name + "/manifest")), dir + "/" + name + "/manifest")
        guard m.name == name, m.version == version, m.entry == entry else { fail("\(dir)/\(name): inconsistent") }
        out.append(Installed(name: name, version: version, entry: entry, reason: reason, manifest: m))
    }
    return out
}

// Writes the installed set as generation N+1 and switches to it with one
// rename; keeps generation N (the previous state) and drops older ones.
func writeInstalled(_ set: [Installed]) -> Int {
    let old = currentGeneration(), gen = old + 1
    mkdirs(generationsDir)
    let tmp = generationsDir + "/.tmp\(gen)"
    check(nd_pkg_remove_tree(tmp), tmp)
    check(nd_pkg_mkdir(tmp, 0o755), tmp)
    for i in set {
        let d = tmp + "/" + i.name
        check(nd_pkg_mkdir(d, 0o755), d)
        writeNew(d + "/manifest", Array(i.manifest.text.utf8), mode: 0o444)
        let files = i.manifest.files.map { "/" + $0.path + "\n" }.joined()
        writeNew(d + "/files", Array(files.utf8), mode: 0o444)
        let state = "entry = \"\(i.entry)\"\nname = \"\(i.name)\"\nreason = \"\(i.reason)\"\nversion = \"\(i.version)\"\n"
        writeNew(d + "/state", Array(state.utf8), mode: 0o444)
        check(nd_pkg_fsync_dir(d), d)
    }
    check(nd_pkg_fsync_dir(tmp), tmp)
    check(nd_pkg_rename(tmp, generationsDir + "/\(gen)"), generationsDir + "/\(gen)")
    setLink(installedLink, to: "generations/\(gen)")
    check(nd_pkg_fsync_dir(dbDir), dbDir)
    for g in listDir(generationsDir) {
        if let n = Int(g), n < old { _ = nd_pkg_remove_tree(generationsDir + "/" + g) }
    }
    return gen
}

// -- trust caches ----------------------------------------------------------------------

// Loads a store entry's trust cache, re-checking its manifest's signature
// and digests first: ndpkg hands the kernel only verified trust caches.
func loadTrust(_ entry: String, _ what: String) {
    let dir = entryDir(entry)
    let m = verifyManifest(readFile(dir + "/manifest.toml"), readFile(dir + "/manifest.sig"), dir)
    guard m.entry == entry else { fail("\(dir): the manifest names another entry") }
    let module = readFileIfPresent(dir + "/trustcache"), grant = readFileIfPresent(dir + "/trustcache.grant")
    verifyTrustCache(m, module: module, grant: grant, dir)
    // A package without Mach-O files has an empty trust cache: nothing to load.
    guard let module, let grant, value(m.text, "entries", section: "trust-cache") != "0" else { return }
    let r = module.withUnsafeBufferPointer { mb in
        grant.withUnsafeBufferPointer { gb in nd_pkg_load_trust_cache(mb.baseAddress!, mb.count, gb.baseAddress!, gb.count) }
    }
    switch r {
    case 0: print("ndpkg: \(what): trust cache loaded")
    case EEXIST: return
    case EAUTH: fail("\(what): the kernel refused the trust cache's grant (EAUTH)")
    case EPERM: fail("\(what): not permitted (root and the load-trust-cache entitlement are required) (EPERM)")
    default: fail("\(what): the kernel refused the trust cache: \(errnoText(r))")
    }
}

// -- activation ------------------------------------------------------------------------

func mountPoint(_ entry: String) -> String { mntDir + "/" + entry }
func linkTarget(_ name: String, _ path: String) -> String { activeDir + "/" + name + "/" + path }

// Mounts a store entry's files/ at mnt/<entry> (nullfs), once.
func mountEntry(_ entry: String) {
    let mp = mountPoint(entry)
    mkdirs(mp)
    if nd_pkg_is_mountpoint(mp) { return }
    check(nd_pkg_nullfs_mount(entryDir(entry) + "/files", mp), "mount nullfs \(entryDir(entry))/files on \(mp)")
}

func unmountEntry(_ entry: String) {
    let mp = mountPoint(entry)
    if nd_pkg_is_mountpoint(mp) {
        let e = nd_pkg_unmount(mp)
        if e != 0 {
            warn("unmount \(mp): \(errnoText(e)) (left mounted; ndpkg gc retries)")
            return
        }
    }
    _ = nd_pkg_rmdir(mp)
}

func setActive(_ name: String, _ entry: String) {
    mkdirs(activeDir)
    setLink(activeDir + "/" + name, to: "../mnt/" + entry + "/d/files")
}

// The farm: /usr/local/<path> -> /var/db/ndpkg/active/<name>/usr/local/<path>.
func linkFiles(_ name: String, _ paths: [String]) {
    for p in paths {
        let live = "/" + p, target = linkTarget(name, p)
        mkdirs(dirname(live))
        if kind(live) == 3, readlink(live) == target { continue }
        check(nd_pkg_symlink(target, live), live)
    }
}

func unlinkFiles(_ name: String, _ paths: [String]) {
    for p in paths {
        let live = "/" + p
        guard readlink(live) == linkTarget(name, p) else { continue }
        check(nd_pkg_unlink(live), live)
        // Empty directories the package's files made, below /usr/local/<dir>.
        var d = dirname(live)
        while d.split(separator: "/").count > 3, nd_pkg_rmdir(d) == 0 { d = dirname(d) }
    }
}

// Whether `path` in the live prefix may be linked for package `name`:
// nothing there, or its own link already.
func linkFree(_ name: String, _ path: String) -> Bool {
    let live = "/" + path
    return kind(live) == 0 || readlink(live) == linkTarget(name, path)
}
