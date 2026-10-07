// SPDX-License-Identifier: BSD-2-Clause
//
// ndpkg system (P2-03, docs/architecture/packaging.md §6.1): system sets as
// ZFS boot environments, over ndbectl (filesystems.md §8.5).
//
// A system set is a signed package of kind `system-set` whose `requires`
// pin its components exactly (`name = version`): a `kernel-collection`
// package (System/Library/Kernels/kernelcache and the static trust cache
// that goes with it) and `system` packages (the userland base). They're
// installed into a boot environment, not activated from the store; each BE
// keeps their receipts (manifest.toml, manifest.sig, trustcache,
// trustcache.grant) in /System/Library/Receipts/ndpkg/<name>/, so a BE
// knows its own set and rolls back with it.
//
// neoboot reads kernels from the ESP, not from ZFS (P3-06), and userland
// has no msdosfs (P3-04), so the ESP is derived state, written whole: its
// mirror, /var/db/ndpkg/system/esp on the shared ndpool/pkg dataset
// (EFI/BOOT/BOOTAA64.EFI, NeoDarwin/boot.cfg with be=<default BE>,
// NeoDarwin/be/<BE>/{kernelcache,trustcache}, NeoDarwin/bootonce), becomes
// a FAT image (makefs -t msdos) written over the ESP partition of the pool's
// disk. neoboot deletes \NeoDarwin\bootonce as it boots it, so the try of an
// upgrade is one-shot (BootEnvironment.swift).

import NDPkgShim

let systemKinds = ["system-set", "kernel-collection", "system"]
let receiptsRelative = "System/Library/Receipts/ndpkg"
let kernelsRelative = "System/Library/Kernels"
let systemDir = dbDir + "/system"
let espStage = systemDir + "/esp"
let espBootConfig = espStage + "/NeoDarwin/boot.cfg"
let espBootOnce = espStage + "/NeoDarwin/bootonce"
let systemStateFile = systemDir + "/state"
let systemSetsDir = systemDir + "/sets"
let beMountPoint = "/private/var/run/ndpkg-system-be"
let espImage = "/private/var/run/ndpkg-esp.img"
let ndbectlCommand = "/sbin/ndbectl"
let zfsCommand = "/sbin/zfs"
let zpoolCommand = "/sbin/zpool"
let makefsCommand = "/usr/sbin/makefs"

// -- running tools ---------------------------------------------------------------------

func runTool(_ argv: [String], capture: Bool = false) -> (status: Int32, output: String) {
    let c: [UnsafePointer<CChar>?] = argv.map { UnsafePointer(strdup($0)) } + [nil]
    defer { for p in c { free(UnsafeMutablePointer(mutating: p)) } }
    var out: UnsafeMutablePointer<CChar>? = nil
    let status = c.withUnsafeBufferPointer { b -> Int32 in
        if capture { return nd_pkg_run(b.baseAddress!, &out) }
        return nd_pkg_run(b.baseAddress!, nil)
    }
    if status < 0 { fail("\(argv[0]): \(errnoText(nd_pkg_errno()))") }
    return (status, take(out) ?? "")
}

func must(_ argv: [String]) {
    let s = runTool(argv).status
    guard s == 0 else { fail("\(argv.joined(separator: " ")): exit \(s)") }
}

// -- the booted BE and the store's dataset -----------------------------------------------

/// The booted BE: the pool and the BE's name, from the root's dataset.
func bootedBE() -> (pool: String, be: String) {
    guard let ds = take(nd_pkg_root_dataset()) else { fail("the root is not on ZFS: system sets need boot environments") }
    let f = ds.split(separator: "/").map { String($0) }
    guard f.count == 3, f[1] == "ROOT" else { fail("the root, \(ds), is not a boot environment (POOL/ROOT/BE)") }
    return (f[0], f[2])
}

