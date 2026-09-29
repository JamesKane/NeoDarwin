// SPDX-License-Identifier: BSD-2-Clause
//
// The ACPI static tables neoboot turns into the device tree (DT-ABI v1,
// docs/kernel/dt-abi.md): the RSDP and XSDT, the MADT (GIC distributor,
// redistributors, one GICC per CPU), the GTDT (the virtual timer's
// interrupt), the SPCR (the console UART) and the FADT (OEM ids, PSCI).
// Offsets are from ACPI 6.5 §5.2 and the SPCR specification.
//
// Target-independent: no UEFI import and no heap. The same file builds into
// neoboot (Embedded Swift, -no-allocations), where the tables are read in
// place in physical memory, and into the host tool dtdump, where they come
// from an acpidump text file. Both reach table bytes through ACPIMemory.

/// Where table bytes come from.
protocol ACPIMemory {
    /// `count` readable bytes at physical `address`, or nil unless all of them are there.
    func bytes(at address: UInt64, count: Int) -> UnsafeRawPointer?
}

/// Why the tables cannot describe the machine. `signature` names the table
/// (0 for none) and `value` is a number worth printing with the message.
struct ACPIError: Error {
    let message: StaticString
    let signature: UInt32
    let value: UInt64

    init(_ message: StaticString, _ signature: UInt32 = 0, _ value: UInt64 = 0) {
        self.message = message
        self.signature = signature
        self.value = value
    }
}

/// Sixty-four elements stored inline, for tables that must not allocate.
struct Fixed64<Element: BitwiseCopyable> {
    typealias Eight = (Element, Element, Element, Element, Element, Element, Element, Element)
    static var capacity: Int { 64 }
    private var storage: (Eight, Eight, Eight, Eight, Eight, Eight, Eight, Eight)

    init(repeating v: Element) {
        let e: Eight = (v, v, v, v, v, v, v, v)
        storage = (e, e, e, e, e, e, e, e)
    }

    subscript(i: Int) -> Element {
        get {
            precondition(i >= 0 && i < Self.capacity)
            return withUnsafeBytes(of: storage) { $0.load(fromByteOffset: i * MemoryLayout<Element>.stride, as: Element.self) }
        }
        set {
            precondition(i >= 0 && i < Self.capacity)
            withUnsafeMutableBytes(of: &storage) { $0.storeBytes(of: newValue, toByteOffset: i * MemoryLayout<Element>.stride, as: Element.self) }
        }
    }
}

/// One MADT GICC structure (ACPI 6.5 §5.2.12.14).
struct ACPICPU: BitwiseCopyable {
    var mpidr: UInt64 = 0          // Aff3 [39:32], Aff2:Aff1:Aff0 [23:0]
    var gicrBase: UInt64 = 0       // 0 when a GICR structure describes the redistributors
    var uid: UInt32 = 0            // ACPI processor UID
    var flags: UInt32 = 0          // bit 0 Enabled, bit 3 Online Capable
    var pmuGSIV: UInt32 = 0        // performance-monitor interrupt; 0 if none

    var enabled: Bool { flags & 1 != 0 }
    var onlineCapable: Bool { flags & 8 != 0 }
    /// The value the kernel keeps as the CPU's physical id and the tree's cpu `reg`.
    var affinity: UInt32 { UInt32(truncatingIfNeeded: mpidr & 0xff_ffff) }
}

/// What neoboot needs from the tables.
struct ACPIFacts {
    // FADT (else XSDT) header: `model`.
    var oemID: (UInt8, UInt8, UInt8, UInt8, UInt8, UInt8) = (0, 0, 0, 0, 0, 0)
    var oemTableID: UInt64 = 0

    // MADT.
    var gicdBase: UInt64 = 0
    var gicVersion: UInt8 = 0
    var gicrBase: UInt64 = 0             // first redistributor frame
    var gicrRegionLength: UInt64 = 0     // what the MADT reserves (GICR structures), or the GICC span
    var gicrFromGICC = false             // per-CPU GICR base addresses rather than GICR structures
    var cpus = Fixed64(repeating: ACPICPU())
    var cpuEntries = 0                   // GICC structures recorded (at most 64)
    var gicCount = 0                     // GICC structures in the table, recorded or not

