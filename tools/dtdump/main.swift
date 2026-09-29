// SPDX-License-Identifier: BSD-2-Clause
//
// dtdump: build the device tree neoboot would give the kernel from a
// machine's ACPI tables, print it, and check it against DT-ABI v1
// (docs/kernel/dt-abi.md). The ACPI parser, the tree synthesis and the check
// are neoboot's own files (boot/neoboot/Sources/Portable), compiled for the
// host, so a table dump from any board shows what neoboot would boot it with.
//
// Usage:
//     dtdump [OPTIONS] ACPIDUMP      tables in acpidump text format (neoboot's
//                                    dump-acpi, or Linux acpidump)
//     dtdump --dt TREE               an Apple-format tree, e.g. from --write-dt
// Options (what the loader knows and ACPI does not; defaults resemble QEMU virt):
//     --dram-base HEX    /chosen dram-base (default 0x40000000)
//     --dram-size HEX    /chosen dram-size (default 0x80000000)
//     --timebase HZ      counter frequency, CNTFRQ_EL0 (default 62500000)
//     --boot-mpidr HEX   the boot CPU (default: the first enabled GICC)
//     --seed HEX         random-seed generator seed (default 1)
//     --acpi-base HEX    where the ACPI copy goes (default dram-base + 32 MiB)
//     --ramdisk HEX,HEX  a ramdisk's address and length (default none)
//     --gicd-ctlr HEX    the GICD_CTLR value neoboot reads, which chooses the
//                        timer's group (default 0x40: DS=1, as QEMU virt without EL3)
//     --timer-group N    force /arm-io/gic timer-group, as boot.cfg's timer-group=N
//     --write-dt FILE    also write the binary tree
// Exits 1 on an ACPI error or a DT-ABI violation, 2 on a usage error.

import Foundation

func fail(_ message: String, code: Int32 = 1) -> Never {
    FileHandle.standardError.write(Data("dtdump: \(message)\n".utf8))
    exit(code)
}

func hex(_ v: UInt64) -> String { "0x" + String(v, radix: 16) }

func sigString(_ s: UInt32) -> String {
    String(decoding: (0..<4).map { UInt8(truncatingIfNeeded: s >> ($0 * 8)) }, as: UTF8.self)
}

func describe(_ e: ACPIError) -> String {
    var s = "ACPI: \(e.message)"
    if e.signature != 0 { s += " [\(sigString(e.signature))]" }
    if e.value != 0 { s += " \(hex(e.value))" }
    return s
}

struct PrintReport: DTReport {
    var lines: [String] = []
    mutating func violation(_ rule: StaticString, _ value: UInt64, hasValue: Bool) {
        lines.append("DT-ABI v1 violation: \(rule)" + (hasValue ? " \(hex(value))" : ""))
    }
}

// MARK: arguments

let usage = "usage: dtdump [--dram-base HEX] [--dram-size HEX] [--timebase HZ] [--boot-mpidr HEX] [--seed HEX] [--acpi-base HEX] [--ramdisk HEX,HEX] [--gicd-ctlr HEX] [--timer-group 0|1] [--write-dt FILE] ACPIDUMP | dtdump --dt TREE"
var args = Array(CommandLine.arguments.dropFirst())
var input: String?
var treeInput: String?
var writeTree: String?
var dramBase: UInt64 = 0x4000_0000
var dramSize: UInt64 = 0x8000_0000
var timebase: UInt64 = 62_500_000
var bootMPIDR: UInt64?
var seed: UInt64 = 1
var acpiBase: UInt64?
var ramdisk: (UInt64, UInt64) = (0, 0)
var gicdCTLR: UInt32 = Platform.gicdCTLRDS
var forcedTimerGroup: UInt32?

func number(_ s: String) -> UInt64 {
    let t = s.hasPrefix("0x") ? String(s.dropFirst(2)) : s
    guard let v = UInt64(t, radix: s.hasPrefix("0x") ? 16 : 10) else { fail("not a number: \(s)", code: 2) }
    return v
}