/// POOL/pkg, the store's dataset (§5.2), mounted at /var/db/ndpkg if the root
/// is on ZFS and nothing mounted it yet; shared by every boot environment.
func mountPackageDataset() {
    guard let ds = take(nd_pkg_root_dataset()), !nd_pkg_is_mountpoint(dbDir) else { return }
    let pkg = String(ds.split(separator: "/")[0]) + "/pkg"
    guard runTool([zfsCommand, "list", "-H", "-o", "name", pkg], capture: true).status == 0 else { return }
    if runTool([zfsCommand, "mount", pkg]).status != 0 && !nd_pkg_is_mountpoint(dbDir) {
        fail("cannot mount \(pkg) at \(dbDir)")
    }
}

// -- receipts ----------------------------------------------------------------------------

struct Receipt {
    var manifest: Manifest
    var dir: String
}

/// The system packages installed in the tree at `root` ("" for /), each
/// receipt's manifest verified against its signature again.
func readReceipts(_ root: String) -> [Receipt] {
    let dir = root + "/" + receiptsRelative
    return listDir(dir).filter { !$0.hasPrefix(".") }.map { name in
        let d = dir + "/" + name
        let m = verifyManifest(readFile(d + "/manifest.toml"), readFile(d + "/manifest.sig"), d)
        guard m.name == name else { fail("\(d): the receipt is for \(m.name)") }
        return Receipt(manifest: m, dir: d)
    }
}

/// Installs a verified system package into the tree at `root`: its files in
/// place (each written beside and renamed over), the files `old` had and it
/// hasn't removed, and its receipt.
func installSystemPackage(_ pkg: Package, into root: String, replacing old: Receipt?) {
    let m = pkg.manifest
    var paths: Set<String> = []
    for e in m.files {
        paths.insert(e.path)
        let dst = root + "/" + e.path
        mkdirs(dirname(dst))
        let tmp = dirname(dst) + "/." + basename(dst) + ".ndpkg\(nd_pkg_pid())"
        _ = nd_pkg_unlink(tmp)
        switch pkg.payload[e.path]! {
        case .file(let data): writeNew(tmp, data, mode: e.mode)
        case .link(let target): check(nd_pkg_symlink(target, tmp), tmp)
        }
        check(nd_pkg_rename(tmp, dst), dst)
    }
    if let old {
        for e in old.manifest.files where !paths.contains(e.path) { _ = nd_pkg_unlink(root + "/" + e.path) }
    }
    let dir = root + "/" + receiptsRelative + "/" + m.name
    let tmp = root + "/" + receiptsRelative + "/.new-" + m.name
    mkdirs(dirname(dir))
    check(nd_pkg_remove_tree(tmp), tmp)
    check(nd_pkg_mkdir(tmp, 0o755), tmp)
    writeNew(tmp + "/manifest.toml", pkg.manifestBytes, mode: 0o444)
    writeNew(tmp + "/manifest.sig", pkg.sig, mode: 0o444)
    if let module = pkg.module, let grant = pkg.grant {
        writeNew(tmp + "/trustcache", module, mode: 0o444)
        writeNew(tmp + "/trustcache.grant", grant, mode: 0o444)
    }
    check(nd_pkg_remove_tree(dir), dir)
    check(nd_pkg_rename(tmp, dir), dir)
    print("ndpkg: system: \(m.name) \(old.map { $0.manifest.version + " -> " } ?? "")\(m.version) (\(m.kind))")
}

func removeSystemPackage(_ r: Receipt, from root: String) {
    for e in r.manifest.files { _ = nd_pkg_unlink(root + "/" + e.path) }
    check(nd_pkg_remove_tree(r.dir), r.dir)
    print("ndpkg: system: \(r.manifest.name) \(r.manifest.version) removed")
}

// -- state -------------------------------------------------------------------------------

/// /var/db/ndpkg/system/state: `previous` (the BE the last change replaced as
/// the default, for rollback) and `try` (a BE an upgrade set to boot once).
func readSystemState() -> [String: String] {
    var st: [String: String] = [:]
    guard let bytes = readFileIfPresent(systemStateFile) else { return st }
    let t = text(bytes)
    for key in ["previous", "try"] { if let v = value(t, key) { st[key] = v } }
    return st
}

func writeSystemState(_ st: [String: String]) {
    var s = ""
    for key in ["previous", "try"] { if let v = st[key] { s += "\(key) = \"\(v)\"\n" } }
    writeAtomically(systemStateFile, s)
}