    // GTDT: the EL1 virtual timer.
    var timerGSIV: UInt32 = 0
    var timerFlags: UInt32 = 0           // bit 0 edge-triggered, bit 1 active-low

    // SPCR.
    var uartType: UInt8 = 0xff
    var uartBase: UInt64 = 0

    // FADT.
    var armBootFlags: UInt16 = 0         // bit 0 PSCI compliant, bit 1 PSCI uses HVC
    var hardwareReduced = false

    /// Bytes ACPI.relocate needs for the copy the kernel gets.
    var relocatedLength = 0

    var enabledCPUs: Int {
        var n = 0
        for i in 0..<cpuEntries where cpus[i].enabled { n += 1 }
        return n
    }

    /// The number of redistributor frames the MADT promises: one per GICC.
    var gicrFrames: Int { gicCount }
}

enum ACPI {
    static let headerLength = 36
    static let maxTableLength = 0x100_0000   // 16 MiB: anything larger is corrupt

    /// A signature as the little-endian u32 at offset 0.
    static func sig(_ s: StaticString) -> UInt32 {
        let p = s.utf8Start
        return UInt32(p[0]) | UInt32(p[1]) << 8 | UInt32(p[2]) << 16 | UInt32(p[3]) << 24
    }

    static let madt = sig("APIC"), gtdt = sig("GTDT"), spcr = sig("SPCR"), fadt = sig("FACP")
    static let xsdt = sig("XSDT"), dsdt = sig("DSDT"), facs = sig("FACS"), rsdp = sig("RSD ")

    // SPCR interface types (the DBG2 port subtypes).
    static let uart16550: UInt8 = 0x00, uart16450: UInt8 = 0x01, uartPL011: UInt8 = 0x03
    static let uartSBSA32: UInt8 = 0x0d, uartSBSA: UInt8 = 0x0e, uart16550GAS: UInt8 = 0x12

    /// Whether the kernel has a driver for an SPCR interface type: its PL011
    /// driver (pe_serial.c), which also drives the SBSA Generic UART, a
    /// register subset of the PL011. The 16550 family waits for P1-12.
    static func uartIsPL011(_ type: UInt8) -> Bool {
        type == uartPL011 || type == uartSBSA32 || type == uartSBSA
    }

    static func sum(_ p: UnsafeRawPointer, _ n: Int) -> UInt8 {
        var s: UInt8 = 0
        for i in 0..<n { s &+= p.load(fromByteOffset: i, as: UInt8.self) }
        return s
    }

    /// Makes the bytes sum to zero by rewriting the one at `at`.
    static func fixChecksum(_ p: UnsafeMutableRawPointer, _ n: Int, at: Int) {
        p.storeBytes(of: 0, toByteOffset: at, as: UInt8.self)
        p.storeBytes(of: 0 &- sum(p, n), toByteOffset: at, as: UInt8.self)
    }

    static func u16(_ p: UnsafeRawPointer, _ o: Int) -> UInt16 { p.loadUnaligned(fromByteOffset: o, as: UInt16.self) }
    static func u32(_ p: UnsafeRawPointer, _ o: Int) -> UInt32 { p.loadUnaligned(fromByteOffset: o, as: UInt32.self) }
    static func u64(_ p: UnsafeRawPointer, _ o: Int) -> UInt64 { p.loadUnaligned(fromByteOffset: o, as: UInt64.self) }

    /// A table with a standard header: all `Length` bytes present and summing to zero.
    static func table<M: ACPIMemory>(_ m: M, at address: UInt64, checksum: Bool = true) throws(ACPIError) -> (UnsafeRawPointer, Int) {
        guard address != 0 else { throw ACPIError("a table pointer is zero") }
        guard let h = m.bytes(at: address, count: 8) else { throw ACPIError("no table at", 0, address) }
        let signature = u32(h, 0)
        let length = Int(u32(h, 4))
        guard length >= (checksum ? headerLength : 8), length <= maxTableLength else {
            throw ACPIError("bad table length", signature, UInt64(length))
        }
        guard let p = m.bytes(at: address, count: length) else { throw ACPIError("table truncated at", signature, address) }
        guard !checksum || sum(p, length) == 0 else { throw ACPIError("bad checksum in the table at", signature, address) }
        return (p, length)
    }

