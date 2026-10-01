// SPDX-License-Identifier: BSD-2-Clause
//
// The network work of launchctl-842: the loopback interface at boot
// (loopback_setup_ipv4/ipv6, system_specific_bootstrap) and the listening
// sockets of a job's Sockets dictionary (sock_dict_edit_entry), which
// launchctl, not launchd, creates and passes to launchd as file
// descriptors in the SubmitJob message.

import Launch

// MARK: - The loopback interface

/// lo0 up, with 127.0.0.1/8 and ::1/128, as launchctl-842 configures it
/// before any job runs. The kernel attaches lo0 but gives it no address.
func setUpLoopback() {
    setUpLoopbackIPv4()
    setUpLoopbackIPv6()
}

/// An ifreq/ifaliasreq/in6_aliasreq name field set to "lo0".
func setInterfaceName<T>(_ name: inout T) {
    withUnsafeMutableBytes(of: &name) { raw in
        for i in raw.indices { raw[i] = 0 }
        for (i, c) in "lo0".utf8.enumerated() { raw[i] = c }
    }
}

/// SIOCGIFFLAGS, then SIOCSIFFLAGS with IFF_UP added.
func bringUp(_ s: Int32) {
    var ifr = ifreq()
    setInterfaceName(&ifr.ifr_name)
    guard nd_ioctl(s, ND_SIOCGIFFLAGS, &ifr) == 0 else {
        warn("lo0: SIOCGIFFLAGS: \(errorString(__error().pointee))")
        return
    }
    ifr.ifr_ifru.ifru_flags |= Int16(IFF_UP)
    if nd_ioctl(s, ND_SIOCSIFFLAGS, &ifr) != 0 {
        warn("lo0: SIOCSIFFLAGS: \(errorString(__error().pointee))")
    }
}

func setUpLoopbackIPv4() {
    let s = socket(AF_INET, SOCK_DGRAM, 0)
    guard s >= 0 else { return }
    defer { _ = close(s) }
    bringUp(s)
    var ifra = ifaliasreq()
    setInterfaceName(&ifra.ifra_name)
    // ifra_addr and ifra_mask are struct sockaddr; sockaddr_in fits in one.
    var addr = sockaddr_in()
    addr.sin_len = UInt8(MemoryLayout<sockaddr_in>.size)
    addr.sin_family = sa_family_t(AF_INET)
    addr.sin_addr.s_addr = UInt32(0x7f00_0001).bigEndian          // INADDR_LOOPBACK
    var mask = addr
    mask.sin_addr.s_addr = UInt32(0xff00_0000).bigEndian          // IN_CLASSA_NET
    withUnsafeBytes(of: addr) { src in
        withUnsafeMutableBytes(of: &ifra.ifra_addr) { $0.copyMemory(from: src) }
    }
    withUnsafeBytes(of: mask) { src in
        withUnsafeMutableBytes(of: &ifra.ifra_mask) { $0.copyMemory(from: src) }
    }
    if nd_ioctl(s, ND_SIOCAIFADDR, &ifra) != 0 {
        warn("lo0: SIOCAIFADDR 127.0.0.1: \(errorString(__error().pointee))")
    }
}

func setUpLoopbackIPv6() {
    let s = socket(AF_INET6, SOCK_DGRAM, 0)
    guard s >= 0 else { return }
    defer { _ = close(s) }
    bringUp(s)
    var ifra = in6_aliasreq()
    setInterfaceName(&ifra.ifra_name)
    ifra.ifra_addr.sin6_len = UInt8(MemoryLayout<sockaddr_in6>.size)
    ifra.ifra_addr.sin6_family = sa_family_t(AF_INET6)
    withUnsafeMutableBytes(of: &ifra.ifra_addr.sin6_addr) { raw in      // in6addr_loopback, ::1
        for i in raw.indices { raw[i] = 0 }
        raw[15] = 1
    }
    ifra.ifra_prefixmask.sin6_len = UInt8(MemoryLayout<sockaddr_in6>.size)
    ifra.ifra_prefixmask.sin6_family = sa_family_t(AF_INET6)
    withUnsafeMutableBytes(of: &ifra.ifra_prefixmask.sin6_addr) { raw in
        for i in raw.indices { raw[i] = 0xff }
    }
    ifra.ifra_lifetime.ia6t_vltime = UInt32(ND6_INFINITE_LIFETIME)
    ifra.ifra_lifetime.ia6t_pltime = UInt32(ND6_INFINITE_LIFETIME)
    if nd_ioctl(s, ND_SIOCAIFADDR_IN6, &ifra) != 0 && __error().pointee != EEXIST {
        warn("lo0: SIOCAIFADDR_IN6 ::1: \(errorString(__error().pointee))")
    }
}

// MARK: - A job's sockets

/// The job's Sockets dictionary with each entry (a socket dictionary or an
/// array of them) replaced by an array of the descriptors created for it,
/// as launchctl-842's distill_config_file() does; nil if the job has none.
/// launchd hands them to the job at check-in (or to launchproxy, for an
/// inetdCompatibility job). Bonjour registration (the Bonjour key) needs
/// mDNSResponder's daemon, which NeoDarwin doesn't run, and is skipped, as
/// are SecureSocketWithKey and multicast groups.
func socketsData(_ job: Plist, label: String) -> launch_data_t? {
    guard case .dictionary(let entries) = job[LAUNCH_JOBKEY_SOCKETS] ?? .boolean(false) else { return nil }
    let sockets = launch_data_alloc(LAUNCH_DATA_DICTIONARY)!
    for entry in entries {
        let fds = launch_data_alloc(LAUNCH_DATA_ARRAY)!
        var count = 0
        switch entry.value {
        case .dictionary:
            count = createSockets(entry.value, into: fds, at: count, label: label)
        case .array(let items):
            for item in items { count = createSockets(item, into: fds, at: count, label: label) }
        default:
            break
        }
        _ = launch_data_dict_insert(sockets, fds, entry.key)
    }
    return sockets
}

