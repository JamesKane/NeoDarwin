// SPDX-License-Identifier: BSD-2-Clause
//
// The machine description neoboot hands the kernel as an Apple device tree,
// synthesised from the ACPI facts (ACPI.swift) and the loader's own
// (placement, counter frequency, boot CPU, seed, clock). The contract is
// DT-ABI v1, docs/kernel/dt-abi.md: every node and property below is listed
// there with the kernel code that reads it, and DTCheck.swift checks a tree
// against it. Target-independent, like ACPI.swift, so dtdump builds the same
// tree on the host.

enum Platform {
    /// MAX_CPUS in the SBSA board config (board_config.h): ml_parse_cpu_topology
    /// asserts on more.
    static let maxCPUs = 32
    /// GICR_PE_SIZE (SBSA.h): one GICv3 redistributor, RD_base + SGI_base.
    static let gicrFrame: UInt64 = 0x2_0000
    static let gicdSize: UInt64 = 0x1_0000
    /// The UART's register window, /arm-io/uart0 reg's size: the PL011's
    /// 4 KiB, or a GENI serial engine's 16 KiB (FreeBSD's uc_range).
    static func uartSize(_ type: UInt8) -> UInt64 {
        ACPI.uartIsGENI(type) ? 0x4000 : 0x1000
    }

    /// The kernel driver's DT compatible (pe_serial.c driver_setup_functions)
    /// for an SPCR interface type it drives.
    static func uartCompatible(_ type: UInt8) -> StaticString {
        ACPI.uartIsGENI(type) ? geniCompatible : pl011Compatible
    }
    static let pl011Compatible: StaticString = "arm,pl011"
    static let geniCompatible: StaticString = "qcom,geni-debug-uart"  // Linux's name for the GENI debug UART
    /// /arm-io ranges granularity.
    static let socAlignment: UInt64 = 0x1_0000
    /// GICD_CTLR.DS (Disable Security). It reads as 1 when the GIC has one
    /// security state, or when firmware has disabled the second. With two
    /// (DS == 0) a Non-secure read sees 0 there, Group 0 is Secure, and its
    /// interrupts go to EL3.
    static let gicdCTLRDS: UInt32 = 1 << 6
    // GIC INTIDs the CPU nodes name, in AppleARMSMP.cpp's three-entry order:
    // IPI, PMI, deferred IPI. The IPIs are SGIs. The PMI is the MADT's
    // performance interrupt, or SBSA's PPI 7 (INTID 23) when the MADT gives
    // none; in the three-entry form AppleARMSMP registers only the IPIs.
    static let sgiIPI: UInt32 = 0
    static let sgiDeferredIPI: UInt32 = 1
    static let ppiPMUDefault: UInt32 = 23

    // AAPL,phandle values. CPUs are cpuPhandleBase + their /cpus index.
    static let rootPhandle: UInt32 = 1, chosenPhandle: UInt32 = 2, uartPhandle: UInt32 = 3, armIOPhandle: UInt32 = 4
    static let gicPhandle: UInt32 = 5, interruptControllerPhandle: UInt32 = 6, timerPhandle: UInt32 = 7
    static let cpuPhandleBase: UInt32 = 16

    /// What the loader knows that ACPI does not.
    struct Facts {
        var dramBase: UInt64
        var dramSize: UInt64
        var timebase: UInt64
        var bootMPIDR: UInt64
        var seed: UInt64
        var ramdiskBase: UInt64 = 0  // physical; 0 when there is none
        var ramdiskSize: UInt64 = 0
        var utcSeconds: UInt64 = 0   // 0 when the firmware has no clock
        var utcCounter: UInt64 = 0   // CNTVCT when utcSeconds was read
        var acpiBase: UInt64 = 0     // the relocated tables; the RSDP is first
        var acpiLength: UInt64 = 0
        var timerGroup: UInt32 = 0   // /arm-io/gic timer-group (Platform.timerGroup)
        var psciConduit = PSCIConduit.absent  // /chosen psci-conduit (Platform.psciConduit)
        var bootUUID: UUID16? = nil  // /chosen boot-uuid: the root's GPT partition GUID; none with a ramdisk
        var trustCacheBase: UInt64 = 0  // physical: the static trust cache segment (TrustCache.swift); 0 when there is none
        var trustCacheSize: UInt64 = 0
    }

    /// How the kernel reaches PSCI, /chosen psci-conduit: an SMC to EL3
    /// firmware, or an HVC to a hypervisor. Without one the kernel can't
    /// start secondary CPUs, and neoboot boots one (cpus=1).
    enum PSCIConduit: UInt8 { case absent, smc, hvc }