/// The ESP's default BE: be= in the mirror's boot.cfg.
func defaultBE() -> String? {
    guard let bytes = readFileIfPresent(espBootConfig) else { return nil }
    for w in text(bytes).split(whereSeparator: { $0 == " " || $0 == "\n" }) where w.hasPrefix("be=") {
        return String(w.dropFirst(3))
    }
    return nil
}

func setDefaultBE(_ be: String) {
    let words = text(readFile(espBootConfig)).split(whereSeparator: { $0 == " " || $0 == "\n" })
        .filter { !$0.hasPrefix("be=") }.map { String($0) }
    writeAtomically(espBootConfig, (words + ["be=" + be]).joined(separator: " ") + "\n")
}

/// The system set a BE was made with ("NAME VERSION"), as upgrade recorded.
func setOf(_ be: String) -> String {
    guard let b = readFileIfPresent(systemSetsDir + "/" + be) else { return "-" }
    return lines(text(b)).first ?? "-"
}

// -- the ESP -----------------------------------------------------------------------------

/// The ESP: the EFI System Partition of the disk whose partition holds the
/// pool (`zpool list -vHP`: /dev/diskNsM, or the port's by-id name
/// /var/run/disk/by-id/media-<partition GUID>, found in the disks' GPTs).
func espDevice(_ pool: String) -> String {
    let r = runTool([zpoolCommand, "list", "-vHP", pool], capture: true)
    var out = [CChar](repeating: 0, count: 64)
    for line in lines(r.output) {
        for f in line.split(whereSeparator: { $0 == " " || $0 == "\t" }) where f.hasPrefix("/") {
            if nd_pkg_find_esp(String(f), &out, out.count) == 0 { return string(out) }
        }
    }
    fail("no EFI System Partition beside pool \(pool)'s vdevs in `zpool list -vHP`:\n\(r.output)")
}

