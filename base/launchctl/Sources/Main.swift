// SPDX-License-Identifier: BSD-2-Clause
//
// launchctl for NeoDarwin (P1-08, docs/base/session.md): the first-party
// replacement for launchctl-842, which needs CoreFoundation. It speaks
// launchd-842's launch_msg() interface, as launchctl-842 does, and covers
// what the system needs:
//
//   launchctl bootstrap -S System [-s]   launchd runs this once at boot
//   launchctl load [-w] PATH...          submit the jobs in plists or directories
//   launchctl unload LABEL-OR-PATH...    remove jobs
//   launchctl start|stop LABEL
//   launchctl list                       PID, last exit status and label of each job
//
// Embedded Swift (language policy T3): NeoDarwin has no Swift runtime yet,
// so the tool must stand on libSystem alone.

import Launch

let launchDaemonDirectories = ["/System/Library/LaunchDaemons", "/Library/LaunchDaemons"]

func warn(_ message: String) {
    print("launchctl: \(message)")
}

// Embedded Swift has no CommandLine; the C entry point takes argv.
@_cdecl("main")
func launchctlMain(_ argc: Int32, _ argv: UnsafeMutablePointer<UnsafeMutablePointer<CChar>?>) -> Int32 {
    LaunchCtl.run(LaunchCtl.arguments(argc, argv))
}

enum LaunchCtl {
    static func run(_ args: [String]) -> Never {
        guard args.count >= 2 else { usage() }
        let rest = Array(args[2...])
        let status: Int32
        switch args[1] {
        case "bootstrap": status = bootstrap(rest)
        case "load": status = load(rest)
        case "unload", "remove": status = unload(rest)
        case "start": status = simple(LAUNCH_KEY_STARTJOB, rest)
        case "stop": status = simple(LAUNCH_KEY_STOPJOB, rest)
        case "list": status = list()
        default: usage()
        }
        exit(status)
    }

    static func usage() -> Never {
        print("usage: launchctl bootstrap -S System [-s] | load [-w] path... | unload label-or-path... | start label | stop label | list")
        exit(64)
    }

    static func arguments(_ argc: Int32, _ argv: UnsafeMutablePointer<UnsafeMutablePointer<CChar>?>) -> [String] {
        var result: [String] = []
        for i in 0..<Int(argc) {
            if let p = argv[i] { result.append(String(cString: p)) }
        }
        return result
    }
}

// MARK: - Loading jobs

/// Reads a plist file; nil (after a message) if it can't be read or parsed.
func readPlist(_ path: String) -> Plist? {
    guard let f = fopen(path, "r") else {
        warn("\(path): \(errorString(__error().pointee))")
        return nil
    }
    defer { fclose(f) }
    var bytes: [UInt8] = []
    var chunk = [UInt8](repeating: 0, count: 4096)
    while true {
        let n = chunk.withUnsafeMutableBytes { fread($0.baseAddress, 1, $0.count, f) }
        if n == 0 { break }
        bytes.append(contentsOf: chunk[0..<n])
    }
    do {
        return try parsePlist(bytes)
    } catch {
        warn("\(path): \(error.message) (at byte \(error.offset))")
        return nil
    }
}

/// The plist files in a directory, in name order; or the path itself.
func plistPaths(_ path: String) -> [String] {
    var st = stat()
    guard stat(path, &st) == 0 else {
        warn("\(path): \(errorString(__error().pointee))")
        return []
    }
    guard isDirectory(st) else { return [path] }
    var names: [String] = []
    guard let dir = opendir(path) else { return [] }
    while let entry = readdir(dir) {
        let name = withUnsafeBytes(of: entry.pointee.d_name) { raw in
            String(decoding: raw.prefix(Int(entry.pointee.d_namlen)), as: UTF8.self)
        }
        if name.hasSuffix(".plist") { names.append(name) }
    }
    closedir(dir)
    return sortedStrings(names).map { "\(path)/\($0)" }
}

func sortedStrings(_ strings: [String]) -> [String] {
    var a = strings
    // Insertion sort by bytes: a directory holds a few dozen plists at most.
    for i in a.indices.dropFirst() {
        var j = i
        while j > 0, lessThan(a[j], a[j - 1]) { a.swapAt(j, j - 1); j -= 1 }
    }
    return a
}