    /// The conduit and why, for the log.
    struct PSCIChoice {
        var conduit: PSCIConduit
        var why: StaticString
    }

    /// FADT ARM_BOOT_ARCH (ACPI 6.5 table 5.10): PSCI_COMPLIANT, PSCI_USE_HVC.
    static let armBootPSCICompliant: UInt16 = 1 << 0
    static let armBootPSCIUseHVC: UInt16 = 1 << 1

    /// The PSCI conduit, from the firmware at run time, as Linux chooses it
    /// (drivers/firmware/psci): the FADT's ARM_BOOT_ARCH flags. A FADT that
    /// reports no PSCI on a CPU that implements EL3 (ID_AA64PFR0_EL1.EL3)
    /// gets SMC: QEMU virt with TF-A is like this, since QEMU describes only
    /// its own PSCI emulation, which it turns off when firmware owns EL3
    /// (docs/kernel/qemu-secure.md). HVC needs a hypervisor above the
    /// kernel, so it is refused when the firmware left EL2 to the loader:
    /// neoboot enters the kernel at EL1 and leaves nothing at EL2 to answer.
    static func psciConduit(armBootFlags: UInt16, el3: Bool, loaderEL: UInt64) -> PSCIChoice {
        if armBootFlags & armBootPSCICompliant != 0 {
            if armBootFlags & armBootPSCIUseHVC == 0 {
                return PSCIChoice(conduit: .smc, why: "SMC, as the FADT says")
            }
            if loaderEL == 2 {
                return PSCIChoice(conduit: .absent, why: "none: the FADT says HVC, but the loader runs at EL2, so no hypervisor would answer")
            }
            return PSCIChoice(conduit: .hvc, why: "HVC, as the FADT says")
        }
        if el3 {
            return PSCIChoice(conduit: .smc, why: "SMC: the FADT reports none, but the CPU implements EL3 (its firmware, e.g. TF-A)")
        }
        return PSCIChoice(conduit: .absent, why: "none: the FADT reports none and the CPU has no EL3")
    }

    /// The timer's GIC group, /arm-io/gic timer-group: Group 0 (a FIQ, the
    /// kernel's original path) when the GIC lets the Non-secure world use
    /// it, i.e. GICD_CTLR.DS reads 1; Group 1 (an IRQ) when it has two
    /// security states and Group 0 belongs to EL3. `forced` is the
    /// command line's timer-group=, which overrides that.
    static func timerGroup(gicdCTLR: UInt32, forced: UInt32?) -> UInt32 {
        if let forced { return forced }
        return gicdCTLR & gicdCTLRDS != 0 ? 0 : 1
    }

    /// What becomes of the SPCR UART. Only `.console` puts it in the tree
    /// (/arm-io/uart0, /defaults serial-device); otherwise the kernel's
    /// console is the framebuffer alone, and without one the loader refuses.
    enum UARTUse: UInt8 {
        case console      // a PL011, SBSA Generic or Qualcomm GENI UART: the serial console
        case off          // boot.cfg says uart=off
        case missing      // no SPCR
        case noDriver     // an interface type the kernel has no driver for (the 16550 family)
        case unusable     // not in system memory, or at address 0
    }

    static func uartUse(_ a: ACPIFacts, off: Bool) -> UARTUse {
        if off { return .off }
        if !a.hasSPCR { return .missing }
        if a.uartSpace == 0 && !ACPI.uartHasDriver(a.uartType) { return .noDriver }
        return ACPI.uartProblem(a) == nil ? .console : .unusable
    }

    /// Where the devices go in the tree, from the ACPI facts.
    struct Layout {
        var uart = true              // /arm-io/uart0 and /defaults serial-device (UARTUse.console)
        var socBase: UInt64 = 0      // /arm-io ranges[1]; every reg under /arm-io is relative to it
        var socSize: UInt64 = 0
        var gicrSize: UInt64 = 0
        var bootIndex = 0            // the boot CPU's GICC
        var cpuNodes = 0             // CPUs in /cpus
        var droppedCPUs = 0          // enabled CPUs past maxCPUs

        /// Whether the GICC at `i` gets a /cpus node: every enabled CPU, the
        /// boot CPU always, up to maxCPUs.
        func listed(_ a: ACPIFacts, _ i: Int) -> Bool {
            guard a.cpus[i].enabled else { return false }
            if i == bootIndex { return true }
            var before = 0
            for j in 0..<i where j != bootIndex && a.cpus[j].enabled { before += 1 }
            return before < maxCPUs - 1
        }
    }

