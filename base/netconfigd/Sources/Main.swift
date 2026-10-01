// SPDX-License-Identifier: BSD-2-Clause
//
// netconfigd: brings NeoDarwin's Ethernet interfaces up at boot and when
// they appear (docs/kernel/network.md, "Bringing interfaces up"). launchd
// runs it from com.neodarwin.netconfigd.plist, KeepAlive.
//
// For each en* interface (getifaddrs' AF_LINK entries), by its line in
// /etc/netconfigd.conf (FreeBSD rc.conf style, ifconfig_<interface>, then
// ifconfig_DEFAULT, then DHCP):
//  - DHCP: `ifconfig <if> up`, then `dhclient -d <if>` (FreeBSD's dhclient,
//    in the foreground, logging to standard error), restarted 10 seconds
//    after it exits for as long as the interface exists;
//  - static: `ifconfig <if> <args> up`, then `route -n add default
//    <defaultrouter>` once, if set;
//  - NONE: nothing.
// and, by its ifconfig_<interface>_ipv6 line (then ifconfig_DEFAULT_ipv6,
// then AUTOCONF, or NONE when the interface is NONE), IPv6:
//  - AUTOCONF (or FreeBSD's "inet6 accept_rtadv"): stateless address
//    autoconfiguration, as IPConfiguration's automatic-v6 service starts it
//    on macOS: IPv6 attached to the interface (SIOCPROTOATTACH_IN6),
//    `ifconfig <if> inet6 -ifdisabled`, the link-local address started
//    (SIOCLL_START) and router advertisements accepted (SIOCAUTOCONF_START);
//    then, whenever the link comes up, `rtsol <if>` solicits a router. The
//    kernel takes the prefixes, addresses and default route from the
//    advertisements;
//  - static: `ifconfig <if> <args>` (e.g. "inet6 2001:db8::10 prefixlen 64"),
//    then `route -n add -inet6 default <ipv6_defaultrouter>` once, if set;
//  - NONE: nothing.
// Interfaces that appear later are found again on the next scan: a message
// on the routing socket (an interface's flags or addresses changed) or a
// 5-second timer triggers one.
//
// On macOS configd's IPConfiguration plugin does this, and its
// InterfaceNamer names the interfaces; NeoDarwin has neither (the kernel's
// NeoDarwinInterfaceNamer names them). Embedded Swift (language policy T3),
// on libSystem alone, as launchctl is.

import NetConfig

let configPath = "/etc/netconfigd.conf"
let dhclient = "/sbin/dhclient"
let ifconfig = "/sbin/ifconfig"
let route = "/sbin/route"
let rtsol = "/sbin/rtsol"
let restartDelay = 10          // seconds before a dhclient that exited runs again
let scanInterval: Int32 = 5000 // milliseconds between scans without routing messages

func log(_ message: String) {
    let line = "netconfigd: \(message)\n"
    _ = line.withCString { write(2, $0, strlen($0)) }
}

func errorString(_ code: Int32) -> String {
    String(cString: strerror(code))
}

// MARK: - Configuration

/// /etc/netconfigd.conf's assignments, in order: name="value" (or
/// name=value), one per line; # starts a comment.
func readConfig(_ path: String) -> [(String, String)] {
    guard let file = fopen(path, "r") else { return [] }
    defer { fclose(file) }
    var entries: [(String, String)] = []
    var buffer = [CChar](repeating: 0, count: 1024)
    while fgets(&buffer, Int32(buffer.count), file) != nil {
        var line = buffer.prefix(strlen(buffer)).map { UInt8(bitPattern: $0) }
        if let hash = line.firstIndex(of: UInt8(ascii: "#")) { line.removeSubrange(hash...) }
        guard let eq = line.firstIndex(of: UInt8(ascii: "=")) else { continue }
        let name = trimmed(Array(line[..<eq]))
        var value = trimmed(Array(line[(eq + 1)...]))
        if value.count >= 2, let first = value.first, first == value.last,
            first == UInt8(ascii: "\"") || first == UInt8(ascii: "'")
        {
            value = Array(value[1..<(value.count - 1)])
        }
        guard !name.isEmpty else { continue }
        entries.append((String(decoding: name, as: UTF8.self), String(decoding: value, as: UTF8.self)))
    }
    return entries
}

func isSpace(_ c: UInt8) -> Bool {
    c == UInt8(ascii: " ") || c == UInt8(ascii: "\t") || c == UInt8(ascii: "\n") || c == UInt8(ascii: "\r")
}