func lessThan(_ x: String, _ y: String) -> Bool {
    let xu = Array(x.utf8), yu = Array(y.utf8)
    for i in 0..<min(xu.count, yu.count) where xu[i] != yu[i] { return xu[i] < yu[i] }
    return xu.count < yu.count
}

/// Submits the jobs at `paths` (files or directories). A job marked Disabled
/// is skipped unless `force` (load -w; launchctl-842 also records the change
/// in its overrides database, which NeoDarwin doesn't keep yet).
func submitJobs(_ paths: [String], force: Bool) -> Int32 {
    var failures: Int32 = 0
    for path in paths {
        for file in plistPaths(path) {
            guard let job = readPlist(file) else { failures += 1; continue }
            guard let label = job[LAUNCH_JOBKEY_LABEL]?.stringValue else {
                warn("\(file): no Label")
                failures += 1
                continue
            }
            if job[LAUNCH_JOBKEY_DISABLED]?.boolValue == true && !force { continue }
            var submitted = job
            if case .dictionary(let entries) = job {
                submitted = .dictionary(entries.filter { $0.key != LAUNCH_JOBKEY_DISABLED })
            }
            guard let data = launchData(submitted) else { failures += 1; continue }
            let error = responseErrno(launchMessage(launchRequest(LAUNCH_KEY_SUBMITJOB, data)))
            if error == EEXIST {
                warn("\(label): already loaded")
            } else if error != 0 {
                warn("\(label): \(errorString(error))")
                failures += 1
            }
        }
    }
    return failures == 0 ? 0 : 1
}

func load(_ args: [String]) -> Int32 {
    var force = false
    var paths: [String] = []
    for a in args {
        if a == "-w" || a == "-F" { force = true } else { paths.append(a) }
    }
    guard !paths.isEmpty else { LaunchCtl.usage() }
    return submitJobs(paths, force: force)
}

func unload(_ args: [String]) -> Int32 {
    var failures: Int32 = 0
    for a in args where a != "-w" {
        var labels: [String] = []
        if a.hasPrefix("/") {
            for file in plistPaths(a) {
                if let label = readPlist(file)?[LAUNCH_JOBKEY_LABEL]?.stringValue { labels.append(label) }
            }
        } else {
            labels.append(a)
        }
        for label in labels {
            let error = responseErrno(launchMessage(launchRequest(LAUNCH_KEY_REMOVEJOB, launch_data_new_string(label))))
            if error != 0 { warn("\(label): \(errorString(error))"); failures += 1 }
        }
    }
    return failures == 0 ? 0 : 1
}

func simple(_ command: String, _ args: [String]) -> Int32 {
    guard args.count == 1 else { LaunchCtl.usage() }
    let error = responseErrno(launchMessage(launchRequest(command, launch_data_new_string(args[0]))))
    if error != 0 { warn("\(args[0]): \(errorString(error))"); return 1 }
    return 0
}

func list() -> Int32 {
    guard let request = launch_data_new_string(LAUNCH_KEY_GETJOBS), let jobs = launchMessage(request) else {
        warn("launch_msg: \(errorString(__error().pointee))")
        return 1
    }
    defer { launch_data_free(jobs) }
    guard launch_data_get_type(jobs) == LAUNCH_DATA_DICTIONARY else {
        warn("GetJobs: \(errorString(launch_data_get_errno(jobs)))")
        return 1
    }
    print("PID\tStatus\tLabel")
    launch_data_dict_iterate(jobs, { job, label, _ in
        guard let job, let label else { return }
        var pid = "-"
        var status = "-"
        if let p = launch_data_dict_lookup(job, LAUNCH_JOBKEY_PID) { pid = "\(launch_data_get_integer(p))" }
        if let s = launch_data_dict_lookup(job, LAUNCH_JOBKEY_LASTEXITSTATUS) { status = "\(launch_data_get_integer(s))" }
        print("\(pid)\t\(status)\t\(String(cString: label))")
    }, nil)
    return 0
}

// MARK: - Bootstrapping the system