while !args.isEmpty {
    let a = args.removeFirst()
    func value() -> String {
        guard !args.isEmpty else { fail(usage, code: 2) }
        return args.removeFirst()
    }
    switch a {
    case "--dram-base": dramBase = number(value())
    case "--dram-size": dramSize = number(value())
    case "--timebase": timebase = number(value())
    case "--boot-mpidr": bootMPIDR = number(value())
    case "--seed": seed = number(value())
    case "--acpi-base": acpiBase = number(value())
    case "--ramdisk":
        let parts = value().split(separator: ",").map { number(String($0)) }
        guard parts.count == 2 else { fail(usage, code: 2) }
        ramdisk = (parts[0], parts[1])
    case "--gicd-ctlr": gicdCTLR = UInt32(truncatingIfNeeded: number(value()))
    case "--timer-group":
        let g = number(value())
        guard g <= 1 else { fail(usage, code: 2) }
        forcedTimerGroup = UInt32(g)
    case "--write-dt": writeTree = value()
    case "--dt": treeInput = value()
    default:
        guard input == nil, !a.hasPrefix("--") else { fail(usage, code: 2) }
        input = a
    }
}

// MARK: printing a tree

func printable(_ v: UnsafeRawBufferPointer) -> Bool {
    guard v.count >= 2, v[v.count - 1] == 0 else { return false }
    for i in 0..<v.count - 1 where !(v[i] == 0 || (v[i] >= 0x20 && v[i] < 0x7f)) { return false }
    return v[0] != 0
}

func format(_ v: UnsafeRawBufferPointer) -> String {
    if v.isEmpty { return "(empty)" }
    if printable(v) {
        return v.split(separator: 0).map { "\"" + String(decoding: $0, as: UTF8.self) + "\"" }.joined(separator: ", ")
    }
    if v.count % 8 == 0 && v.count > 4 {
        return "<" + (0..<v.count / 8).map { hex(v.loadUnaligned(fromByteOffset: $0 * 8, as: UInt64.self)) }.joined(separator: " ") + "> (u64)"
    }
    if v.count % 4 == 0 {
        return "<" + (0..<v.count / 4).map { hex(UInt64(v.loadUnaligned(fromByteOffset: $0 * 4, as: UInt32.self))) }.joined(separator: " ") + ">"
    }
    return "[" + v.map { String($0, radix: 16) }.joined(separator: " ") + "]"
}

func nodeName(_ n: DTNode) -> String {
    guard let v = n.property("name"), printable(v) else { return "?" }
    return String(decoding: v.prefix { $0 != 0 }, as: UTF8.self)
}

func printNode(_ n: DTNode, path: String, into out: inout String, count: inout Int) {
    count += 1
    out += path + "\n"
    n.forEachProperty { name, value in
        let key = String(decoding: UnsafeRawBufferPointer(start: name, count: DTNode.nameLength).prefix { $0 != 0 }, as: UTF8.self)
        if key == "name" { return }
        out += "    \(key) = \(format(value))\n"
    }
    n.forEachChild { c in
        printNode(c, path: (path == "/" ? "" : path) + "/" + nodeName(c), into: &out, count: &count)
    }
}

/// Prints and checks a tree; returns the violations (checks of its own included).
func show(_ tree: UnsafeRawPointer, _ length: Int, extra: [String]) -> Int {
    var report = PrintReport()
    let n = DTCheck.check(tree, length: length, &report)
    var out = "device tree: \(length) bytes\n"
    var nodeCount = 0
    if DTNode(tree: tree, length: length, offset: 0).end() != nil {
        printNode(DTNode(tree: tree, length: length, offset: 0), path: "/", into: &out, count: &nodeCount)
    }
    print(out, terminator: "")
    for l in report.lines + extra { print("dtdump: \(l)") }
    let total = n + extra.count
    print(total == 0 ? "dtdump: DT-ABI v1: ok (\(nodeCount) nodes)" : "dtdump: DT-ABI v1: \(total) violation(s)")
    return total
}

