// SPDX-License-Identifier: BSD-2-Clause
//
// sshd-keygen-wrapper for NeoDarwin: what Apple's (OpenSSH-354.0.3,
// sshd-keygen-wrapper/SSHDWrapper.swift) does on an installed system.
// launchd runs it, through launchproxy, for each connection to the ssh
// socket of com.openssh.sshd (ssh.plist), with the connection as standard
// input and output. It generates each missing host key (ecdsa, ed25519,
// rsa) in /etc/ssh, as HostKeyManager does (`ssh-keygen -q -t ALG -f PATH
// -N "" -C ""`), then replaces itself with `sshd -i`.
// Left out, as macOS-only: the Recovery (base system) keys and banner, the
// preboot volume's copy of the keys and sshd-fvunlock's plist.
// One addition: until NeoDarwin has a log store, syslog(3) writes to
// standard error (base/standins/libsystem_trace), which sshd -i points at
// /dev/null; `-e -E /var/log/sshd.log` keeps sshd's log in that file.
//
// Embedded Swift (language policy T3), on libSystem alone.

import Wrapper

let sshDirectory = "/etc/ssh"
let keygen = "/usr/bin/ssh-keygen"
let sshd = "/usr/sbin/sshd"
let sshdArguments = ["-i", "-e", "-E", "/var/log/sshd.log"]

func complain(_ message: String) {
    let line = "sshd-keygen-wrapper: \(message)\n"
    _ = line.withCString { write(2, $0, strlen($0)) }
}

@_cdecl("main")
func wrapperMain(_ argc: Int32, _ argv: UnsafeMutablePointer<UnsafeMutablePointer<CChar>?>) -> Int32 {
    guard argc <= 1 else {
        complain("Usage: sshd-keygen-wrapper")
        exit(EX_USAGE)
    }
    for algorithm in ["ecdsa", "ed25519", "rsa"] {
        let path = "\(sshDirectory)/ssh_host_\(algorithm)_key"
        guard access(path, F_OK) != 0 else { continue }
        let status = run([keygen, "-q", "-t", algorithm, "-f", path, "-N", "", "-C", ""])
        if status != 0 { complain("Failed to generate \(algorithm) host key: ssh-keygen exited with \(status)") }
    }
    let args = [sshd] + sshdArguments
    var cArgs: [UnsafeMutablePointer<CChar>?] = args.map { strdup($0) } + [nil]
    _ = execv(sshd, &cArgs)
    complain("\(sshd): \(String(cString: strerror(__error().pointee)))")
    exit(EX_SOFTWARE)
}

/// Runs a tool with standard input and output on /dev/null (the
/// connection is ours, for sshd), standard error inherited; its exit
/// status, or -1 if it couldn't run or a signal ended it.
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
    posix_spawn_file_actions_addopen(&actions, 1, "/dev/null", O_WRONLY, 0)
    var pid: pid_t = 0
    let error = posix_spawn(&pid, cArgs[0]!, &actions, nil, &cArgs, &cEnv)
    guard error == 0 else {
        complain("\(argv[0]): \(String(cString: strerror(error)))")
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
