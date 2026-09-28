// SPDX-License-Identifier: BSD-2-Clause
//
// The machine description neoboot hands the kernel as an Apple device tree.
// P1-03 describes QEMU virt (gic-version=3) from constants; P1-04 replaces
// the constants with values read from ACPI (MADT, GTDT, SPCR) and checks the
// result against the DT-ABI (arm64-sbsa-bringup.md §2.2). Node and property
// names follow the kernel's reads cited there.

enum Platform {
    // QEMU virt memory map (hw/arm/virt.c).
    static let socBase: UInt64 = 0x0800_0000  // /arm-io ranges[1]; every reg below is relative to it
    static let socSize: UInt64 = 0x0800_0000
    static let gicdOffset: UInt64 = 0x0
    static let gicdSize: UInt64 = 0x1_0000
    static let gicrOffset: UInt64 = 0xa_0000
    // One 128 KiB redistributor frame (RD_base + SGI_base) per CPU present;
    // QEMU reserves 0xf60000 for 123 CPUs, but only these frames exist.
    // P1-04 takes the frames from the MADT's GICC entries.
    static let cpuCount: UInt64 = 1
    static let gicrSize: UInt64 = 0x2_0000 * cpuCount
    static let uartOffset: UInt64 = 0x100_0000
    static let uartSize: UInt64 = 0x1000
    static let uartBase: UInt = UInt(socBase + uartOffset)

    static let uartPhandle: UInt32 = 3
    static let gicPhandle: UInt32 = 5
    // GIC INTIDs the CPU node names, in AppleARMSMP.cpp's three-entry order:
    // IPI, PMI, deferred IPI. IPIs are SGIs; the PMU is PPI 23 on SBSA.
    static let sgiIPI: UInt32 = 0
    static let ppiPMU: UInt32 = 23
    static let sgiDeferredIPI: UInt32 = 1

    struct Facts {
        var dramBase: UInt64
        var dramSize: UInt64
        var timebase: UInt64
        var mpidr: UInt64
        var seed: UInt64
        var ramdiskBase: UInt64 = 0  // physical; 0 when there is none
        var ramdiskSize: UInt64 = 0
    }

    /// Writes the tree; returns its length, or nil if it did not fit.
    static func deviceTree(into w: inout DeviceTreeWriter, _ f: Facts) -> Int? {
        w.begin()  // /
        w.property("name", string: "device-tree")
        w.property("compatible", string: "NeoDarwin,sbsa")
        w.property("model", string: "NeoDarwin,qemu-virt")
        w.property("target-type", string: "sbsa")
        w.property("#address-cells", u32: 2)
        w.property("#size-cells", u32: 2)
        w.property("AAPL,phandle", u32: 1)

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
        w.property("AAPL,phandle", u32: 2)
        w.begin()  // /chosen/memory-map: RAMDisk; ACPITables later
        w.property("name", string: "memory-map")
        if f.ramdiskSize != 0 {
            w.property("RAMDisk", f.ramdiskBase, f.ramdiskSize)  // IOKitBSDInit.cpp: becomes md0
        }
        w.end()
        w.end()

        w.begin()  // /defaults
        w.property("name", string: "defaults")
        w.property("serial-device", u32: uartPhandle)  // pe_serial.c: which UART is the console
        w.end()

        w.begin()  // /cpus
        w.property("name", string: "cpus")
        w.property("#address-cells", u32: 1)
        w.property("#size-cells", u32: 0)
        w.begin()  // /cpus/cpu0: the boot CPU only until P1-06 brings up SMP
        w.property("name", string: "cpu0")
        w.property("device_type", string: "cpu")
        w.property("reg", u32: UInt32(truncatingIfNeeded: f.mpidr & 0xff_ffff))
        w.property("state", string: "running")
        w.property("timebase-frequency", u32: UInt32(truncatingIfNeeded: f.timebase))  // the kernel never reads CNTFRQ
        w.property("interrupt-parent", u32: gicPhandle)
        w.property("interrupts", u32s: sgiIPI, ppiPMU, sgiDeferredIPI)
        w.property("AAPL,phandle", u32: 10)
        w.end()
        w.end()

        w.begin()  // /arm-io
        w.property("name", string: "arm-io")
        w.property("device_type", string: "soc")
        w.property("ranges", 0, socBase, socSize)  // pe_identify_machine.c: ranges[1] is the SoC base
        w.property("#address-cells", u32: 2)
        w.property("#size-cells", u32: 2)
        w.property("AAPL,phandle", u32: 4)

        w.begin()  // /arm-io/gic
        w.property("name", string: "gic")
        w.property("compatible", string: "arm,gic-v3")
        w.property("reg", gicdOffset, gicdSize, gicrOffset, gicrSize)  // pe_fiq.c
        w.property("interrupt-controller", string: "gic")  // IODTMapOneInterrupt stops here
        w.property("#interrupt-cells", u32: 1)  // one cell: the INTID (NeoDarwinGICv3)
        w.property("#address-cells", u32: 0)
        w.property("AAPL,phandle", u32: gicPhandle)
        w.end()

        w.begin()  // /arm-io/interrupt-controller: legacy lookup in pe_identify_machine.c
        w.property("name", string: "interrupt-controller")
        w.property("interrupt-controller", string: "master")
        w.property("reg", gicdOffset, gicdSize)
        w.property("AAPL,phandle", u32: 6)
        w.end()

        w.begin()  // /arm-io/timer: same legacy lookup; any small MMIO frame
        w.property("name", string: "timer")
        w.property("device_type", string: "timer")
        w.property("reg", gicrOffset, 0x1_0000)
        w.property("AAPL,phandle", u32: 7)
        w.end()

        w.begin()  // /arm-io/uart0: polled PL011 console
        w.property("name", string: "uart0")
        w.property("device_type", string: "serial")
        w.property("compatible", string: "arm,pl011")
        w.property("reg", uartOffset, uartSize)
        w.property("AAPL,phandle", u32: uartPhandle)
        w.end()
        w.end()  // /arm-io

        w.end()  // /
        return w.complete ? w.offset : nil
    }
}