// MARK: --dt: check an existing tree

if let treeInput {
    guard input == nil, let data = FileManager.default.contents(atPath: treeInput) else { fail(usage, code: 2) }
    let bytes = [UInt8](data)
    let violations = bytes.withUnsafeBytes { show($0.baseAddress!, $0.count, extra: []) }
    exit(violations == 0 ? 0 : 1)
}

// MARK: ACPI → tree

guard let input else { fail(usage, code: 2) }
guard let text = try? String(contentsOfFile: input, encoding: .utf8) else { fail("cannot read \(input)") }
let dump: AcpiDump
do {
    dump = try AcpiDump(text: text)
} catch {
    fail("\(input):\(error.line): \(error.message)")
}
guard let rsdp = dump.rsdp else { fail("\(input): no RSDP in the dump") }
print("acpi: \(input): \(dump.tables.count) tables: " + dump.tables.map { $0.signature }.joined(separator: " "))

let facts: ACPIFacts
do throws(ACPIError) {
    facts = try ACPI.parse(dump, rsdp: rsdp)
} catch {
    print("dtdump: \(describe(error))")
    fail("the tables do not describe a machine neoboot can boot")
}

let boot = bootMPIDR ?? (0..<facts.cpuEntries).map { facts.cpus[$0] }.first { $0.enabled }!.mpidr
let layout: Platform.Layout
do throws(ACPIError) {
    layout = try Platform.layout(facts, bootMPIDR: boot)
} catch {
    print("dtdump: \(describe(error))")
    fail("the tables do not describe a machine neoboot can boot")
}

let oem = withUnsafeBytes(of: facts.oemID) { String(decoding: $0, as: UTF8.self) }
let oemTable = withUnsafeBytes(of: facts.oemTableID) { String(decoding: $0, as: UTF8.self) }
print("acpi: OEM \"\(oem)\" \"\(oemTable)\", hardware-reduced \(facts.hardwareReduced), PSCI "
      + ((facts.armBootFlags & 1) == 0 ? "absent" : (facts.armBootFlags & 2) != 0 ? "HVC" : "SMC"))
print("acpi: MADT: GICv\(facts.gicVersion == 0 ? 3 : facts.gicVersion) GICD \(hex(facts.gicdBase)); GICR \(hex(facts.gicrBase)), "
      + "\(facts.gicrFrames) frame(s) of \(hex(facts.gicrRegionLength)) reserved" + (facts.gicrFromGICC ? " (from GICC entries)" : ""))
for i in 0..<facts.cpuEntries {
    let c = facts.cpus[i]
    print("acpi: MADT: GICC uid \(c.uid) MPIDR \(hex(c.mpidr)) flags \(hex(UInt64(c.flags))) PMU GSIV \(c.pmuGSIV)"
          + (c.gicrBase != 0 ? " GICR \(hex(c.gicrBase))" : "") + (i == layout.bootIndex ? " (boot)" : ""))
}
print("acpi: GTDT: virtual timer GSIV \(facts.timerGSIV) flags \(hex(UInt64(facts.timerFlags)))")
print("acpi: SPCR: interface type \(hex(UInt64(facts.uartType))) at \(hex(facts.uartBase))")
let timerGroup = Platform.timerGroup(gicdCTLR: gicdCTLR, forced: forcedTimerGroup)
print("gic: GICD_CTLR \(hex(UInt64(gicdCTLR))) (DS=\(gicdCTLR & Platform.gicdCTLRDS != 0 ? 1 : 0)): timer on Group \(timerGroup)"
      + (forcedTimerGroup != nil ? ", forced" : ""))

