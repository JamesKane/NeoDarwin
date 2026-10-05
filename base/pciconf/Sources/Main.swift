// SPDX-License-Identifier: BSD-2-Clause
//
// pciconf: FreeBSD's PCI listing over the I/O Registry (P4-21 checkpoint
// 6b, docs/architecture/freebsd-parity.md §2.1). FreeBSD reads /dev/pci;
// NeoDarwin has IOPCIFamily, whose IOPCIDevice nubs carry what PCIOCGETCONF
// returns as properties (IOPCIConfigurator::constructProperties):
// vendor-id, device-id, revision-id, class-code, subsystem-vendor-id and
// subsystem-id (each a little-endian 32-bit OSData), and reg, whose first
// cell is the Open Firmware phys.hi word (bus in bits 16-23, device in
// 11-15, function in 8-10). The domain is the host bridge's pci-segment
// (NeoDarwinPCIHostBridge). The driver is the nub's first child in the
// service plane, named by its class, with a unit numbered per class in
// listing order; FreeBSD's driver names and units are its own.
//
// Supported: -l (with -v, which adds the class and subclass names; the
// vendor and device names need FreeBSD's pci_vendors database, which isn't
// installed), a device selector, -C class, and -a. Everything that reads
// configuration space (-r, -w, -b, -c, -e, -B, -V, -D, -R, -W) needs a user
// client IOPCIFamily doesn't have on NeoDarwin (Apple's is in the closed
// AppleACPIPlatform), and fails saying so. -t (the device tree) is ioreg's.
// Embedded Swift (language policy T1 tool, built as T3) over NDIOKit.

import NDIOKit

struct Function {
    var domain: UInt32 = 0
    var bus: UInt32 = 0
    var slot: UInt32 = 0
    var function: UInt32 = 0
    var vendor: UInt32 = 0
    var device: UInt32 = 0
    var subvendor: UInt32 = 0
    var subdevice: UInt32 = 0
    var classCode: UInt32 = 0
    var revision: UInt32 = 0
    var header: UInt32 = 0
    var driver: String = ""   // class name of the attached driver, or ""
    var name: String = ""     // driver and unit, or none<N>

    var selector: String { "pci\(domain):\(bus):\(slot):\(function)" }
    var baseClass: UInt32 { classCode >> 16 }
    var subClass: UInt32 { (classCode >> 8) & 0xff }
    var sortKey: UInt64 {
        UInt64(domain) << 32 | UInt64(bus) << 16 | UInt64(slot) << 8 | UInt64(function)
    }
}

func warn(_ message: String) {
    nd_warn("pciconf: " + message)
}

func fail(_ message: String) -> Never {
    warn(message)
    exit(1)
}

func hex(_ value: UInt32, _ width: Int) -> String {
    var s = String(value, radix: 16)
    while s.count < width { s = "0" + s }
    return s
}

/// A little-endian integer from an OSData property (up to 4 bytes), or nil.
func dataWord(_ entry: nd_io_t, _ key: String) -> UInt32? {
    guard let cf = nd_io_property(entry, key) else { return nil }
    defer { nd_cf_release(cf) }
    guard nd_cf_kind(cf) == ND_CF_DATA, let p = nd_cf_data_bytes(cf) else { return nil }
    let n = min(nd_cf_data_length(cf), 4)
    var v: UInt32 = 0
    for i in 0..<n { v |= UInt32(p[i]) << (8 * UInt32(i)) }
    return v
}

func numberProperty(_ cf: nd_cf_t) -> Int64? {
    var v: Int64 = 0
    return nd_cf_kind(cf) == ND_CF_NUMBER && nd_cf_number_value(cf, &v) ? v : nil
}

/// The class name of the nub's first child in the service plane: its driver.
func driverName(_ entry: nd_io_t) -> String {
    let it = nd_io_child_iterator(entry)
    guard it != 0 else { return "" }
    defer { nd_io_release(it) }
    let child = nd_io_iterator_next(it)
    guard child != 0 else { return "" }
    defer { nd_io_release(child) }
    var buf = [CChar](repeating: 0, count: 128)
    guard nd_io_class_name(child, &buf, buf.count) else { return "" }
    return buf.withUnsafeBufferPointer { String(cString: $0.baseAddress!) }
}