/// Writes the ESP from its mirror: a FAT image of the mirror, the
/// partition's size, written over it.
func writeESP(_ pool: String) {
    let dev = espDevice(pool)
    let size = nd_pkg_device_size(dev)
    guard size > 0 else { fail("\(dev): no size") }
    _ = nd_pkg_unlink(espImage)
    must([makefsCommand, "-t", "msdos", "-o", "volume_label=NEODARWIN", "-s", "\(size)", espImage, espStage])
    let e = nd_pkg_write_esp(espImage, dev)
    _ = nd_pkg_unlink(espImage)
    guard e == 0 else { fail("writing the ESP \(dev): \(e == EINVAL ? "not a FAT partition of the image's size" : errnoText(e))") }
    print("ndpkg: system: the ESP (\(dev)) written from \(espStage)")
}

// -- commands ----------------------------------------------------------------------------

func versionCompare(_ a: String, _ b: String) -> Int {
    let x = a.split(separator: "."), y = b.split(separator: ".")
    for i in 0..<max(x.count, y.count) {
        let p = i < x.count ? String(x[i]) : "", q = i < y.count ? String(y[i]) : ""
        if let m = Int(p), let n = Int(q) {
            if m != n { return m < n ? -1 : 1 }
        } else if p != q {
            return p < q ? -1 : 1
        }
    }
    return 0
}

func openSystemPackage(_ a: Available, kind: String?) -> Package {
    let pkg = openPackage(a.file)
    let m = pkg.manifest
    guard pkg.manifestSHA256 == a.manifestSHA256, m.name == a.name, m.version == a.version else {
        fail("\(a.file): isn't the package the index names (\(a.name) \(a.version))")
    }
    guard systemKinds.contains(m.kind), kind == nil || m.kind == kind!, m.kind != "system-set" || kind != nil else {
        fail("\(a.file): \(m.name) is \(m.kind), not \(kind ?? "a system set's component")")
    }
    return pkg
}

/// `ndpkg system upgrade [VERSION]`: the newest system set (or VERSION) of
/// the booted one's name, installed into a new BE that boots once.
func systemUpgrade(_ repos: [String], _ wanted: String?) {
    let (pool, booted) = bootedBE()
    guard let set = readReceipts("").first(where: { $0.manifest.kind == "system-set" })?.manifest else {
        fail("the booted boot environment has no system set (/\(receiptsRelative))")
    }
    var avail: [Available] = []
    for r in repositories(repos) { avail += readIndex(r) }
    let candidates = avail.filter { $0.name == set.name && (wanted == nil || $0.version == wanted!) }
    guard var best = candidates.first else { fail("\(set.name)\(wanted.map { " " + $0 } ?? ""): not in the repositories") }
    for c in candidates where versionCompare(c.version, best.version) > 0 { best = c }
    if versionCompare(best.version, set.version) == 0 || (wanted == nil && versionCompare(best.version, set.version) < 0) {
        print("ndpkg: system: \(set.name) \(set.version) is booted; nothing to upgrade")
        return
    }
    // Everything verified before anything changes.
    let setPkg = openSystemPackage(best, kind: "system-set")
    var pkgs = [setPkg]
    for dep in setPkg.manifest.requires {
        let f = dep.split(separator: " ").map { String($0) }
        guard f.count == 3, f[1] == "=" || f[1] == "==" else { fail("\(set.name) \(best.version): \(dep): a system set pins exact versions") }
        guard let a = avail.first(where: { $0.name == f[0] && $0.version == f[2] }) else { fail("\(dep): not in the repositories") }
        pkgs.append(openSystemPackage(a, kind: nil))
    }
    let kernels = pkgs.filter { $0.manifest.kind == "kernel-collection" }
    guard kernels.count == 1, case .file(let kc) = kernels[0].payload[kernelsRelative + "/kernelcache"] ?? .link("") else {
        fail("\(set.name) \(best.version): needs one kernel-collection with /\(kernelsRelative)/kernelcache")
    }
    var tc: [UInt8]? = nil
    if case .file(let t) = kernels[0].payload[kernelsRelative + "/trustcache"] ?? .link("") { tc = t }
    print("ndpkg: system: \(set.name) \(set.version) -> \(best.version): \(pkgs.count) packages verified")

    // A new BE from the booted one, the set installed into it.
    let be = "\(set.name)-\(best.version)"
    must([ndbectlCommand, "-p", pool, "create", be])
    mkdirs(beMountPoint)
    must([ndbectlCommand, "-p", pool, "mount", be, beMountPoint])
    let old = readReceipts(beMountPoint)
    for p in pkgs {
        installSystemPackage(p, into: beMountPoint, replacing: old.first { $0.manifest.name == p.manifest.name })
    }
    for r in old where !pkgs.contains(where: { $0.manifest.name == r.manifest.name }) {
        removeSystemPackage(r, from: beMountPoint)
    }
    must([ndbectlCommand, "-p", pool, "umount", be])

    // Its kernel on the ESP, and one try.
    let dir = espStage + "/NeoDarwin/be/" + be
    check(nd_pkg_remove_tree(dir), dir)
    mkdirs(dir)
    writeNew(dir + "/kernelcache", kc, mode: 0o644)
    if let tc { writeNew(dir + "/trustcache", tc, mode: 0o644) }
    mkdirs(systemSetsDir)
    writeAtomically(systemSetsDir + "/" + be, "\(set.name) \(best.version)\n")
    writeAtomically(espBootOnce, be + "\n")
    var st = readSystemState()
    st["try"] = be
    writeSystemState(st)
    writeESP(pool)
    print("ndpkg: system: \(be) boots once at the next boot (then \(defaultBE() ?? booted) again until `ndpkg system confirm`)")
}

/// `ndpkg system confirm`, run by launchd at every boot (ndpkg boot): a BE
/// on its try that got this far becomes the default.
func systemConfirm() {
    let (pool, booted) = bootedBE()
    var st = readSystemState()
    guard let tried = st["try"] else {
        print("ndpkg: system: booted \(booted); no try to confirm")
        return
    }
    st["try"] = nil
    _ = nd_pkg_unlink(espBootOnce)  // neoboot deleted the ESP's as it booted it
    guard tried == booted else {
        writeSystemState(st)
        print("ndpkg: system: the try of \(tried) failed: booted \(booted), the default")
        return
    }
    let previous = defaultBE()
    must([ndbectlCommand, "-p", pool, "activate", booted])
    setDefaultBE(booted)
    if let previous { st["previous"] = previous }
    writeSystemState(st)
    writeESP(pool)
    print("ndpkg: system: \(booted) confirmed: the default from now on (previous: \(previous ?? "-"))")
}

/// `ndpkg system rollback`: the BE the last change replaced is the default again.
func systemRollback() {
    let (pool, _) = bootedBE()
    var st = readSystemState()
    guard let previous = st["previous"] else { fail("no previous boot environment to roll back to") }
    let current = defaultBE()
    must([ndbectlCommand, "-p", pool, "activate", previous])
    setDefaultBE(previous)
    st["previous"] = current
    st["try"] = nil
    _ = nd_pkg_unlink(espBootOnce)
    writeSystemState(st)
    writeESP(pool)
    print("ndpkg: system: rolled back: \(previous) (\(setOf(previous))) boots from the next boot on")
}

func systemStatus(_ asJSON: Bool) {
    let (pool, booted) = bootedBE()
    let st = readSystemState()
    let set = readReceipts("").first { $0.manifest.kind == "system-set" }.map { "\($0.manifest.name) \($0.manifest.version)" } ?? "-"
    if asJSON {
        print(jsonObject([("pool", json(pool)), ("booted", json(booted)), ("system-set", json(set)),
                          ("default", json(defaultBE() ?? "-")), ("try", json(st["try"] ?? "-")),
                          ("previous", json(st["previous"] ?? "-"))]))
        return
    }
    print("booted: \(booted) (\(set))\ndefault: \(defaultBE() ?? "-")\ntry: \(st["try"] ?? "-")\nprevious: \(st["previous"] ?? "-")")
    for r in readReceipts("") { print("package: \(r.manifest.name) \(r.manifest.version) (\(r.manifest.kind))") }
}

/// `ndpkg system list`: each BE, N booted, D the ESP's default, T its pending
/// try, P the rollback target, and the set it was made with.
func systemList() {
    let (pool, booted) = bootedBE()
    let st = readSystemState(), def = defaultBE()
    let r = runTool([zfsCommand, "list", "-H", "-d", "1", "-t", "filesystem", "-o", "name", pool + "/ROOT"], capture: true)
    print("BE Flags System-set")
    for line in lines(r.output) where line.hasPrefix(pool + "/ROOT/") {
        let be = String(line.dropFirst(pool.count + 6))
        var flags = ""
        if be == booted { flags += "N" }
        if be == def { flags += "D" }
        if be == st["try"] { flags += "T" }
        if be == st["previous"] { flags += "P" }
        print("\(be) \(flags.isEmpty ? "-" : flags) \(setOf(be))")
    }
}

/// `ndpkg boot`, launchd's job at every boot: the store's dataset, the
/// booted BE's system packages' trust caches, the packages (`activate`),
/// then `system confirm`.
func bootJob() {
    for r in readReceipts("") {
        guard let module = readFileIfPresent(r.dir + "/trustcache"), let grant = readFileIfPresent(r.dir + "/trustcache.grant") else { continue }
        verifyTrustCache(r.manifest, module: module, grant: grant, r.dir)
        // A package without Mach-O files has an empty trust cache: nothing to load.
        if value(r.manifest.text, "entries", section: "trust-cache") == "0" { continue }
        let e = module.withUnsafeBufferPointer { m in
            grant.withUnsafeBufferPointer { g in nd_pkg_load_trust_cache(m.baseAddress!, m.count, g.baseAddress!, g.count) }
        }
        print("ndpkg: boot: \(r.manifest.name) \(r.manifest.version): trust cache \(e == 0 ? "loaded" : e == EEXIST ? "already loaded" : "refused: " + errnoText(e))")
    }
    activateAll()
    if take(nd_pkg_root_dataset()) != nil && exists(systemDir) { systemConfirm() }
}