/// `bootstrap -S System`: what launchctl-842's system_specific_bootstrap()
/// does that NeoDarwin's system needs, in its order. The kernel mounts the
/// root read-only, and launchd creates its socket under /var/tmp only once
/// asked, so the root is made writable first (launchctl-842 runs fsck and
/// `mount -uw /`; NeoDarwin has neither tool yet and remounts directly).
/// Left out until their tools exist: fsck, /etc/rc.* scripts, loopback
/// setup, sysctl.conf, BootCache, auditd and IOKit's quiet wait.
func bootstrap(_ args: [String]) -> Int32 {
    guard args.count >= 2, args[0] == "-S" else { LaunchCtl.usage() }
    guard args[1] == "System" else {
        warn("bootstrap: only the System session is supported")
        return 1
    }
    remountRootWritable()
    var mib: [Int32] = [CTL_KERN, KERN_HOSTNAME]
    let hostname = "localhost"
    _ = hostname.withCString { sysctl(&mib, 2, nil, nil, UnsafeMutableRawPointer(mutating: $0), strlen($0) + 1) }
    emptyDirectory("/var/run")
    emptyDirectory("/tmp")
    _ = unlink("/etc/nologin")
    touch("/var/run/utmpx")
    _ = _vproc_set_global_on_demand(true)
    let status = submitJobs(launchDaemonDirectories.filter { access($0, F_OK) == 0 }, force: false)
    _ = _vproc_set_global_on_demand(false)
    return status
}

/// `mount -uw /`: an update mount of the root without MNT_RDONLY.
func remountRootWritable() {
    var fs = statfs()
    guard statfs("/", &fs) == 0 else {
        warn("statfs /: \(errorString(__error().pointee))")
        return
    }
    guard (fs.f_flags & UInt32(MNT_RDONLY)) != 0 else { return }
    let type = withUnsafeBytes(of: fs.f_fstypename) { String(decoding: $0.prefix { $0 != 0 }, as: UTF8.self) }
    guard type == "hfs" else {
        warn("the root is read-only, and launchctl can't remount \(type)")
        return
    }
    // The kernel names the root's device "root_device", which no path
    // reaches; the update mount needs one, so find the block device in
    // /dev that holds the root.
    var device = withUnsafeBytes(of: fs.f_mntfromname) { String(decoding: $0.prefix { $0 != 0 }, as: UTF8.self) }
    if !device.hasPrefix("/dev/") {
        guard let found = blockDevice(holding: "/") else {
            warn("mount -uw /: no block device in /dev holds the root")
            return
        }
        device = found
    }
    let flags = Int32(bitPattern: (fs.f_flags & ~UInt32(MNT_RDONLY)) | UInt32(MNT_UPDATE))
    let error = device.withCString { fspec -> Int32 in
        var args = nd_hfs_mount_args()
        args.fspec = UnsafeMutablePointer(mutating: fspec)
        return mount("hfs", "/", flags, &args) == 0 ? 0 : __error().pointee
    }
    if error != 0 { warn("mount -uw / (\(device)): \(errorString(error))") }
}

/// The /dev block device whose device number is `path`'s file system's.
func blockDevice(holding path: String) -> String? {
    var target = stat()
    guard stat(path, &target) == 0, let dir = opendir("/dev") else { return nil }
    defer { closedir(dir) }
    while let entry = readdir(dir) {
        let name = withUnsafeBytes(of: entry.pointee.d_name) { raw in
            String(decoding: raw.prefix(Int(entry.pointee.d_namlen)), as: UTF8.self)
        }
        var st = stat()
        let candidate = "/dev/\(name)"
        if lstat(candidate, &st) == 0, (Int32(st.st_mode) & S_IFMT) == S_IFBLK, st.st_rdev == target.st_dev {
            return candidate
        }
    }
    return nil
}

/// Removes everything inside `path`, leaving the directory.
func emptyDirectory(_ path: String) {
    guard let dir = opendir(path) else { return }
    var names: [String] = []
    while let entry = readdir(dir) {
        let name = withUnsafeBytes(of: entry.pointee.d_name) { raw in
            String(decoding: raw.prefix(Int(entry.pointee.d_namlen)), as: UTF8.self)
        }
        if name != "." && name != ".." { names.append(name) }
    }
    closedir(dir)
    for name in names {
        let child = "\(path)/\(name)"
        var st = stat()
        guard lstat(child, &st) == 0 else { continue }
        if isDirectory(st) {
            emptyDirectory(child)
            _ = rmdir(child)
        } else {
            _ = unlink(child)
        }
    }
}

func touch(_ path: String) {
    let fd = creat(path, 0o644)
    if fd >= 0 { _ = close(fd) }
}

func isDirectory(_ st: stat) -> Bool {
    (Int32(st.st_mode) & S_IFMT) == S_IFDIR
}