// The copy of the tables the kernel gets, and a check that it reads the same.
let base = acpiBase ?? dramBase + 0x200_0000
let copy = UnsafeMutableRawPointer.allocate(byteCount: facts.relocatedLength, alignment: 16)
copy.initializeMemory(as: UInt8.self, repeating: 0, count: facts.relocatedLength)
var extra: [String] = []
let copyLength: Int
do throws(ACPIError) {
    copyLength = try ACPI.relocate(dump, rsdp: rsdp, into: copy, physical: base, capacity: facts.relocatedLength)
    let again = try ACPI.parse(LayeredMemory(copyAddress: base, copy: copy, copyLength: copyLength, dump: dump), rsdp: base)
    if again.gicCount != facts.gicCount || again.gicdBase != facts.gicdBase || again.gicrBase != facts.gicrBase
        || again.uartBase != facts.uartBase || again.timerGSIV != facts.timerGSIV || again.enabledCPUs != facts.enabledCPUs {
        extra.append("cross-check: the relocated tables read differently from the originals")
    }
} catch {
    print("dtdump: relocated tables: \(describe(error))")
    fail("the relocated ACPI tables are broken")
}
print("acpi: relocated copy: \(copyLength) bytes at \(hex(base))")

let capacity = 0x1_0000
let treeBuffer = UnsafeMutableRawPointer.allocate(byteCount: capacity, alignment: 16)
treeBuffer.initializeMemory(as: UInt8.self, repeating: 0, count: capacity)
var writer = DeviceTreeWriter(base: treeBuffer, capacity: capacity)
let loader = Platform.Facts(dramBase: dramBase, dramSize: dramSize, timebase: timebase, bootMPIDR: boot, seed: seed,
                            ramdiskBase: ramdisk.0, ramdiskSize: ramdisk.1, acpiBase: base, acpiLength: UInt64(copyLength),
                            timerGroup: timerGroup)
guard let treeLength = Platform.deviceTree(into: &writer, loader, facts, layout) else { fail("the tree does not fit in \(capacity) bytes") }

// Checks that need the ACPI facts as well as the tree.
let root = DTNode(tree: treeBuffer, length: treeLength, offset: 0)
if let cpus = root.child(named: "cpus") {
    var n = 0
    cpus.forEachChild { _ in n += 1 }
    if n != min(facts.enabledCPUs, Platform.maxCPUs) {
        extra.append("cross-check: \(n) cpu nodes for \(facts.enabledCPUs) enabled MADT GICCs")
    }
}
if let io = root.child(named: "arm-io"), let soc = io.u64("ranges", 1) {
    if let gic = io.child(named: "gic") {
        if (gic.u64("reg", 0) ?? 0) + soc != facts.gicdBase { extra.append("cross-check: /arm-io/gic GICD is not the MADT's") }
        if (gic.u64("reg", 2) ?? 0) + soc != facts.gicrBase { extra.append("cross-check: /arm-io/gic GICR is not the MADT's") }
        if (gic.u64("reg", 3) ?? 0) / Platform.gicrFrame != UInt64(facts.gicrFrames) {
            extra.append("cross-check: /arm-io/gic GICR frames are not one per MADT GICC")
        }
        if gic.u32("timer-ppi") != facts.timerGSIV { extra.append("cross-check: /arm-io/gic timer-ppi is not the GTDT's virtual timer") }
    }
    if let uart = io.child(named: "uart0"), (uart.u64("reg", 0) ?? 0) + soc != facts.uartBase {
        extra.append("cross-check: /arm-io/uart0 is not the SPCR UART")
    }
}

if let writeTree {
    guard FileManager.default.createFile(atPath: writeTree, contents: Data(bytes: treeBuffer, count: treeLength)) else {
        fail("cannot write \(writeTree)")
    }
}
exit(show(treeBuffer, treeLength, extra: extra) == 0 ? 0 : 1)