func trimmed(_ bytes: [UInt8]) -> [UInt8] {
    var start = 0
    var end = bytes.count
    while start < end && isSpace(bytes[start]) { start += 1 }
    while end > start && isSpace(bytes[end - 1]) { end -= 1 }
    return Array(bytes[start..<end])
}

/// The words of a static configuration, split at spaces and tabs.
func words(_ s: String) -> [String] {
    var result: [String] = []
    var word: [UInt8] = []
    for c in s.utf8 {
        if isSpace(c) {
            if !word.isEmpty { result.append(String(decoding: word, as: UTF8.self)); word = [] }
        } else {
            word.append(c)
        }
    }
    if !word.isEmpty { result.append(String(decoding: word, as: UTF8.self)) }
    return result
}

func lookup(_ config: [(String, String)], _ name: String) -> String? {
    var found: String? = nil
    for (key, value) in config where key == name { found = value }   // the last assignment wins, as in rc.conf
    return found
}

func caseInsensitiveEqual(_ a: String, _ b: String) -> Bool {
    strcasecmp(a, b) == 0
}

// MARK: - Interfaces

/// The names of the Ethernet interfaces (en*) the kernel has now.
func ethernetInterfaces() -> [String] {
    var list: UnsafeMutablePointer<ifaddrs>? = nil
    guard getifaddrs(&list) == 0 else {
        log("getifaddrs: \(errorString(__error().pointee))")
        return []
    }
    defer { freeifaddrs(list) }
    var names: [String] = []
    var entry = list
    while let ifa = entry {
        entry = ifa.pointee.ifa_next
        guard let addr = ifa.pointee.ifa_addr, Int32(addr.pointee.sa_family) == AF_LINK else { continue }
        let name = String(cString: ifa.pointee.ifa_name)
        if name.hasPrefix("en") && !names.contains(name) { names.append(name) }
    }
    return names
}

// MARK: - Processes

/// Starts a tool (an absolute path) with standard input on /dev/null and
/// standard output and error inherited; its pid, or nil.
func spawn(_ argv: [String]) -> pid_t? {
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
        log("\(argv[0]): \(errorString(error))")
        return nil
    }
    return pid
}

/// Runs a tool and waits for it: its exit status, or -1.
func run(_ argv: [String]) -> Int32 {
    guard let pid = spawn(argv) else { return -1 }
    var status: Int32 = 0
    while waitpid(pid, &status, 0) == -1 {
        guard __error().pointee == EINTR else { return -1 }
    }
    return describe(status)
}

/// An exit status from waitpid's: the code, or 128 + the signal.
// <sys/wait.h>'s WIFEXITED and friends are macros Swift doesn't import.
func describe(_ status: Int32) -> Int32 {
    let signal = status & 0x7f
    return signal == 0 ? (status >> 8) & 0xff : 128 + signal
}

// MARK: - The interfaces' state

enum Mode {
    case dhcp
    case manual([String])
    case none
}

enum IPv6Mode {
    case autoconf
    case manual([String])
    case none
}

struct Interface {
    var name: String
    var mode: Mode
    var ipv6: IPv6Mode
    var client: pid_t? = nil      // dhclient's pid while it runs
    var solicitor: pid_t? = nil   // rtsol's pid while it runs
    var autoconfStarted = false   // IPv6 attached and accepting router advertisements
    var linkWasActive = false     // the link's state when last looked at
    var restartAt: Int = 0        // when to start dhclient again (seconds, CLOCK_MONOTONIC)
}

func now() -> Int {
    var ts = timespec()
    clock_gettime(CLOCK_MONOTONIC, &ts)
    return Int(ts.tv_sec)
}

func mode(of name: String, _ config: [(String, String)]) -> Mode {
    let value = lookup(config, "ifconfig_\(name)") ?? lookup(config, "ifconfig_DEFAULT") ?? "DHCP"
    if caseInsensitiveEqual(value, "DHCP") || caseInsensitiveEqual(value, "SYNCDHCP") { return .dhcp }
    if caseInsensitiveEqual(value, "NONE") || value.isEmpty { return .none }
    return .manual(words(value))
}

func ipv6Mode(of name: String, _ mode: Mode, _ config: [(String, String)]) -> IPv6Mode {
    var fallback = "AUTOCONF"
    if case .none = mode { fallback = "NONE" }
    let value = lookup(config, "ifconfig_\(name)_ipv6") ?? lookup(config, "ifconfig_DEFAULT_ipv6") ?? fallback
    let args = words(value)
    if caseInsensitiveEqual(value, "AUTOCONF")
        || (args.count == 2 && args[0] == "inet6" && args[1] == "accept_rtadv")
    {
        return .autoconf
    }
    if caseInsensitiveEqual(value, "NONE") || value.isEmpty { return .none }
    return .manual(args)
}