func readFunctions() -> [Function] {
    let it = nd_io_matching_services("IOPCIDevice")
    guard it != 0 else { fail("can't search the I/O Registry for IOPCIDevice") }
    defer { nd_io_release(it) }
    var result: [Function] = []
    while true {
        let entry = nd_io_iterator_next(it)
        if entry == 0 { break }
        defer { nd_io_release(entry) }
        guard let vendor = dataWord(entry, "vendor-id"), let reg = dataWord(entry, "reg") else { continue }
        var f = Function()
        f.vendor = vendor & 0xffff
        f.device = (dataWord(entry, "device-id") ?? 0xffff) & 0xffff
        f.classCode = (dataWord(entry, "class-code") ?? 0) & 0xffffff
        f.revision = (dataWord(entry, "revision-id") ?? 0) & 0xff
        f.subvendor = (dataWord(entry, "subsystem-vendor-id") ?? 0) & 0xffff
        f.subdevice = (dataWord(entry, "subsystem-id") ?? 0) & 0xffff
        f.bus = (reg >> 16) & 0xff
        f.slot = (reg >> 11) & 0x1f
        f.function = (reg >> 8) & 0x7
        if let seg = nd_io_search_property(entry, "pci-segment", true) {
            if let v = numberProperty(seg) { f.domain = UInt32(truncatingIfNeeded: v) }
            nd_cf_release(seg)
        }
        f.driver = driverName(entry)
        result.append(f)
    }
    result.sort { $0.sortKey < $1.sortKey }
    // The header type isn't published: 1 for a PCI-PCI bridge, 2 for a
    // CardBus bridge, 0 otherwise, with the multi-function bit when another
    // function of the same device is present.
    for i in result.indices {
        let c = result[i].classCode >> 8
        result[i].header = c == 0x0604 || c == 0x0609 ? 1 : c == 0x0607 ? 2 : 0
        if result.contains(where: {
            $0.domain == result[i].domain && $0.bus == result[i].bus && $0.slot == result[i].slot
                && $0.function != result[i].function
        }) {
            result[i].header |= 0x80
        }
    }
    var units: [(String, Int)] = []
    var none = 0
    for i in result.indices {
        let d = result[i].driver
        if d.isEmpty {
            result[i].name = "none\(none)"
            none += 1
        } else if let j = units.firstIndex(where: { $0.0 == d }) {
            result[i].name = "\(d)\(units[j].1)"
            units[j].1 += 1
        } else {
            result[i].name = "\(d)0"
            units.append((d, 1))
        }
    }
    return result
}

