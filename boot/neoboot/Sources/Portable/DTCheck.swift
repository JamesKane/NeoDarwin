// SPDX-License-Identifier: BSD-2-Clause
//
// Reading an Apple flattened device tree (pexpert/pexpert/device_tree.h),
// and checking one against DT-ABI v1 (docs/kernel/dt-abi.md). neoboot runs
// the check on the tree it has just written and refuses to boot a tree that
// fails it; dtdump runs it on the tree it synthesises from an acpidump.
// Target-independent and allocation-free.

/// One node of a flattened tree: {u32 nProperties; u32 nChildren}, the
/// properties {char name[32]; u32 length; value padded to 4 bytes}, then
/// the children. Every read is bounds-checked against the tree's length.
struct DTNode {
    static let nameLength = 32
    let tree: UnsafeRawPointer
    let length: Int
    let offset: Int

    var propertyCount: Int { Int(tree.loadUnaligned(fromByteOffset: offset, as: UInt32.self)) }
    var childCount: Int { Int(tree.loadUnaligned(fromByteOffset: offset + 4, as: UInt32.self)) }

    /// Properties start at offset + 8; the kernel ignores the top bit of a
    /// length (a placeholder flag in Apple's trees).
    static func propertyLength(_ tree: UnsafeRawPointer, _ at: Int) -> Int {
        Int(tree.loadUnaligned(fromByteOffset: at + nameLength, as: UInt32.self) & 0x7fff_ffff)
    }

    /// The offset of the first child, or nil if the properties overrun the tree.
    var childrenOffset: Int? {
        var at = offset + 8
        guard at <= length else { return nil }
        for _ in 0..<propertyCount {
            guard at + Self.nameLength + 4 <= length else { return nil }
            let n = Self.propertyLength(tree, at)
            at += Self.nameLength + 4 + ((n + 3) & ~3)
            guard at <= length else { return nil }
        }
        return at
    }

    /// The offset just past this node and its subtree, or nil if malformed.
    func end(depth: Int = 0) -> Int? {
        guard depth < 16, offset + 8 <= length, var at = childrenOffset else { return nil }
        for _ in 0..<childCount {
            guard let e = DTNode(tree: tree, length: length, offset: at).end(depth: depth + 1) else { return nil }
            at = e
        }
        return at
    }

    /// Calls `body` with each property's name (NUL-padded, 32 bytes) and value.
    func forEachProperty(_ body: (UnsafeRawPointer, UnsafeRawBufferPointer) -> Void) {
        var at = offset + 8
        for _ in 0..<propertyCount {
            let n = Self.propertyLength(tree, at)
            body(tree + at, UnsafeRawBufferPointer(start: tree + at + Self.nameLength + 4, count: n))
            at += Self.nameLength + 4 + ((n + 3) & ~3)
        }
    }

    /// Calls `body` with each child. The node must have passed end().
    func forEachChild(_ body: (DTNode) -> Void) {
        guard var at = childrenOffset else { return }
        for _ in 0..<childCount {
            let child = DTNode(tree: tree, length: length, offset: at)
            body(child)
            guard let e = child.end() else { return }
            at = e
        }
    }

    static func nameMatches(_ p: UnsafeRawPointer, _ name: StaticString) -> Bool {
        let n = name.utf8CodeUnitCount
        guard n < nameLength else { return false }
        for i in 0..<n where p.load(fromByteOffset: i, as: UInt8.self) != name.utf8Start[i] { return false }
        return p.load(fromByteOffset: n, as: UInt8.self) == 0
    }

    func property(_ name: StaticString) -> UnsafeRawBufferPointer? {
        var at = offset + 8
        for _ in 0..<propertyCount {
            let n = Self.propertyLength(tree, at)
            if Self.nameMatches(tree + at, name) {
                return UnsafeRawBufferPointer(start: tree + at + Self.nameLength + 4, count: n)
            }
            at += Self.nameLength + 4 + ((n + 3) & ~3)
        }
        return nil
    }