    /// The RSDP's XSDT address, after checking both checksums (ACPI 6.5 §5.2.5.3).
    static func xsdtAddress<M: ACPIMemory>(_ m: M, rsdp address: UInt64) throws(ACPIError) -> UInt64 {
        guard let p = m.bytes(at: address, count: 36) else { throw ACPIError("no RSDP at", rsdp, address) }
        guard u64(p, 0) == 0x2052_5450_2044_5352 else { throw ACPIError("bad RSDP signature at", rsdp, address) }  // "RSD PTR "
        guard sum(p, 20) == 0 else { throw ACPIError("bad RSDP checksum", rsdp, address) }
        guard p.load(fromByteOffset: 15, as: UInt8.self) >= 2 else {
            throw ACPIError("ACPI 1.0 RSDP (no XSDT); SBBR requires ACPI 6", rsdp, UInt64(p.load(fromByteOffset: 15, as: UInt8.self)))
        }
        let length = Int(u32(p, 20))
        guard length >= 36, let q = m.bytes(at: address, count: length), sum(q, length) == 0 else {
            throw ACPIError("bad RSDP extended checksum", rsdp, address)
        }
        let x = u64(p, 24)
        guard x != 0 else { throw ACPIError("the RSDP has no XSDT", rsdp) }
        return x
    }

    /// The XSDT, checked, and its entry count.
    static func xsdtTable<M: ACPIMemory>(_ m: M, rsdp: UInt64) throws(ACPIError) -> (address: UInt64, bytes: UnsafeRawPointer, entries: Int) {
        let x = try xsdtAddress(m, rsdp: rsdp)
        let (xp, xl) = try table(m, at: x)
        guard u32(xp, 0) == xsdt else { throw ACPIError("the RSDP's XSDT pointer finds", u32(xp, 0), x) }
        return (x, xp, (xl - headerLength) / 8)
    }

    /// Calls `body` with every table reachable from the RSDP, in order: the
    /// RSDP, the XSDT, each XSDT entry, and after the FADT its DSDT and FACS.
    /// Every table except the FACS (which has no checksum) is checked.
    static func forEachTable<M: ACPIMemory>(
        _ m: M, rsdp: UInt64,
        _ body: (_ signature: UInt32, _ address: UInt64, _ bytes: UnsafeRawPointer, _ length: Int) -> Void
    ) throws(ACPIError) {
        let x = try xsdtTable(m, rsdp: rsdp)
        let rp = m.bytes(at: rsdp, count: 36)!
        body(Self.rsdp, rsdp, rp, Int(u32(rp, 20)))
        body(xsdt, x.address, x.bytes, Int(u32(x.bytes, 4)))
        for i in 0..<x.entries {
            let a = u64(x.bytes, headerLength + i * 8)
            let (tp, tl) = try table(m, at: a)
            body(u32(tp, 0), a, tp, tl)
            if u32(tp, 0) == fadt {
                if let d = fadtDSDT(tp, tl) {
                    let (dp, dl) = try table(m, at: d)
                    body(u32(dp, 0), d, dp, dl)
                }
                if let f = fadtFACS(tp, tl) {
                    let (fp, fl) = try table(m, at: f, checksum: false)
                    body(u32(fp, 0), f, fp, fl)
                }
            }
        }
    }

    /// The FADT's DSDT: X_DSDT (offset 140) when present, else DSDT (40).
    static func fadtDSDT(_ p: UnsafeRawPointer, _ n: Int) -> UInt64? {
        if n >= 148, u64(p, 140) != 0 { return u64(p, 140) }
        if n >= 44, u32(p, 40) != 0 { return UInt64(u32(p, 40)) }
        return nil
    }