// FreeBSD's pci_nomatch_tab: class, subclass (-1 for the class), name.
let classNames: [(UInt32, Int32, StaticString)] = [
    (0x00, -1, "old"), (0x00, 0x00, "non-VGA display device"), (0x00, 0x01, "VGA-compatible display device"),
    (0x01, -1, "mass storage"), (0x01, 0x00, "SCSI"), (0x01, 0x01, "ATA"), (0x01, 0x02, "floppy disk"),
    (0x01, 0x03, "IPI"), (0x01, 0x04, "RAID"), (0x01, 0x05, "ATA (ADMA)"), (0x01, 0x06, "SATA"),
    (0x01, 0x07, "SAS"), (0x01, 0x08, "NVM"), (0x01, 0x09, "UFS"),
    (0x02, -1, "network"), (0x02, 0x00, "ethernet"), (0x02, 0x01, "token ring"), (0x02, 0x02, "fddi"),
    (0x02, 0x03, "ATM"), (0x02, 0x04, "ISDN"), (0x02, 0x05, "WorldFip"), (0x02, 0x06, "PICMG"),
    (0x02, 0x07, "InfiniBand"), (0x02, 0x08, "host fabric"),
    (0x03, -1, "display"), (0x03, 0x00, "VGA"), (0x03, 0x01, "XGA"), (0x03, 0x02, "3D"),
    (0x04, -1, "multimedia"), (0x04, 0x00, "video"), (0x04, 0x01, "audio"), (0x04, 0x02, "telephony"),
    (0x04, 0x03, "HDA"),
    (0x05, -1, "memory"), (0x05, 0x00, "RAM"), (0x05, 0x01, "flash"),
    (0x06, -1, "bridge"), (0x06, 0x00, "HOST-PCI"), (0x06, 0x01, "PCI-ISA"), (0x06, 0x02, "PCI-EISA"),
    (0x06, 0x03, "PCI-MCA"), (0x06, 0x04, "PCI-PCI"), (0x06, 0x05, "PCI-PCMCIA"), (0x06, 0x06, "PCI-NuBus"),
    (0x06, 0x07, "PCI-CardBus"), (0x06, 0x08, "PCI-RACEway"), (0x06, 0x09, "Semi-transparent PCI-to-PCI"),
    (0x06, 0x0a, "InfiniBand-PCI"), (0x06, 0x0b, "AdvancedSwitching-PCI"),
    (0x07, -1, "simple comms"), (0x07, 0x00, "UART"), (0x07, 0x01, "parallel port"),
    (0x07, 0x02, "multiport serial"), (0x07, 0x03, "generic modem"),
    (0x08, -1, "base peripheral"), (0x08, 0x00, "interrupt controller"), (0x08, 0x01, "DMA controller"),
    (0x08, 0x02, "timer"), (0x08, 0x03, "realtime clock"), (0x08, 0x04, "PCI hot-plug controller"),
    (0x08, 0x05, "SD host controller"), (0x08, 0x06, "IOMMU"), (0x08, 0x07, "Root Complex Event Collector"),
    (0x09, -1, "input device"), (0x09, 0x00, "keyboard"), (0x09, 0x01, "digitizer"), (0x09, 0x02, "mouse"),
    (0x09, 0x03, "scanner"), (0x09, 0x04, "gameport"),
    (0x0a, -1, "docking station"), (0x0b, -1, "processor"),
    (0x0c, -1, "serial bus"), (0x0c, 0x00, "FireWire"), (0x0c, 0x01, "AccessBus"), (0x0c, 0x02, "SSA"),
    (0x0c, 0x03, "USB"), (0x0c, 0x04, "Fibre Channel"), (0x0c, 0x05, "SMBus"), (0x0c, 0x06, "InfiniBand"),
    (0x0c, 0x07, "IPMI"), (0x0c, 0x08, "SERCOS"), (0x0c, 0x09, "CANbus"), (0x0c, 0x0a, "MIPI I3C"),
    (0x0d, -1, "wireless controller"), (0x0d, 0x00, "iRDA"), (0x0d, 0x01, "IR"), (0x0d, 0x10, "RF"),
    (0x0d, 0x11, "bluetooth"), (0x0d, 0x12, "broadband"), (0x0d, 0x20, "ethernet 802.11a"),
    (0x0d, 0x21, "ethernet 802.11b"), (0x0d, 0x40, "cellular controller/modem"),
    (0x0d, 0x41, "cellular controller/modem plus ethernet"),
    (0x0e, -1, "intelligent I/O controller"), (0x0e, 0x00, "I2O"),
    (0x0f, -1, "satellite communication"), (0x0f, 0x01, "sat TV"), (0x0f, 0x02, "sat audio"),
    (0x0f, 0x03, "sat voice"), (0x0f, 0x04, "sat data"),
    (0x10, -1, "encrypt/decrypt"), (0x10, 0x00, "network/computer crypto"), (0x10, 0x10, "entertainment crypto"),
    (0x11, -1, "dasp"), (0x11, 0x00, "DPIO module"), (0x11, 0x01, "performance counters"),
    (0x11, 0x10, "communication synchronizer"), (0x11, 0x20, "signal processing management"),
    (0x12, -1, "processing accelerators"), (0x12, 0x00, "processing accelerators"),
    (0x13, -1, "non-essential instrumentation"),
]

func className(_ f: Function) -> String? {
    classNames.first { $0.0 == f.baseClass && $0.1 == -1 }.map { "\($0.2)" }
}

func subclassName(_ f: Function) -> String? {
    classNames.first { $0.0 == f.baseClass && $0.1 == Int32(f.subClass) }.map { "\($0.2)" }
}