    /// `uart`: whether the SPCR UART is in the tree (uartUse is .console).
    static func layout(_ a: ACPIFacts, bootMPIDR: UInt64, uart: Bool = true) throws(ACPIError) -> Layout {
        var l = Layout()
        l.uart = uart
        let boot = UInt32(truncatingIfNeeded: bootMPIDR & 0xff_ffff)
        var found = false
        for i in 0..<a.cpuEntries where a.cpus[i].enabled && a.cpus[i].affinity == boot {
            l.bootIndex = i
            found = true
        }
        guard found else { throw ACPIError("the boot CPU is not an enabled GICC in the MADT; MPIDR", ACPI.madt, bootMPIDR) }
        for i in 0..<a.cpuEntries where l.listed(a, i) { l.cpuNodes += 1 }
        l.droppedCPUs = a.enabledCPUs - l.cpuNodes
        l.gicrSize = UInt64(a.gicrFrames) * gicrFrame

        // ranges: the smallest aligned window over the devices, so that
        // nothing depends on one board's address map. ranges[1] must not be
        // 0: pe_identify_machine and serial_init read 0 as "no SoC".
        var lo = min(a.gicdBase, a.gicrBase), hi = max(a.gicdBase + gicdSize, a.gicrBase + l.gicrSize)
        if uart {
            lo = min(lo, a.uartBase)
            hi = max(hi, a.uartBase + uartSize(a.uartType))
        }
        lo &= ~(socAlignment - 1)
        guard lo != 0 else { throw ACPIError("a device at physical page 0 would make /arm-io ranges[1] zero, which the kernel reads as no SoC") }
        l.socBase = lo
        l.socSize = (hi - lo + socAlignment - 1) & ~(socAlignment - 1)
        return l
    }