    /// The FADT's FACS: X_FIRMWARE_CTRL (132) when present, else FIRMWARE_CTRL (36).
    static func fadtFACS(_ p: UnsafeRawPointer, _ n: Int) -> UInt64? {
        if n >= 140, u64(p, 132) != 0 { return u64(p, 132) }
        if n >= 40, u32(p, 36) != 0 { return UInt64(u32(p, 36)) }
        return nil
    }

    /// Reads the tables (docs/kernel/dt-abi.md, "Where the values come from").
    static func parse<M: ACPIMemory>(_ m: M, rsdp: UInt64) throws(ACPIError) -> ACPIFacts {
        var f = ACPIFacts()
        var sawMADT = false, sawGTDT = false, sawSPCR = false, sawFADT = false
        let x = try xsdtTable(m, rsdp: rsdp)
        copyOEM(x.bytes, into: &f)
        var relocated = 48 + ((Int(u32(x.bytes, 4)) + 15) & ~15)  // the RSDP and XSDT copies
        for i in 0..<x.entries {
            let (p, n) = try table(m, at: u64(x.bytes, headerLength + i * 8))
            relocated += (n + 15) & ~15
            let signature = u32(p, 0)
            switch signature {
            case fadt:
                guard !sawFADT else { throw ACPIError("more than one FADT") }
                sawFADT = true
                copyOEM(p, into: &f)
                if n >= 116 { f.hardwareReduced = u32(p, 112) & (1 << 20) != 0 }
                if n >= 131 { f.armBootFlags = u16(p, 129) }
                if let d = fadtDSDT(p, n) {
                    let (_, dl) = try table(m, at: d)
                    relocated += (dl + 15) & ~15
                }
            case madt:
                guard !sawMADT else { throw ACPIError("more than one MADT") }
                sawMADT = true
                try parseMADT(p, n, into: &f)
            case gtdt:
                sawGTDT = true
                guard n >= 72 else { throw ACPIError("GTDT too short for the virtual timer", signature, UInt64(n)) }
                f.timerGSIV = u32(p, 64)
                f.timerFlags = u32(p, 68)
            case spcr:
                sawSPCR = true
                guard n >= 52 else { throw ACPIError("SPCR too short for the base address", signature, UInt64(n)) }
                f.uartType = p.load(fromByteOffset: 36, as: UInt8.self)
                let space = p.load(fromByteOffset: 40, as: UInt8.self)
                guard space == 0 else { throw ACPIError("SPCR UART not in system memory; address space", signature, UInt64(space)) }
                f.uartBase = u64(p, 44)
            default:
                break
            }
        }
        f.relocatedLength = relocated
        guard sawMADT else { throw ACPIError("no MADT (APIC): the GIC and the CPUs are described nowhere") }
        guard sawGTDT else { throw ACPIError("no GTDT: the timer interrupt is described nowhere") }
        guard sawSPCR else { throw ACPIError("no SPCR: the console UART is described nowhere") }
        try check(f)
        return f
    }

    static func copyOEM(_ p: UnsafeRawPointer, into f: inout ACPIFacts) {
        withUnsafeMutableBytes(of: &f.oemID) { $0.copyMemory(from: UnsafeRawBufferPointer(start: p + 10, count: 6)) }
        f.oemTableID = u64(p, 16)
    }

