// SPDX-License-Identifier: BSD-2-Clause
//
// ndbectl: boot environments on a root-on-ZFS pool (P3-03,
// docs/architecture/filesystems.md §8), after FreeBSD's bectl(8):
//
//   ndbectl list                     each BE, its flags and its origin
//   ndbectl create NEW [ORIGIN]      snapshot ORIGIN (default: the booted BE)
//                                    and clone it as POOL/ROOT/NEW
//   ndbectl activate [-t] BE         boot BE from now on (the pool's bootfs);
//                                    -t: boot it once (org.neodarwin:bootonce,
//                                    which zfs.kext clears as it boots it)
//   ndbectl mount BE DIR             mount a BE that isn't booted at DIR
//   ndbectl umount BE                unmount it again
//   ndbectl destroy BE               destroy a BE that is neither booted nor
//                                    active, and its origin snapshot
//
// The pool is the booted root's (the root's statfs f_mntfromname, "POOL/ROOT/BE")
// unless -p POOL comes first. BEs are POOL/ROOT/<name>, mountpoint=/ and
// canmount=noauto, as bectl makes them; the kernel mounts the chosen one as
// the root (rd=zfs:POOL in boot.cfg; zfs.kext takes bootonce, then a dataset
// rd= names, then bootfs). `ndpkg system` (P2-03) wraps these operations.
//
// Embedded Swift (language policy T3): NeoDarwin has no Swift runtime yet,
// so the tool stands on libSystem alone, like launchctl.

import Shim

let bootOnceProperty = "org.neodarwin:bootonce"
let zfsCommand = "/sbin/zfs"
let zpoolCommand = "/sbin/zpool"

@_cdecl("main")
func ndbectlMain(_ argc: Int32, _ argv: UnsafeMutablePointer<UnsafeMutablePointer<CChar>?>) -> Int32 {
    var args: [String] = []
    for i in 1..<Int(argc) {
        if let p = argv[i] { args.append(String(cString: p)) }
    }
    var pool: String? = nil
    if args.count >= 2 && args[0] == "-p" {
        pool = args[1]
        args.removeFirst(2)
    }
    guard let command = args.first else { usage() }
    let rest = Array(args.dropFirst())
    let be = BootEnvironments(pool: pool ?? bootedPool())
    switch command {
    case "list": return be.list()
    case "create" where rest.count == 1 || rest.count == 2:
        return be.create(rest[0], from: rest.count == 2 ? rest[1] : nil)
    case "activate" where rest.count == 1: return be.activate(rest[0], once: false)
    case "activate" where rest.count == 2 && rest[0] == "-t": return be.activate(rest[1], once: true)
    case "mount" where rest.count == 2: return be.mount(rest[0], at: rest[1])
    case "umount" where rest.count == 1, "unmount" where rest.count == 1: return be.unmount(rest[0])
    case "destroy" where rest.count == 1: return be.destroy(rest[0])
    default: usage()
    }
}

func usage() -> Never {
    print("usage: ndbectl [-p POOL] list | create NEW [ORIGIN] | activate [-t] BE | mount BE DIR | umount BE | destroy BE")
    exit(2)
}

func fail(_ message: String) -> Never {
    print("ndbectl: \(message)")
    exit(1)
}

/// The root's dataset if the root is on ZFS ("POOL/ROOT/BE"), else nil.
func bootedDataset() -> String? {
    var fs = statfs()
    guard statfs("/", &fs) == 0 else { return nil }
    let type = withUnsafeBytes(of: fs.f_fstypename) { cString($0) }
    guard type == "zfs" else { return nil }
    return withUnsafeBytes(of: fs.f_mntfromname) { cString($0) }
}

func bootedPool() -> String {
    guard let ds = bootedDataset() else { fail("the root is not on ZFS; name the pool with -p POOL") }
    return String(ds.split(separator: "/", omittingEmptySubsequences: false)[0])
}