    func u32(_ name: StaticString) -> UInt32? {
        guard let v = property(name), v.count == 4 else { return nil }
        return v.loadUnaligned(as: UInt32.self)
    }

    func u64(_ name: StaticString) -> UInt64? {
        guard let v = property(name), v.count == 8 else { return nil }
        return v.loadUnaligned(as: UInt64.self)
    }

    /// The `index`th u64 of a property, if it has that many.
    func u64(_ name: StaticString, _ index: Int) -> UInt64? {
        guard let v = property(name), v.count >= (index + 1) * 8 else { return nil }
        return v.loadUnaligned(fromByteOffset: index * 8, as: UInt64.self)
    }

    /// Whether a property is exactly the NUL-terminated string `s`.
    func string(_ name: StaticString, is s: StaticString) -> Bool {
        guard let v = property(name), v.count == s.utf8CodeUnitCount + 1, v[s.utf8CodeUnitCount] == 0 else { return false }
        for i in 0..<s.utf8CodeUnitCount where v[i] != s.utf8Start[i] { return false }
        return true
    }

    /// A NUL-terminated, non-empty string property.
    func isString(_ name: StaticString) -> Bool {
        guard let v = property(name), v.count >= 2, v[v.count - 1] == 0 else { return false }
        for i in 0..<v.count - 1 where v[i] == 0 { return false }
        return true
    }

    func child(named name: StaticString) -> DTNode? {
        var found: DTNode? = nil
        forEachChild { c in
            if found == nil, c.string("name", is: name) { found = c }
        }
        return found
    }
}

/// Where check() reports violations.
protocol DTReport {
    /// One violated rule; `value` is worth printing when `hasValue`.
    mutating func violation(_ rule: StaticString, _ value: UInt64, hasValue: Bool)
}

extension DTReport {
    mutating func violation(_ rule: StaticString) { violation(rule, 0, hasValue: false) }
    mutating func violation(_ rule: StaticString, _ value: UInt64) { violation(rule, value, hasValue: true) }
}

enum DTCheck {
    /// Checks a tree against DT-ABI v1; returns the number of violations.
    /// The rules are listed, with their reasons, in docs/kernel/dt-abi.md.
    static func check<R: DTReport>(_ tree: UnsafeRawPointer, length: Int, _ r: inout R) -> Int {
        var count = Counter(inner: r)
        run(tree, length, &count)
        r = count.inner
        return count.n
    }

    struct Counter<R: DTReport>: DTReport {
        var inner: R
        var n = 0
        mutating func violation(_ rule: StaticString, _ value: UInt64, hasValue: Bool) {
            n += 1
            inner.violation(rule, value, hasValue: hasValue)
        }
    }