/// FreeBSD's selectors: [name@]pci[D:]B:S[:F][:] (or without "pci"), or a name and unit.
func matches(_ f: Function, _ sel: String) -> Bool {
    if sel == f.name || sel == f.name + ":" { return true }
    var s = Substring(sel)
    if let at = s.firstIndex(of: "@") { s = s[s.index(after: at)...] }
    if s.hasPrefix("pci") { s = s.dropFirst(3) }
    if s.hasSuffix(":") { s = s.dropLast() }
    let parts = s.split(separator: ":", omittingEmptySubsequences: false)
    var nums: [UInt32] = []
    for p in parts {
        guard let n = UInt32(p) else { return false }
        nums.append(n)
    }
    switch nums.count {
    case 2: return f.domain == 0 && f.bus == nums[0] && f.slot == nums[1] && f.function == 0
    case 3: return f.domain == 0 && f.bus == nums[0] && f.slot == nums[1] && f.function == nums[2]
    case 4: return f.domain == nums[0] && f.bus == nums[1] && f.slot == nums[2] && f.function == nums[3]
    default: return false
    }
}

func usage() -> Never {
    nd_warn("usage: pciconf -l [-v] [-C class] [device]\n       pciconf -a device")
    exit(1)
}

func unsupported(_ option: String) -> Never {
    fail("-\(option): configuration space access is not supported on NeoDarwin (IOPCIFamily has no user client for it)")
}

@_cdecl("main")
func pciconfMain(_ argc: Int32, _ argv: UnsafeMutablePointer<UnsafeMutablePointer<CChar>?>) -> Int32 {
    var args: [String] = []
    for i in 1..<Int(argc) {
        if let p = argv[i] { args.append(String(cString: p)) }
    }
    var list = false, attached = false, verbose = false
    var classFilter: String? = nil
    var operands: [String] = []
    var i = 0
    while i < args.count {
        let a = args[i]
        i += 1
        if a == "--" { operands += args[i...]; break }
        guard a.hasPrefix("-"), a.count > 1 else { operands.append(a); continue }
        var opts = Array(a.utf8.dropFirst())
        while !opts.isEmpty {
            let c = opts.removeFirst()
            switch c {
            case UInt8(ascii: "l"): list = true
            case UInt8(ascii: "a"): attached = true
            case UInt8(ascii: "v"): verbose = true
            case UInt8(ascii: "C"):
                if !opts.isEmpty {
                    classFilter = String(decoding: opts, as: UTF8.self)
                    opts = []
                } else if i < args.count {
                    classFilter = args[i]
                    i += 1
                } else {
                    usage()
                }
            case UInt8(ascii: "r"), UInt8(ascii: "w"), UInt8(ascii: "b"), UInt8(ascii: "c"), UInt8(ascii: "e"),
                UInt8(ascii: "B"), UInt8(ascii: "V"), UInt8(ascii: "D"), UInt8(ascii: "R"), UInt8(ascii: "W"),
                UInt8(ascii: "x"), UInt8(ascii: "h"):
                unsupported(String(decoding: [c], as: UTF8.self))
            case UInt8(ascii: "t"):
                fail("-t: use ioreg -c IOPCIDevice for the device tree")
            default:
                usage()
            }
        }
    }
    if list == attached || operands.count > 1 || (attached && operands.count != 1) { usage() }
    let functions = readFunctions()
    if attached {
        guard let f = functions.first(where: { matches($0, operands[0]) }) else {
            fail("\(operands[0]): no such device")
        }
        print("\(operands[0]): \(f.driver.isEmpty ? "not " : "")attached")
        return f.driver.isEmpty ? 2 : 0
    }
    if let c = classFilter, !classNames.contains(where: { $0.1 == -1 && "\($0.2)".lowercased() == c.lowercased() }) {
        fail("Invalid class name")
    }
    var found = false
    for f in functions {
        if let sel = operands.first, !matches(f, sel) { continue }
        if let c = classFilter, className(f)?.lowercased() != c.lowercased() { continue }
        found = true
        print("\(f.name)@\(f.selector):\tclass=0x\(hex(f.classCode, 6)) rev=0x\(hex(f.revision, 2)) "
            + "hdr=0x\(hex(f.header, 2)) vendor=0x\(hex(f.vendor, 4)) device=0x\(hex(f.device, 4)) "
            + "subvendor=0x\(hex(f.subvendor, 4)) subdevice=0x\(hex(f.subdevice, 4))")
        if verbose {
            if let c = className(f) { print("    class      = \(c)") }
            if let s = subclassName(f) { print("    subclass   = \(s)") }
        }
    }
    if let sel = operands.first, !found { fail("\(sel): no such device") }
    return 0
}