    /// Writes the tree; returns its length, or nil if it did not fit.
    static func deviceTree(into w: inout DeviceTreeWriter, _ f: Facts, _ a: ACPIFacts, _ l: Layout) -> Int? {
        w.begin()  // /
        w.property("name", string: "device-tree")
        w.property("compatible", string: "NeoDarwin,sbsa")  // NeoDarwinPlatformExpert's IONameMatch
        modelProperty(&w, a)
        w.property("target-type", string: "sbsa")
        w.property("#address-cells", u32: 2)
        w.property("#size-cells", u32: 2)
        w.property("AAPL,phandle", u32: rootPhandle)

        w.begin()  // /chosen
        w.property("name", string: "chosen")
        w.property("dram-base", u64: f.dramBase)  // arm_init.c: panics if absent
        w.property("dram-size", u64: f.dramSize)
        w.property("debug-enabled", u32: 1)
        w.property("firmware-version", string: "neoboot-0.1")
        w.property("random-seed", length: 64) { v in  // pe_gen.c: seeds the early PRNG
            var x = f.seed | 1
            for i in 0..<8 {
                x ^= x << 13; x ^= x >> 7; x ^= x << 17  // xorshift64
                v.storeBytes(of: x, toByteOffset: i * 8, as: UInt64.self)
            }
        }
        if f.utcSeconds != 0 {  // NeoDarwinPlatformExpert: the time of day, and IORTC
            w.property("neodarwin,utc-seconds", u64: f.utcSeconds)
            w.property("neodarwin,utc-counter", u64: f.utcCounter)
        }
        w.property("acpi-rsdp", u64: f.acpiBase)  // the relocated RSDP, for the ACPICA kext (Tier 2)
        w.property("acpi-tables", f.acpiBase, f.acpiLength)
        switch f.psciConduit {  // NeoDarwinPSCI: how CPU_ON reaches the firmware
        case .smc: w.property("psci-conduit", string: "smc")
        case .hvc: w.property("psci-conduit", string: "hvc")
        case .absent: break
        }
        if let uuid = f.bootUUID {  // IOKitBSDInit.cpp IOFindBSDRoot: the root by UUID (AppleFileSystemDriver)
            w.property("boot-uuid", length: GPT.textLength + 1) { v in GPT.writeText(uuid, into: v) }
        }
        w.property("AAPL,phandle", u32: chosenPhandle)
        w.begin()  // /chosen/memory-map
        w.property("name", string: "memory-map")
        if f.ramdiskSize != 0 {
            w.property("RAMDisk", f.ramdiskBase, f.ramdiskSize)  // IOKitBSDInit.cpp: becomes md0
        }
        w.property("ACPITables", f.acpiBase, f.acpiLength)
        if f.trustCacheSize != 0 {
            // kern_trustcache.c load_static_trust_cache: a DTTrustCacheRange
            // over iBoot's trust_cache_offsets_t segment (P1-15).
            w.property("TrustCache", f.trustCacheBase, f.trustCacheSize)
        }
        w.end()
        if emulated(a) {
            w.begin()  // /chosen/machine-timeouts: machine.c machine_timeout_init_with_suffix
            w.property("name", string: "machine-timeouts")
            w.property("global-scale", u32: emulatorTimeoutScale)
            w.end()
        }
        w.end()

        w.begin()  // /defaults: pe_serial.c panics without the node
        w.property("name", string: "defaults")
        if l.uart {
            w.property("serial-device", u32: uartPhandle)  // pe_serial.c: which UART is the console
        }
        w.end()

        w.begin()  // /cpus: every enabled GICC; the boot CPU is running, PSCI starts the rest
        w.property("name", string: "cpus")
        w.property("#address-cells", u32: 1)
        w.property("#size-cells", u32: 0)
        var index: UInt32 = 0
        for i in 0..<a.cpuEntries where l.listed(a, i) {
            let cpu = a.cpus[i]
            w.begin()
            cpuName(&w, index)
            w.property("device_type", string: "cpu")
            w.property("reg", u32: cpu.affinity)  // the kernel's phys_id; MPIDR Aff2:Aff1:Aff0
            // One cluster: without HAS_CLUSTER, ml_parse_cpu_topology puts
            // every cpu with no cluster-type in logical cluster 0 (one SMP
            // processor set), which is what a DynamIQ cluster is. These
            // describe that cluster, replacing the kernel's defaults from
            // the MPIDR (Aff1, Aff0), which on DynamIQ are a core number
            // and 0.
            w.property("die-cluster-id", u32: 0)
            w.property("cluster-core-id", u32: index)
            if i == l.bootIndex {
                w.property("state", string: "running")
            } else {
                w.property("state", string: "waiting")
            }
            w.property("timebase-frequency", u32: UInt32(truncatingIfNeeded: f.timebase))  // the kernel never reads CNTFRQ
            w.property("interrupt-parent", u32: gicPhandle)
            w.property("interrupts", u32s: sgiIPI, cpu.pmuGSIV != 0 ? cpu.pmuGSIV : ppiPMUDefault, sgiDeferredIPI)
            w.property("AAPL,phandle", u32: cpuPhandleBase + index)
            w.end()
            index += 1
        }
        w.end()

        w.begin()  // /arm-io
        w.property("name", string: "arm-io")
        w.property("device_type", string: "soc")
        w.property("ranges", 0, l.socBase, l.socSize)  // pe_identify_machine.c: ranges[1] is the SoC base
        w.property("#address-cells", u32: 2)
        w.property("#size-cells", u32: 2)
        w.property("AAPL,phandle", u32: armIOPhandle)

        let gicd = a.gicdBase - l.socBase, gicr = a.gicrBase - l.socBase
        w.begin()  // /arm-io/gic
        w.property("name", string: "gic")
        w.property("compatible", string: "arm,gic-v3")
        w.property("reg", gicd, gicdSize, gicr, l.gicrSize)  // pe_fiq.c; one frame per MADT GICC
        w.property("interrupt-controller", string: "gic")  // IODTMapOneInterrupt stops here
        w.property("#interrupt-cells", u32: 1)  // one cell: the INTID (NeoDarwinGICv3)
        w.property("#address-cells", u32: 0)
        w.property("timer-ppi", u32: a.timerGSIV)  // pe_fiq.c: the GTDT's virtual timer
        w.property("timer-group", u32: f.timerGroup)  // pe_fiq.c: 0 FIQ, 1 IRQ
        w.property("AAPL,phandle", u32: gicPhandle)
        w.end()

        w.begin()  // /arm-io/interrupt-controller: legacy lookup in pe_identify_machine.c
        w.property("name", string: "interrupt-controller")
        w.property("interrupt-controller", string: "master")
        w.property("reg", gicd, gicdSize)
        w.property("AAPL,phandle", u32: interruptControllerPhandle)
        w.end()

        w.begin()  // /arm-io/timer: same legacy lookup; any small MMIO frame
        w.property("name", string: "timer")
        w.property("device_type", string: "timer")
        w.property("reg", gicr, 0x1_0000)
        w.property("AAPL,phandle", u32: timerPhandle)
        w.end()

        if l.uart {
            w.begin()  // /arm-io/uart0: the SPCR console, polled (a PL011 or a GENI serial engine)
            w.property("name", string: "uart0")
            w.property("device_type", string: "serial")
            w.property("compatible", string: uartCompatible(a.uartType))  // pe_serial.c: selects the driver
            w.property("reg", a.uartBase - l.socBase, uartSize(a.uartType))
            w.property("AAPL,phandle", u32: uartPhandle)
            w.end()
        }
        w.end()  // /arm-io

        w.end()  // /
        return w.complete ? w.offset : nil
    }