    static func parseMADT(_ p: UnsafeRawPointer, _ n: Int, into f: inout ACPIFacts) throws(ACPIError) {
        guard n >= 44 else { throw ACPIError("MADT too short", madt, UInt64(n)) }
        var gicrEnd: UInt64 = 0
        var gicrSplit = false
        var o = 44
        while o < n {
            guard o + 2 <= n else { throw ACPIError("MADT structure header overruns the table at offset", madt, UInt64(o)) }
            let type = p.load(fromByteOffset: o, as: UInt8.self)
            let length = Int(p.load(fromByteOffset: o + 1, as: UInt8.self))
            guard length >= 2, o + length <= n else { throw ACPIError("MADT structure overruns the table at offset", madt, UInt64(o)) }
            switch type {
            case 0x0b:  // GICC
                guard length >= 76 else { throw ACPIError("MADT GICC structure too short", madt, UInt64(length)) }
                if f.cpuEntries < Fixed64<ACPICPU>.capacity {
                    f.cpus[f.cpuEntries] = ACPICPU(mpidr: u64(p, o + 68), gicrBase: u64(p, o + 60), uid: u32(p, o + 8),
                                                   flags: u32(p, o + 12), pmuGSIV: u32(p, o + 20))
                    f.cpuEntries += 1
                }
                f.gicCount += 1
            case 0x0c:  // GICD
                guard length >= 24 else { throw ACPIError("MADT GICD structure too short", madt, UInt64(length)) }
                guard f.gicdBase == 0 else { throw ACPIError("more than one GICD in the MADT") }
                f.gicdBase = u64(p, o + 8)
                f.gicVersion = p.load(fromByteOffset: o + 20, as: UInt8.self)
            case 0x0e:  // GICR: a discovery range of redistributor frames
                guard length >= 16 else { throw ACPIError("MADT GICR structure too short", madt, UInt64(length)) }
                let base = u64(p, o + 4), len = UInt64(u32(p, o + 12))
                if f.gicrRegionLength == 0 {
                    f.gicrBase = base
                    gicrEnd = base + len
                } else if base == gicrEnd {
                    gicrEnd += len
                } else {
                    gicrSplit = true  // a second, separate range: only the first is used
                }
                f.gicrRegionLength = gicrEnd - f.gicrBase
            default:
                break
            }
            o += length
        }
        guard f.gicdBase != 0 else { throw ACPIError("no GICD in the MADT") }
        guard f.gicCount > 0 else { throw ACPIError("no GICC (CPU) structures in the MADT") }
        let frames = UInt64(f.gicCount)
        if f.gicrRegionLength == 0 {
            // No GICR structure: each GICC names its CPU's frame. DT-ABI v1
            // describes one range, so the frames must be contiguous.
            guard f.cpuEntries == f.gicCount else { throw ACPIError("too many GICCs to check their redistributors", madt, frames) }
            var lo = UInt64.max, hi: UInt64 = 0
            for i in 0..<f.cpuEntries {
                let g = f.cpus[i].gicrBase
                guard g != 0 else { throw ACPIError("no GICR structure and a GICC without a GICR base; MPIDR", madt, f.cpus[i].mpidr) }
                guard g % Platform.gicrFrame == 0 else { throw ACPIError("misaligned GICR base address", madt, g) }
                for j in 0..<i where f.cpus[j].gicrBase == g { throw ACPIError("two GICCs share the GICR frame at", madt, g) }
                lo = min(lo, g)
                hi = max(hi, g + Platform.gicrFrame)
            }
            guard hi - lo == frames * Platform.gicrFrame else { throw ACPIError("the GICCs' redistributor frames are not contiguous; span", madt, hi - lo) }
            f.gicrBase = lo
            f.gicrRegionLength = hi - lo
            f.gicrFromGICC = true
        } else {
            guard frames * Platform.gicrFrame <= f.gicrRegionLength else {
                throw ACPIError(gicrSplit ? "the first GICR range is too small for every CPU's frame, and ranges are split; length"
                                          : "the GICR ranges are too small for one 128 KiB frame per GICC; length",
                                madt, f.gicrRegionLength)
            }
        }
    }