    static func run<R: DTReport>(_ tree: UnsafeRawPointer, _ length: Int, _ r: inout R) {
        let root = DTNode(tree: tree, length: length, offset: 0)
        guard length >= 8, let end = root.end() else { r.violation("the tree is malformed: a node or property overruns it"); return }
        if end != length { r.violation("the tree's length is not its root node's; bytes past the root", UInt64(length - end)) }
        checkNames(root, &r)
        checkPhandles(root, root, &r)

        // /
        if !root.string("name", is: "device-tree") { r.violation("/ name is not \"device-tree\"") }
        if !root.string("compatible", is: "NeoDarwin,sbsa") { r.violation("/ compatible is not \"NeoDarwin,sbsa\" (NeoDarwinPlatformExpert matches it)") }
        if !root.isString("model") { r.violation("/ has no model string") }
        if !root.isString("target-type") { r.violation("/ has no target-type string") }
        if root.u32("#address-cells") != 2 || root.u32("#size-cells") != 2 { r.violation("/ #address-cells and #size-cells are not 2") }

        // /chosen
        var dramBase: UInt64 = 0, dramEnd: UInt64 = 0
        if let chosen = root.child(named: "chosen") {
            if let b = chosen.u64("dram-base"), let s = chosen.u64("dram-size"), s != 0 {
                dramBase = b
                dramEnd = b + s
                if b % 0x4000 != 0 { r.violation("/chosen dram-base is not 16 KiB aligned", b) }
            } else {
                r.violation("/chosen dram-base or dram-size missing, not a u64, or zero (arm_init.c panics)")
            }
            if (chosen.property("random-seed")?.count ?? 0) < 64 { r.violation("/chosen random-seed is shorter than 64 bytes") }
            if chosen.u32("debug-enabled") == nil { r.violation("/chosen debug-enabled is not a u32") }
            if !chosen.isString("firmware-version") { r.violation("/chosen has no firmware-version string") }
            let rsdp = chosen.u64("acpi-rsdp")
            let tables = chosen.property("acpi-tables")
            if rsdp == nil || rsdp == 0 { r.violation("/chosen acpi-rsdp missing or zero") }
            if tables?.count != 16 { r.violation("/chosen acpi-tables is not (u64 address, u64 length)") }
            if let rsdp, let t0 = chosen.u64("acpi-tables", 0), let t1 = chosen.u64("acpi-tables", 1),
               rsdp < t0 || rsdp + 36 > t0 + t1 {
                r.violation("/chosen acpi-rsdp is not inside acpi-tables", rsdp)
            }
            if let map = chosen.child(named: "memory-map") {
                map.forEachProperty { name, value in
                    if DTNode.nameMatches(name, "name") { return }
                    if value.count != 16 {
                        r.violation("/chosen/memory-map entry is not (u64 address, u64 length)")
                        return
                    }
                    let a = value.loadUnaligned(as: UInt64.self), n = value.loadUnaligned(fromByteOffset: 8, as: UInt64.self)
                    if n == 0 || a < dramBase || a + n > dramEnd {
                        r.violation("/chosen/memory-map entry is empty or outside [dram-base, dram-base + dram-size); address", a)
                    }
                }
                if map.u64("ACPITables", 0) != chosen.u64("acpi-tables", 0) || map.u64("ACPITables", 1) != chosen.u64("acpi-tables", 1)
                    || map.property("ACPITables")?.count != 16 {
                    r.violation("/chosen/memory-map ACPITables is not /chosen acpi-tables")
                }
                if let rd = map.property("RAMDisk"), rd.count == 16, rd.loadUnaligned(fromByteOffset: 8, as: UInt64.self) % 0x4000 != 0 {
                    r.violation("/chosen/memory-map RAMDisk length is not a multiple of 16 KiB")
                }
            } else {
                r.violation("no /chosen/memory-map")
            }
        } else {
            r.violation("no /chosen (arm_init.c panics)")
        }

        // /arm-io and its devices
        var socSize: UInt64 = 0
        var gicPhandle: UInt32? = nil
        var gicdOffset: UInt64? = nil, gicrSize: UInt64 = 0
        if let io = root.child(named: "arm-io") {
            if !io.string("device_type", is: "soc") { r.violation("/arm-io device_type is not \"soc\"") }
            if let ranges = io.property("ranges"), ranges.count == 24 {
                if ranges.loadUnaligned(fromByteOffset: 8, as: UInt64.self) == 0 {
                    r.violation("/arm-io ranges[1] is 0, which pe_identify_machine reads as no SoC")
                }
                socSize = ranges.loadUnaligned(fromByteOffset: 16, as: UInt64.self)
            } else {
                r.violation("/arm-io ranges is not (child, parent, size) as three u64")
            }
            if io.u32("#address-cells") != 2 || io.u32("#size-cells") != 2 { r.violation("/arm-io #address-cells and #size-cells are not 2") }
            io.forEachChild { c in
                guard let reg = c.property("reg") else { return }
                if reg.count == 0 || reg.count % 16 != 0 { r.violation("an /arm-io reg is not (u64 offset, u64 size) pairs") ; return }
                for k in 0..<reg.count / 16 {
                    let o = reg.loadUnaligned(fromByteOffset: k * 16, as: UInt64.self)
                    let s = reg.loadUnaligned(fromByteOffset: k * 16 + 8, as: UInt64.self)
                    if s == 0 || o + s > socSize { r.violation("an /arm-io reg is empty or outside ranges; offset", o) }
                }
            }
            if let gic = io.child(named: "gic") {
                if !gic.string("compatible", is: "arm,gic-v3") { r.violation("/arm-io/gic compatible is not \"arm,gic-v3\"") }
                if gic.property("interrupt-controller") == nil { r.violation("/arm-io/gic has no interrupt-controller") }
                if gic.u32("#interrupt-cells") != 1 { r.violation("/arm-io/gic #interrupt-cells is not 1") }
                gicPhandle = gic.u32("AAPL,phandle")
                if gic.property("reg")?.count == 32 {
                    gicdOffset = gic.u64("reg", 0)
                    if (gic.u64("reg", 1) ?? 0) < 0x1_0000 { r.violation("/arm-io/gic GICD size is under 64 KiB") }
                    gicrSize = gic.u64("reg", 3) ?? 0
                    if gicrSize == 0 || gicrSize % Platform.gicrFrame != 0 { r.violation("/arm-io/gic GICR size is not a whole number of 128 KiB frames", gicrSize) }
                } else {
                    r.violation("/arm-io/gic reg is not (GICD offset, size, GICR offset, size) as four u64 (pe_fiq.c panics)")
                }
            } else {
                r.violation("no /arm-io/gic (pe_fiq.c panics)")
            }
            if let ic = io.child(named: "interrupt-controller") {
                if !ic.string("interrupt-controller", is: "master") { r.violation("/arm-io/interrupt-controller is not \"master\"") }
                if ic.u64("reg", 0) == nil || ic.u64("reg", 0) != gicdOffset { r.violation("/arm-io/interrupt-controller reg is not the GICD") }
            } else {
                r.violation("no /arm-io/interrupt-controller (without it ml_init_timebase is skipped)")
            }
            var timers = 0
            io.forEachChild { c in
                if c.string("device_type", is: "timer") {
                    timers += 1
                    if c.property("reg")?.count != 16 { r.violation("the timer node's reg is not one (offset, size) pair") }
                }
            }
            if timers != 1 { r.violation("not exactly one /arm-io node with device_type \"timer\"; found", UInt64(timers)) }
        } else {
            r.violation("no /arm-io")
        }

        // /defaults serial-device → a PL011 under /arm-io
        if let defaults = root.child(named: "defaults"), let p = defaults.u32("serial-device") {
            let (uart, n) = findPhandle(root, p)
            if n != 1 || uart == nil {
                r.violation("/defaults serial-device names no node; phandle", UInt64(p))
            } else if let uart {
                if !uart.string("compatible", is: "arm,pl011") { r.violation("the serial device is not compatible \"arm,pl011\", the kernel's only UART driver") }
                if uart.property("reg")?.count != 16 { r.violation("the serial device reg is not one (offset, size) pair (pe_serial.c asserts)") }
            }
        } else {
            r.violation("no /defaults serial-device (the kernel would have no console)")
        }

        // /cpus
        if let cpus = root.child(named: "cpus") {
            if cpus.u32("#address-cells") != 1 || cpus.u32("#size-cells") != 0 { r.violation("/cpus #address-cells is not 1 or #size-cells is not 0") }
            var n = 0, running = 0
            cpus.forEachChild { c in
                n += 1
                if !c.string("device_type", is: "cpu") { r.violation("a /cpus child's device_type is not \"cpu\"") }
                guard let reg = c.u32("reg") else { r.violation("a cpu reg is not a u32 (ml_parse_cpu_topology: mandatory)"); return }
                var same = 0
                cpus.forEachChild { d in if d.u32("reg") == reg { same += 1 } }
                if same != 1 { r.violation("two cpus share reg (MPIDR)", UInt64(reg)) }
                if c.string("state", is: "running") {
                    running += 1
                } else if !c.string("state", is: "waiting") {
                    r.violation("a cpu state is neither \"running\" nor \"waiting\"; reg", UInt64(reg))
                }
                if (c.u32("timebase-frequency") ?? 0) == 0 { r.violation("a cpu timebase-frequency is not a non-zero u32; reg", UInt64(reg)) }
                if gicPhandle == nil || c.u32("interrupt-parent") != gicPhandle { r.violation("a cpu interrupt-parent is not the GIC; reg", UInt64(reg)) }
                if let irq = c.property("interrupts"), irq.count == 12 {
                    let ipi = irq.loadUnaligned(as: UInt32.self), pmi = irq.loadUnaligned(fromByteOffset: 4, as: UInt32.self)
                    let deferred = irq.loadUnaligned(fromByteOffset: 8, as: UInt32.self)
                    if ipi >= 16 || deferred >= 16 || ipi == deferred { r.violation("cpu interrupts 0 and 2 are not two distinct SGIs; reg", UInt64(reg)) }
                    if pmi < 16 || pmi >= 32 { r.violation("cpu interrupt 1 (PMI) is not a PPI; reg", UInt64(reg)) }
                } else {
                    r.violation("a cpu interrupts is not three one-cell specifiers (AppleARMSMP's IPI form); reg", UInt64(reg))
                }
                if c.u32("AAPL,phandle") == nil { r.violation("a cpu has no AAPL,phandle; reg", UInt64(reg)) }
            }
            if running != 1 { r.violation("not exactly one cpu is \"running\" (the boot CPU); running", UInt64(running)) }
            if n == 0 || n > Platform.maxCPUs { r.violation("the number of cpus is not 1 to MAX_CPUS (32)", UInt64(n)) }
            // One redistributor frame per CPU. A tree capped at MAX_CPUS keeps
            // every frame the MADT names.
            if gicrSize != 0 {
                let frames = gicrSize / Platform.gicrFrame
                if n < Platform.maxCPUs ? frames != UInt64(n) : frames < UInt64(n) {
                    r.violation("GICR frames are not one per cpu; frames", frames)
                }
            }
        } else {
            r.violation("no /cpus")
        }
    }