func cString(_ raw: UnsafeRawBufferPointer) -> String {
    let bytes = raw.bindMemory(to: UInt8.self)
    let n = bytes.firstIndex(of: 0) ?? bytes.count
    return String(decoding: UnsafeBufferPointer(rebasing: bytes[0..<n]), as: UTF8.self)
}

/// Runs a tool (an absolute path) and waits: its exit status (128 + the
/// signal if one ended it) and, with `capture`, its standard output.
func run(_ argv: [String], capture: Bool = false) -> (status: Int32, output: String) {
    var cArgs: [UnsafeMutablePointer<CChar>?] = argv.map { strdup($0) } + [nil]
    var cEnv: [UnsafeMutablePointer<CChar>?] = [strdup("PATH=/usr/bin:/bin:/usr/sbin:/sbin"), nil]
    defer {
        for p in cArgs { free(p) }
        for p in cEnv { free(p) }
    }
    var fds: [Int32] = [-1, -1]
    var actions = posix_spawn_file_actions_t(bitPattern: 0)
    posix_spawn_file_actions_init(&actions)
    defer { posix_spawn_file_actions_destroy(&actions) }
    if capture {
        guard pipe(&fds) == 0 else { fail("pipe: \(String(cString: strerror(__error().pointee)))") }
        posix_spawn_file_actions_adddup2(&actions, fds[1], 1)
        posix_spawn_file_actions_addclose(&actions, fds[0])
    }
    var pid: pid_t = 0
    let error = posix_spawn(&pid, cArgs[0]!, &actions, nil, &cArgs, &cEnv)
    if capture { close(fds[1]) }
    guard error == 0 else {
        if capture { close(fds[0]) }
        fail("\(argv[0]): \(String(cString: strerror(error)))")
    }
    var output: [UInt8] = []
    if capture {
        var buffer = [UInt8](repeating: 0, count: 4096)
        while true {
            let n = buffer.withUnsafeMutableBytes { read(fds[0], $0.baseAddress, 4096) }
            if n > 0 { output.append(contentsOf: buffer[0..<n]); continue }
            if n < 0 && __error().pointee == EINTR { continue }
            break
        }
        close(fds[0])
    }
    var status: Int32 = 0
    while waitpid(pid, &status, 0) == -1 {
        guard __error().pointee == EINTR else { fail("waitpid \(argv[0]): \(String(cString: strerror(__error().pointee)))") }
    }
    let signal = status & 0x7f
    let code = signal == 0 ? (status >> 8) & 0xff : 128 + signal
    return (code, String(decoding: output, as: UTF8.self))
}

/// Runs a tool, echoing it; exits with its status if it fails.
func must(_ argv: [String]) {
    let status = run(argv).status
    guard status == 0 else { fail("\(argv.joined(separator: " ")): exit \(status)") }
}

/// One value of `zfs get` or `zpool get` (-H -o value), "-" if unset.
func value(_ argv: [String]) -> String {
    let r = run(argv, capture: true)
    guard r.status == 0 else { return "-" }
    return r.output.split(separator: "\n").first.map { String($0) } ?? "-"
}

struct BootEnvironments {
    let pool: String
    var root: String { "\(pool)/ROOT" }

    func dataset(_ name: String) -> String {
        guard !name.isEmpty, !name.contains("/"), !name.contains("@") else { fail("bad boot environment name: \(name)") }
        return "\(root)/\(name)"
    }

    func exists(_ ds: String) -> Bool {
        run([zfsCommand, "list", "-H", "-o", "name", ds], capture: true).status == 0
    }

    var bootfs: String { value([zpoolCommand, "get", "-H", "-o", "value", "bootfs", pool]) }
    var bootOnce: String { value([zfsCommand, "get", "-H", "-o", "value", bootOnceProperty, pool]) }