/// Configures a new interface: brings it up, and starts its DHCP client or
/// applies its static configuration, then its IPv6 configuration. The
/// default routers are added once.
func configure(_ interface: inout Interface, _ config: [(String, String)], defaultRouterAdded: inout Bool,
    ipv6DefaultRouterAdded: inout Bool)
{
    configureIPv4(&interface, config, defaultRouterAdded: &defaultRouterAdded)
    configureIPv6(&interface, config, defaultRouterAdded: &ipv6DefaultRouterAdded)
}

func configureIPv4(_ interface: inout Interface, _ config: [(String, String)], defaultRouterAdded: inout Bool) {
    switch interface.mode {
    case .none:
        log("\(interface.name): left alone (NONE)")
    case .manual(let args):
        let status = run([ifconfig, interface.name] + args + ["up"])
        log("\(interface.name): static (ifconfig \(interface.name) \(args.joined(separator: " ")) up): status \(status)")
        if status == 0, !defaultRouterAdded, let router = lookup(config, "defaultrouter"), !router.isEmpty {
            let routeStatus = run([route, "-n", "add", "default", router])
            log("default route via \(router): status \(routeStatus)")
            defaultRouterAdded = routeStatus == 0
        }
    case .dhcp:
        let status = run([ifconfig, interface.name, "up"])
        if status != 0 { log("\(interface.name): ifconfig up: status \(status)") }
        startClient(&interface)
    }
}

// MARK: - IPv6

/// An in6_aliasreq or in6_ifreq name field set to `name`.
func setInterfaceName<T>(_ field: inout T, _ name: String) {
    withUnsafeMutableBytes(of: &field) { raw in
        for i in raw.indices { raw[i] = 0 }
        for (i, c) in name.utf8.prefix(raw.count - 1).enumerated() { raw[i] = c }
    }
}

/// One of IPConfiguration's interface requests; a log line if it fails.
/// The request takes an in6_aliasreq with only the name set.
func request6(_ s: Int32, _ name: String, _ label: String, _ request: UInt) -> Bool {
    var ifra = in6_aliasreq()
    setInterfaceName(&ifra.ifra_name, name)
    if nd_ioctl(s, request, &ifra) != 0 && __error().pointee != EEXIST {
        log("\(name): \(label): \(errorString(__error().pointee))")
        return false
    }
    return true
}

func configureIPv6(_ interface: inout Interface, _ config: [(String, String)], defaultRouterAdded: inout Bool) {
    let name = interface.name
    switch interface.ipv6 {
    case .none:
        return
    case .manual(let args):
        let status = run([ifconfig, name] + args + ["up"])
        log("\(name): IPv6 static (ifconfig \(name) \(args.joined(separator: " ")) up): status \(status)")
        if status == 0, !defaultRouterAdded, let router = lookup(config, "ipv6_defaultrouter"), !router.isEmpty {
            let routeStatus = run([route, "-n", "add", "-inet6", "default", router])
            log("IPv6 default route via \(router): status \(routeStatus)")
            defaultRouterAdded = routeStatus == 0
        }
    case .autoconf:
        let s = socket(AF_INET6, SOCK_DGRAM, 0)
        guard s >= 0 else {
            log("\(name): IPv6: socket: \(errorString(__error().pointee))")
            return
        }
        defer { _ = close(s) }
        if case .none = interface.mode { _ = run([ifconfig, name, "up"]) }
        // IPConfiguration's order: attach, enable, link-local, then RAs.
        guard request6(s, name, "SIOCPROTOATTACH_IN6", ND_SIOCPROTOATTACH_IN6) else { return }
        let enabled = run([ifconfig, name, "inet6", "-ifdisabled"])
        if enabled != 0 { log("\(name): ifconfig inet6 -ifdisabled: status \(enabled)") }
        guard request6(s, name, "SIOCLL_START", ND_SIOCLL_START) else { return }
        var ifr = in6_ifreq()
        setInterfaceName(&ifr.ifr_name, name)
        guard nd_ioctl(s, ND_SIOCAUTOCONF_START, &ifr) == 0 else {
            log("\(name): SIOCAUTOCONF_START: \(errorString(__error().pointee))")
            return
        }
        interface.autoconfStarted = true
        log("\(name): IPv6 autoconfiguration (link-local address started, router advertisements accepted)")
    }
}

