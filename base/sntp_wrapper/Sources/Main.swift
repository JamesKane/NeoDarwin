// SPDX-License-Identifier: BSD-2-Clause
//
// sntp-wrapper: sets the clock from the NTP servers in /etc/ntp.conf
// (docs/base/pf-ntp.md). launchd runs it from com.neodarwin.sntp.plist at
// load and every StartInterval seconds.
//
// What macOS's ntpd-wrapper (ntp-139) did before it started ntpd, without
// configd: wait (at most 60 seconds) until the system has a resolver
// configuration (/etc/resolv.conf exists: a DHCP lease's, or a static
// file), then, for each `server` or `pool` line of /etc/ntp.conf in order,
// run `sntp -K /dev/null -S -s -M 128 HOST` until one succeeds. sntp
// (Apple's, from ntp-139) steps the clock when it is more than 128 ms off,
// ntpd's step threshold, and slews it otherwise. A HOST of the form
// host:port or [address]:port names a server on another port (NeoDarwin's
// sntp patch 0002). sntp's report goes to standard output and its errors to
// standard error, which the job keeps in /var/log/sntp.log.
// Exit status: 0 when a server set the clock, EX_UNAVAILABLE when none
// did, EX_CONFIG when /etc/ntp.conf names no server.
//
// Embedded Swift (language policy T3), on libSystem alone.

import SNTPWrapper

let configPath = "/etc/ntp.conf"
let resolverPath = "/etc/resolv.conf"
let sntp = "/usr/bin/sntp"
let sntpArguments = ["-K", "/dev/null", "-S", "-s", "-M", "128"]
let resolverWait = 60 // seconds

func say(_ message: String, to fd: Int32 = 2) {
    let line = "sntp-wrapper: \(message)\n"
    _ = line.withCString { write(fd, $0, strlen($0)) }
}

/// The hosts of /etc/ntp.conf's `server` and `pool` lines, in order: the
/// word after the keyword; the line's options (iburst, ...) are ntpd's and
/// sntp has no use for them. # starts a comment.
func readServers(_ path: String) -> [String]? {
    guard let file = fopen(path, "r") else { return nil }
    defer { fclose(file) }
    var servers: [String] = []
    var buffer = [CChar](repeating: 0, count: 1024)
    while fgets(&buffer, Int32(buffer.count), file) != nil {
        var line = buffer.prefix(strlen(buffer)).map { UInt8(bitPattern: $0) }
        if let hash = line.firstIndex(of: UInt8(ascii: "#")) { line.removeSubrange(hash...) }
        let words = line.split(whereSeparator: { $0 == UInt8(ascii: " ") || $0 == UInt8(ascii: "\t") || $0 == UInt8(ascii: "\n") || $0 == UInt8(ascii: "\r") })
        guard words.count >= 2 else { continue }
        let keyword = String(decoding: words[0], as: UTF8.self)
        if keyword == "server" || keyword == "pool" {
            servers.append(String(decoding: words[1], as: UTF8.self))
        }
    }
    return servers
}

/// Runs sntp with standard input on /dev/null and standard output and
/// error inherited; its exit status, or -1 if it couldn't run or a signal
/// ended it.
func run(_ argv: [String]) -> Int32 {
    var cArgs: [UnsafeMutablePointer<CChar>?] = argv.map { strdup($0) } + [nil]
    var cEnv: [UnsafeMutablePointer<CChar>?] = [strdup("PATH=/usr/bin:/bin:/usr/sbin:/sbin"), nil]
    defer {
        for p in cArgs { free(p) }
        for p in cEnv { free(p) }
    }
    var actions: posix_spawn_file_actions_t? = nil
    posix_spawn_file_actions_init(&actions)
    defer { posix_spawn_file_actions_destroy(&actions) }
    posix_spawn_file_actions_addopen(&actions, 0, "/dev/null", O_RDONLY, 0)
    var pid: pid_t = 0
    let error = posix_spawn(&pid, cArgs[0]!, &actions, nil, &cArgs, &cEnv)
    guard error == 0 else {
        say("\(argv[0]): \(String(cString: strerror(error)))")
        return -1
    }
    var status: Int32 = 0
    while waitpid(pid, &status, 0) == -1 {
        guard __error().pointee == EINTR else { return -1 }
    }
    // <sys/wait.h>'s WIFEXITED and friends are macros Swift doesn't import.
    guard status & 0x7f == 0 else { return -1 }
    return (status >> 8) & 0xff
}

@_cdecl("main")
func wrapperMain(_ argc: Int32, _ argv: UnsafeMutablePointer<UnsafeMutablePointer<CChar>?>) -> Int32 {
    guard argc <= 1 else {
        say("Usage: sntp-wrapper")
        exit(EX_USAGE)
    }
    guard let servers = readServers(configPath) else {
        say("\(configPath): \(String(cString: strerror(__error().pointee)))")
        exit(EX_CONFIG)
    }
    guard !servers.isEmpty else {
        say("\(configPath) names no server")
        exit(EX_CONFIG)
    }
    var info = stat()
    var waited = 0
    while stat(resolverPath, &info) != 0 && waited < resolverWait {
        if waited == 0 { say("waiting for \(resolverPath) (the network)") }
        sleep(1)
        waited += 1
    }
    for server in servers {
        say("\(server)")
        let status = run([sntp] + sntpArguments + [server])
        if status == 0 { exit(0) }
        say("\(server): sntp exited with \(status)")
    }
    say("no server set the clock")
    exit(EX_UNAVAILABLE)
}