    /// Each BE with bectl's flags: N booted now, R booted on reboot, T tried
    /// once on the next boot (then R again).
    func list() -> Int32 {
        let r = run([zfsCommand, "list", "-H", "-d", "1", "-t", "filesystem", "-o", "name,used,origin", root], capture: true)
        guard r.status == 0 else { fail("no boot environments: \(root) not found") }
        let booted = bootedDataset() ?? "", next = bootfs, once = bootOnce
        print("BE Active Used Origin")
        for line in r.output.split(separator: "\n") {
            let f = line.split(separator: "\t").map { String($0) }
            guard f.count == 3, f[0] != root else { continue }
            var flags = ""
            if f[0] == booted { flags += "N" }
            if f[0] == next { flags += "R" }
            if f[0] == once { flags += "T" }
            if flags.isEmpty { flags = "-" }
            let name = String(f[0].dropFirst(root.count + 1))
            print("\(name) \(flags) \(f[1]) \(f[2])")
        }
        return 0
    }

    func create(_ name: String, from origin: String?) -> Int32 {
        let new = dataset(name)
        guard !exists(new) else { fail("\(new) exists") }
        let source: String
        if let origin { source = dataset(origin) } else {
            guard let booted = bootedDataset(), booted.hasPrefix(root + "/") else {
                fail("the booted root is not a boot environment of \(pool); name the origin")
            }
            source = booted
        }
        guard exists(source) else { fail("\(source) not found") }
        let snapshot = "\(source)@\(name)"
        must([zfsCommand, "snapshot", snapshot])
        must([zfsCommand, "clone", "-o", "canmount=noauto", "-o", "mountpoint=/", snapshot, new])
        print("ndbectl: created \(new) from \(snapshot)")
        return 0
    }

    func activate(_ name: String, once: Bool) -> Int32 {
        let ds = dataset(name)
        guard exists(ds) else { fail("\(ds) not found") }
        if once {
            must([zfsCommand, "set", "\(bootOnceProperty)=\(ds)", pool])
            print("ndbectl: \(ds) boots once, on the next boot; then \(bootfs) again")
        } else {
            must([zpoolCommand, "set", "bootfs=\(ds)", pool])
            if bootOnce != "-" { must([zfsCommand, "inherit", bootOnceProperty, pool]) }
            print("ndbectl: \(ds) is active: it boots from the next boot on")
        }
        return 0
    }

    func mount(_ name: String, at dir: String) -> Int32 {
        let ds = dataset(name)
        guard ds != bootedDataset() else { fail("\(ds) is the booted root") }
        guard exists(ds) else { fail("\(ds) not found") }
        must([zfsCommand, "set", "mountpoint=\(dir)", ds])
        must([zfsCommand, "mount", ds])
        print("ndbectl: \(ds) mounted at \(dir)")
        return 0
    }

    func unmount(_ name: String) -> Int32 {
        let ds = dataset(name)
        guard ds != bootedDataset() else { fail("\(ds) is the booted root") }
        must([zfsCommand, "umount", ds])
        must([zfsCommand, "set", "mountpoint=/", ds])
        return 0
    }

    func destroy(_ name: String) -> Int32 {
        let ds = dataset(name)
        guard exists(ds) else { fail("\(ds) not found") }
        guard ds != bootedDataset() else { fail("\(ds) is the booted root") }
        guard ds != bootfs else { fail("\(ds) is active; activate another first") }
        if ds == bootOnce { must([zfsCommand, "inherit", bootOnceProperty, pool]) }
        let origin = value([zfsCommand, "get", "-H", "-o", "value", "origin", ds])
        must([zfsCommand, "destroy", "-r", ds])
        // The snapshot create made, if nothing else was cloned from it.
        if origin.hasPrefix(root + "/"), origin.hasSuffix("@\(name)") {
            _ = run([zfsCommand, "destroy", origin])
        }
        print("ndbectl: destroyed \(ds)")
        return 0
    }
}