    /// The facts the kernel's assumptions rest on.
    static func check(_ f: ACPIFacts) throws(ACPIError) {
        // GICv4 redistributors are 256 KiB (VLPI frames); pe_fiq.c and
        // NeoDarwinGICv3 step through them in GICR_PE_SIZE, 128 KiB.
        guard f.gicVersion == 3 || f.gicVersion == 0 else {
            throw ACPIError("GIC version is not 3; the kernel walks 128 KiB GICv3 redistributor frames. Version", madt, UInt64(f.gicVersion))
        }
        guard f.enabledCPUs > 0 else { throw ACPIError("no enabled CPU in the MADT") }
        // pe_fiq.c routes the timer as PPI 27 (INTID 27) and nothing else.
        guard f.timerGSIV == Platform.timerPPI else {
            throw ACPIError("GTDT virtual timer is not PPI 27 (INTID), which pe_fiq.c hardcodes; GSIV", gtdt, UInt64(f.timerGSIV))
        }
        for i in 0..<f.cpuEntries where f.cpus[i].enabled {
            let pmu = f.cpus[i].pmuGSIV
            guard pmu == 0 || (pmu >= 16 && pmu < 32) else { throw ACPIError("GICC performance interrupt is not a PPI; GSIV", madt, UInt64(pmu)) }
            for j in 0..<i where f.cpus[j].enabled && f.cpus[j].affinity == f.cpus[i].affinity {
                throw ACPIError("two enabled GICCs have the same MPIDR", madt, f.cpus[i].mpidr)
            }
        }
        guard uartIsPL011(f.uartType) else {
            if f.uartType == uart16550 || f.uartType == uart16450 || f.uartType == uart16550GAS {
                throw ACPIError("SPCR names a 16550-family UART, which the kernel has no driver for until P1-12; interface type", spcr, UInt64(f.uartType))
            }
            throw ACPIError("SPCR names a UART the kernel has no driver for; interface type", spcr, UInt64(f.uartType))
        }
        guard f.uartBase != 0 else { throw ACPIError("SPCR UART base address is zero") }
    }

    /// Copies the tables into `dst`, which the kernel will reach at
    /// `physical`: the RSDP, the XSDT, every XSDT table and the DSDT, each
    /// 16-byte aligned, with the pointers between them rewritten and the
    /// checksums redone. The RSDT is dropped. The FACS stays where it is
    /// (the FADT keeps pointing at it): it is the firmware's, and
    /// hardware-reduced ACPI has none. Returns the bytes written, at most
    /// `capacity` (ACPIFacts.relocatedLength is enough).
    static func relocate<M: ACPIMemory>(_ m: M, rsdp: UInt64, into dst: UnsafeMutableRawPointer, physical: UInt64,
                                        capacity: Int) throws(ACPIError) -> Int {
        var c = Copier(dst: dst, capacity: capacity)
        let x = try xsdtTable(m, rsdp: rsdp)
        let r = try c.place(m.bytes(at: rsdp, count: 36)!, 36)
        let xc = try c.place(x.bytes, Int(u32(x.bytes, 4)))
        for i in 0..<x.entries {
            let (p, n) = try table(m, at: u64(x.bytes, headerLength + i * 8))
            let at = try c.place(p, n)
            (dst + xc).storeBytes(of: physical + UInt64(at), toByteOffset: headerLength + i * 8, as: UInt64.self)
            if u32(p, 0) == fadt, let d = fadtDSDT(p, n) {
                let (dp, dl) = try table(m, at: d)
                let new = physical + UInt64(try c.place(dp, dl))
                let f = dst + at
                if n >= 148 { f.storeBytes(of: new, toByteOffset: 140, as: UInt64.self) }
                if n >= 44 { f.storeBytes(of: new <= 0xffff_ffff ? UInt32(new) : 0, toByteOffset: 40, as: UInt32.self) }
                fixChecksum(f, n, at: 9)
            }
        }
        fixChecksum(dst + xc, Int(u32(dst + xc, 4)), at: 9)
        let rc = dst + r
        rc.storeBytes(of: UInt32(0), toByteOffset: 16, as: UInt32.self)   // RsdtAddress
        rc.storeBytes(of: UInt32(36), toByteOffset: 20, as: UInt32.self)  // Length
        rc.storeBytes(of: physical + UInt64(xc), toByteOffset: 24, as: UInt64.self)
        fixChecksum(rc, 20, at: 8)
        fixChecksum(rc, 36, at: 32)
        return c.cursor
    }

    struct Copier {
        let dst: UnsafeMutableRawPointer
        let capacity: Int
        var cursor = 0

        /// Copies `n` bytes to the next 16-byte boundary; returns their offset.
        mutating func place(_ p: UnsafeRawPointer, _ n: Int) throws(ACPIError) -> Int {
            let at = cursor
            guard at + n <= capacity else { throw ACPIError("the ACPI copy does not fit; bytes available", 0, UInt64(capacity)) }
            (dst + at).copyMemory(from: p, byteCount: n)
            cursor = (at + n + 15) & ~15
            return at
        }
    }
}