/// Creates the sockets one socket dictionary describes, appending their
/// descriptors to `fds` from index `index`; returns the next index.
func createSockets(_ spec: Plist, into fds: launch_data_t, at index: Int, label: String) -> Int {
    var next = index
    var type = SOCK_STREAM
    if same(spec["SockType"], "dgram") { type = SOCK_DGRAM }
    if same(spec["SockType"], "seqpacket") { type = SOCK_SEQPACKET }
    let passive = spec["SockPassive"]?.boolValue ?? true
    func append(_ fd: Int32) {
        _ = launch_data_array_set_index(fds, launch_data_new_fd(fd)!, next)
        next += 1
    }
    if let path = spec["SockPathName"]?.stringValue {
        if let fd = unixSocket(path, type: type, passive: passive, mode: spec["SockPathMode"]?.integerValue, label: label) {
            append(fd)
        }
        return next
    }
    var hints = addrinfo()
    hints.ai_socktype = type
    if passive { hints.ai_flags |= AI_PASSIVE }
    if same(spec["SockFamily"], "IPv4") { hints.ai_family = AF_INET }
    if same(spec["SockFamily"], "IPv6") { hints.ai_family = AF_INET6 }
    if same(spec["SockProtocol"], "TCP") { hints.ai_protocol = IPPROTO_TCP }
    if same(spec["SockProtocol"], "UDP") { hints.ai_protocol = IPPROTO_UDP }
    let node = spec["SockNodeName"]?.stringValue
    var service = spec["SockServiceName"]?.stringValue
    if service == nil, let port = spec["SockServiceName"]?.integerValue { service = "\(port)" }
    var res0: UnsafeMutablePointer<addrinfo>?
    let gerr = withOptionalCString(node) { n in withOptionalCString(service) { s in getaddrinfo(n, s, &hints, &res0) } }
    guard gerr == 0 else {
        warn("\(label): getaddrinfo(): \(String(cString: gai_strerror(gerr)))")
        return next
    }
    defer { freeaddrinfo(res0) }
    var res = res0
    while let ai = res {
        res = ai.pointee.ai_next
        let fd = socket(ai.pointee.ai_family, ai.pointee.ai_socktype, ai.pointee.ai_protocol)
        guard fd >= 0 else {
            warn("\(label): socket(): \(errorString(__error().pointee))")
            continue
        }
        if passive {
            var on: Int32 = 1
            let size = socklen_t(MemoryLayout<Int32>.size)
            if ai.pointee.ai_family == AF_INET6 { _ = setsockopt(fd, IPPROTO_IPV6, IPV6_V6ONLY, &on, size) }
            _ = setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &on, size)
            guard bind(fd, ai.pointee.ai_addr, ai.pointee.ai_addrlen) == 0 else {
                warn("\(label): bind(): \(errorString(__error().pointee))")
                _ = close(fd)
                continue
            }
            if type != SOCK_DGRAM && listen(fd, -1) != 0 {
                warn("\(label): listen(): \(errorString(__error().pointee))")
                _ = close(fd)
                continue
            }
        } else if connect(fd, ai.pointee.ai_addr, ai.pointee.ai_addrlen) != 0 {
            warn("\(label): connect(): \(errorString(__error().pointee))")
            _ = close(fd)
            continue
        }
        append(fd)
    }
    return next
}

/// An AF_UNIX socket at `path`: bound (replacing a stale one, with a umask
/// of 077, then SockPathMode) and listening, or connected.
func unixSocket(_ path: String, type: Int32, passive: Bool, mode: Int64?, label: String) -> Int32? {
    var sun = sockaddr_un()
    sun.sun_family = sa_family_t(AF_UNIX)
    let bytes = Array(path.utf8)
    guard bytes.count < MemoryLayout.size(ofValue: sun.sun_path) else {
        warn("\(label): \(path): name too long")
        return nil
    }
    withUnsafeMutableBytes(of: &sun.sun_path) { raw in
        for (i, b) in bytes.enumerated() { raw[i] = b }
    }
    sun.sun_len = UInt8(MemoryLayout<sockaddr_un>.size)
    let fd = socket(AF_UNIX, type, 0)
    guard fd >= 0 else { return nil }
    let ok = withUnsafePointer(to: &sun) { p in
        p.withMemoryRebound(to: sockaddr.self, capacity: 1) { sa -> Bool in
            let len = socklen_t(MemoryLayout<sockaddr_un>.size)
            if !passive { return connect(fd, sa, len) == 0 }
            guard unlink(path) == 0 || __error().pointee == ENOENT else { return false }
            let old = umask(0o077)
            defer { _ = umask(old) }
            guard bind(fd, sa, len) == 0 else { return false }
            if let mode { _ = chmod(path, mode_t(mode)) }
            return type == SOCK_DGRAM || listen(fd, -1) == 0
        }
    }
    guard ok else {
        warn("\(label): \(path): \(errorString(__error().pointee))")
        _ = close(fd)
        return nil
    }
    return fd
}

/// A string value equal to `name`, ignoring case, as launchctl-842 compares
/// socket keys' values (strcasecmp).
func same(_ value: Plist?, _ name: String) -> Bool {
    guard let s = value?.stringValue else { return false }
    return strcasecmp(s, name) == 0
}

func withOptionalCString<R>(_ s: String?, _ body: (UnsafePointer<CChar>?) -> R) -> R {
    guard let s else { return body(nil) }
    return s.withCString { body($0) }
}