    /// "cpu" and the decimal index.
    static func cpuName(_ w: inout DeviceTreeWriter, _ index: UInt32) {
        let digits = index >= 10 ? 2 : 1
        w.property("name", length: 3 + digits + 1) { v in
            v.storeBytes(of: 0x63, toByteOffset: 0, as: UInt8.self)  // c
            v.storeBytes(of: 0x70, toByteOffset: 1, as: UInt8.self)  // p
            v.storeBytes(of: 0x75, toByteOffset: 2, as: UInt8.self)  // u
            if digits == 2 { v.storeBytes(of: UInt8(48 + index / 10), toByteOffset: 3, as: UInt8.self) }
            v.storeBytes(of: UInt8(48 + index % 10), toByteOffset: 2 + digits, as: UInt8.self)
            v.storeBytes(of: 0, toByteOffset: 3 + digits, as: UInt8.self)
        }
    }

    /// /chosen/machine-timeouts global-scale on an emulator. XNU's machine
    /// timeouts (the lock timeouts, the ticket lock's half of "lock",
    /// "lock-panic", the debugger's "debug-ack") are real time since
    /// kernel patch 0038, and a TCG vCPU holding the scheduler's pset lock
    /// can be descheduled on a loaded host for longer than the ticket
    /// lock's 0.125 s: a ZFS suite shard panicked "Ticket spinlock timeout"
    /// with about forty QEMU tests running beside it (2026-10-04). Eight
    /// times gives a ticket lock 1 s. A boot-arg ml-timeout-global-scale
    /// still overrides it; boards keep XNU's values.
    static let emulatorTimeoutScale: UInt32 = 8

    /// QEMU: virt's tables carry OEM ID "BOCHS ", and sbsa-ref's (EDK2
    /// SbsaQemu) OEM ID "LINARO" with OEM table ID "SBSAQEMU".
    static func emulated(_ a: ACPIFacts) -> Bool {
        let bochs = a.oemID == (0x42, 0x4f, 0x43, 0x48, 0x53, 0x20)  // "BOCHS "
        let sbsaQemu = a.oemTableID == 0x554d_4551_4153_4253  // "SBSAQEMU", little-endian
        return bochs || sbsaQemu
    }

    /// `model`: "OEMID,OEM Table ID" from the FADT (else the XSDT), each
    /// with trailing spaces dropped, e.g. "BOCHS,BXPC" on QEMU.
    static func modelProperty(_ w: inout DeviceTreeWriter, _ a: ACPIFacts) {
        var raw: (UInt64, UInt64) = (0, 0)
        withUnsafeMutableBytes(of: &raw) { r in
            withUnsafeBytes(of: a.oemID) { r.baseAddress!.copyMemory(from: $0.baseAddress!, byteCount: 6) }
            r.storeBytes(of: a.oemTableID, toByteOffset: 8, as: UInt64.self)
        }
        let bytes = raw
        let oemLength = withUnsafeBytes(of: bytes) { trimmed($0.baseAddress!, 6) }
        let tableLength = withUnsafeBytes(of: bytes) { trimmed($0.baseAddress! + 8, 8) }
        w.property("model", length: oemLength + 1 + tableLength + 1) { v in
            withUnsafeBytes(of: bytes) { b in
                v.copyMemory(from: b.baseAddress!, byteCount: oemLength)
                v.storeBytes(of: 0x2c, toByteOffset: oemLength, as: UInt8.self)  // ,
                (v + oemLength + 1).copyMemory(from: b.baseAddress! + 8, byteCount: tableLength)
                v.storeBytes(of: 0, toByteOffset: oemLength + 1 + tableLength, as: UInt8.self)
            }
        }
    }

    /// Length without trailing spaces and NULs; other non-printable bytes end it.
    static func trimmed(_ p: UnsafeRawPointer, _ n: Int) -> Int {
        var end = 0
        for i in 0..<n {
            let c = p.load(fromByteOffset: i, as: UInt8.self)
            if c < 0x20 || c > 0x7e || c == 0x2c { break }
            if c != 0x20 { end = i + 1 }
        }
        return end
    }
}