/// Whether the interface's link is up (SIOCGIFMEDIA's IFM_ACTIVE); an
/// interface without media status counts as up. rtsol ignores an
/// interface whose link is down (and, run once, exits at once), so it is
/// started when the link comes up, as IPConfiguration solicits on link up.
func linkActive(_ name: String) -> Bool {
    let s = socket(AF_INET, SOCK_DGRAM, 0)
    guard s >= 0 else { return false }
    defer { _ = close(s) }
    var ifmr = ifmediareq()
    setInterfaceName(&ifmr.ifm_name, name)
    guard nd_ioctl(s, ND_SIOCGIFMEDIA, &ifmr) == 0 else { return true }
    guard ifmr.ifm_status & IFM_AVALID != 0 else { return true }
    return ifmr.ifm_status & IFM_ACTIVE != 0
}

/// rtsol: router solicitations until an advertisement comes (or three
/// went unanswered); the kernel processes the advertisement.
func startSolicitor(_ interface: inout Interface) {
    interface.solicitor = spawn([rtsol, interface.name])
    if let pid = interface.solicitor {
        log("\(interface.name): IPv6 autoconfiguration (rtsol \(interface.name), pid \(pid))")
    }
}

func startClient(_ interface: inout Interface) {
    interface.client = spawn([dhclient, "-d", interface.name])
    if let pid = interface.client {
        log("\(interface.name): DHCP (dhclient -d \(interface.name), pid \(pid))")
    } else {
        interface.restartAt = now() + restartDelay
    }
}

// MARK: - Main loop

@_cdecl("main")
func netconfigdMain(_ argc: Int32, _ argv: UnsafeMutablePointer<UnsafeMutablePointer<CChar>?>) -> Int32 {
    let config = readConfig(configPath)
    var interfaces: [Interface] = []
    var defaultRouterAdded = false
    var ipv6DefaultRouterAdded = false

    // A routing socket: any message (an interface's flags or addresses
    // changed, one attached) is a reason to look for new interfaces.
    let routing = socket(PF_ROUTE, SOCK_RAW, 0)
    if routing < 0 { log("routing socket: \(errorString(__error().pointee)); scanning on a timer only") }

    while true {
        // New interfaces.
        for name in ethernetInterfaces() where !interfaces.contains(where: { $0.name == name }) {
            let ipv4 = mode(of: name, config)
            var interface = Interface(name: name, mode: ipv4, ipv6: ipv6Mode(of: name, ipv4, config))
            configure(&interface, config, defaultRouterAdded: &defaultRouterAdded,
                ipv6DefaultRouterAdded: &ipv6DefaultRouterAdded)
            interfaces.append(interface)
        }

        // Clients that exited, and their restarts.
        var status: Int32 = 0
        while true {
            let pid = waitpid(-1, &status, WNOHANG)
            if pid <= 0 { break }
            for i in interfaces.indices where interfaces[i].solicitor == pid {
                log("\(interfaces[i].name): rtsol exited (status \(describe(status)))")
                interfaces[i].solicitor = nil
            }
            for i in interfaces.indices where interfaces[i].client == pid {
                log("\(interfaces[i].name): dhclient exited (status \(describe(status))); again in \(restartDelay) s")
                interfaces[i].client = nil
                interfaces[i].restartAt = now() + restartDelay
            }
        }
        let present = ethernetInterfaces()

        // Router solicitation whenever an autoconfiguring interface's link
        // comes up (at boot, the link comes up after `ifconfig up`).
        for i in interfaces.indices where interfaces[i].autoconfStarted && present.contains(interfaces[i].name) {
            let active = linkActive(interfaces[i].name)
            if active && !interfaces[i].linkWasActive && interfaces[i].solicitor == nil {
                startSolicitor(&interfaces[i])
            }
            interfaces[i].linkWasActive = active
        }

        var nextRestart = Int.max
        for i in interfaces.indices {
            guard case .dhcp = interfaces[i].mode, interfaces[i].client == nil else { continue }
            guard present.contains(interfaces[i].name) else { continue }
            if now() >= interfaces[i].restartAt {
                _ = run([ifconfig, interfaces[i].name, "up"])
                startClient(&interfaces[i])
            } else {
                nextRestart = min(nextRestart, interfaces[i].restartAt)
            }
        }

        // Wait for a routing message, a restart or the next scan.
        var timeout = scanInterval
        if nextRestart != Int.max { timeout = min(timeout, Int32(max(0, nextRestart - now()) * 1000)) }
        if routing >= 0 {
            var pfd = pollfd(fd: routing, events: Int16(POLLIN), revents: 0)
            if poll(&pfd, 1, timeout) > 0 {
                var buffer = [UInt8](repeating: 0, count: 2048)
                _ = read(routing, &buffer, buffer.count)
            }
        } else {
            _ = usleep(useconds_t(timeout) * 1000)
        }
    }
}