    /// Every node has a NUL-terminated name.
    static func checkNames<R: DTReport>(_ n: DTNode, _ r: inout R) {
        if !n.isString("name") { r.violation("a node has no name string") }
        n.forEachChild { c in checkNames(c, &r) }
    }

    /// Every AAPL,phandle is a u32 and unique in the tree.
    static func checkPhandles<R: DTReport>(_ root: DTNode, _ n: DTNode, _ r: inout R) {
        if let v = n.property("AAPL,phandle") {
            if v.count != 4 {
                r.violation("an AAPL,phandle is not a u32")
            } else if findPhandle(root, v.loadUnaligned(as: UInt32.self)).count != 1 {
                r.violation("AAPL,phandle is not unique", UInt64(v.loadUnaligned(as: UInt32.self)))
            }
        }
        n.forEachChild { c in checkPhandles(root, c, &r) }
    }

    /// The node with a phandle, and how many nodes have it.
    static func findPhandle(_ n: DTNode, _ p: UInt32) -> (node: DTNode?, count: Int) {
        var found: DTNode? = nil
        var count = 0
        if n.u32("AAPL,phandle") == p {
            found = n
            count = 1
        }
        n.forEachChild { c in
            let (f, k) = findPhandle(c, p)
            if found == nil { found = f }
            count += k
        }
        return (found, count)
    }
}
