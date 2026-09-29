// SPDX-License-Identifier: BSD-2-Clause
//
// neoboot: read the ACPI tables, load the flat boot kernel collection from
// the ESP, describe the machine in an Apple device tree synthesised from ACPI
// (DT-ABI v1, docs/kernel/dt-abi.md), fill boot_args, leave boot services
// and enter the kernel at EL1 with the MMU off, as iBoot does
// (docs/kernel/arm64-sbsa-bringup.md §2.1).
//
// Physical layout, from the collection's (32 MiB-congruent) base:
//
//     kernelcache (flat, kcgen) | device tree (64 KiB) | ACPI tables | ramdisk | boot_args page | ← topOfKernelData
//
// The ACPI tables are a copy, with their pointers rewritten, so that the
// kernel reaches them through its physmap (/chosen/memory-map/ACPITables).
// The ramdisk is optional. It lies below topOfKernelData because the kernel
// maps it with ml_static_ptovirt() (IOKitBSDInit.cpp), and boot_args
// describes the DRAM window from that base to the end of the
// largest hole-free run of memory the kernel may own.

import UEFI

let kernelcachePath: StaticString = "\\NeoDarwin\\kernelcache"
let bootConfigPath: StaticString = "\\NeoDarwin\\boot.cfg"
let ramdiskPath: StaticString = "\\NeoDarwin\\ramdisk"
/// Appended when a ramdisk is loaded and the command line names no root.
let ramdiskRoot: StaticString = " rd=md0"
/// -noprogress: with no framebuffer, PE_init_iokit()'s progress-bar
/// centring loop never terminates; with one, the console is text anyway
/// (boot_args.Video.v_display = 0).
let defaultCommandLine: StaticString = "debug=0x14e serial=3 -v -noprogress"
/// Appended on a multiprocessor without a PSCI conduit when the command line
/// doesn't name a CPU count: the tree lists every CPU, but NeoDarwinPSCI
/// can't start the secondaries. ml_parse_cpu_topology then takes the boot
/// CPU alone, and IOKit leaves the other cpu nubs unused (IOPlatformExpert.cpp).
let uniprocessorCap: StaticString = " cpus=1"
/// A loader option in boot.cfg: list every ACPI table on the console in
/// acpidump's format before booting (for dtdump fixtures from any board).
let dumpACPIOption: StaticString = "dump-acpi"
/// Loader options in boot.cfg that choose the timer's GIC group
/// (/arm-io/gic timer-group) instead of GICD_CTLR.DS: Group 1 makes the
/// timer an IRQ on a GIC that would allow Group 0, which is how QEMU without
/// EL3 tests the path TrustZone boards need.
let timerGroup0Option: StaticString = "timer-group=0"
let timerGroup1Option: StaticString = "timer-group=1"
/// GICD_CTLR, at offset 0 of the distributor.
let gicdCTLROffset: UInt64 = 0

let kernelPage: UInt64 = 0x4000
/// start.s maps the kernel with 16 KiB-granule L2 blocks, so the collection's
/// physical address must equal its virtual address modulo 32 MiB.
let l2Block: UInt64 = 0x200_0000
let treeCapacity: UInt64 = 0x1_0000
let efiLoadError: EFI_STATUS = 0x8000_0000_0000_0001

@_cdecl("efi_main")
func efiMain(_ image: EFI_HANDLE?, _ system: UnsafeMutablePointer<EFI_SYSTEM_TABLE>) -> EFI_STATUS {
    Console.firmware = system.pointee.ConOut
    let fw = Firmware(image: image, boot: system.pointee.BootServices, runtime: system.pointee.RuntimeServices)
    fw.disableWatchdog()
    put("neoboot 0.1: loading NeoDarwin\n")
    return boot(fw, system)  // returns only on failure
}

func fail(_ why: StaticString) -> EFI_STATUS {
    put("neoboot: ")
    put(why)
    put("\n")
    return efiLoadError
}

func roundUp(_ v: UInt64, _ a: UInt64) -> UInt64 { (v + a - 1) / a * a }

/// Whether a space-separated argument in the line starts with `prefix`.
func mentions(_ line: UnsafeMutableRawPointer, _ length: Int, _ prefix: StaticString) -> Bool {
    let p = prefix.utf8Start, n = prefix.utf8CodeUnitCount
    var i = 0
    while i + n <= length {
        if i == 0 || line.load(fromByteOffset: i - 1, as: UInt8.self) == 32 {
            var j = 0
            while j < n && line.load(fromByteOffset: i + j, as: UInt8.self) == p[j] { j += 1 }
            if j == n { return true }
        }
        i += 1
    }
    return false
}

/// Whether `word` is one whole space-separated argument in the line.
func hasArgument(_ line: UnsafeMutableRawPointer, _ length: Int, _ word: StaticString) -> Bool {
    let p = word.utf8Start, n = word.utf8CodeUnitCount
    var i = 0
    while i + n <= length {
        if (i == 0 || line.load(fromByteOffset: i - 1, as: UInt8.self) == 32)
            && (i + n == length || line.load(fromByteOffset: i + n, as: UInt8.self) == 32) {
            var j = 0
            while j < n && line.load(fromByteOffset: i + j, as: UInt8.self) == p[j] { j += 1 }
            if j == n { return true }
        }
        i += 1
    }
    return false
}

/// The timer's GIC group from the distributor's security state and
/// boot.cfg (Platform.timerGroup), reported on the console.
func chooseTimerGroup(_ a: ACPIFacts, _ config: UnsafeMutableRawPointer, _ configLength: Int) -> UInt32 {
    let ctlr = nd_mmio_read32(a.gicdBase + gicdCTLROffset)
    var forced: UInt32? = nil
    if hasArgument(config, configLength, timerGroup0Option) { forced = 0 }
    if hasArgument(config, configLength, timerGroup1Option) { forced = 1 }
    let group = Platform.timerGroup(gicdCTLR: ctlr, forced: forced)
    put("neoboot: GIC: GICD_CTLR ")
    putHex(UInt64(ctlr))
    put(ctlr & Platform.gicdCTLRDS != 0 ? ", DS=1 (Group 0 open to Non-secure)" : ", DS=0 (two security states; Group 0 is Secure)")
    put("; timer PPI ")
    putDec(UInt64(a.timerGSIV))
    put(group == 0 ? " on Group 0 (FIQ)" : " on Group 1 (IRQ)")
    if forced != nil { put(group == 0 ? ", forced by timer-group=0" : ", forced by timer-group=1") }
    put("\n")
    return group
}

/// The PSCI conduit for NeoDarwinPSCI (/chosen psci-conduit), from the FADT
/// and the CPU (Platform.psciConduit), reported on the console. Without one
/// a multiprocessor boots its boot CPU alone.
func choosePSCIConduit(_ a: ACPIFacts, _ l: Platform.Layout) -> Platform.PSCIConduit {
    let choice = Platform.psciConduit(armBootFlags: a.armBootFlags, el3: nd_el3_implemented() != 0, loaderEL: nd_current_el())
    put("neoboot: PSCI conduit: ")
    put(choice.why)
    put("\n")
    if choice.conduit == .absent && l.cpuNodes > 1 {
        put("neoboot: PSCI: the kernel cannot start the other CPUs; booting one (cpus=1) unless boot.cfg names cpus= or cpumask=\n")
    }
    return choice.conduit
}

/// \NeoDarwin\boot.cfg with whitespace runs made single spaces, into `line`
/// (BootArgs.commandLineLength bytes); returns its length, 0 if absent.
func readBootConfig(_ root: UnsafeMutablePointer<EFI_FILE_PROTOCOL>, into line: UnsafeMutableRawPointer) -> Int {
    guard let cfg = EFIFile(root: root, path: bootConfigPath) else { return 0 }
    var length = 0
    let n = min(cfg.size, UInt64(BootArgs.commandLineLength - 1))
    if cfg.read(at: 0, count: n, into: line) { length = Int(n) }
    cfg.close()
    var out = 0
    for i in 0..<length {
        var b = line.load(fromByteOffset: i, as: UInt8.self)
        if b == 10 || b == 13 || b == 9 { b = 32 }
        if b == 32 && (out == 0 || line.load(fromByteOffset: out - 1, as: UInt8.self) == 32) { continue }
        line.storeBytes(of: b, toByteOffset: out, as: UInt8.self)
        out += 1
    }
    while out > 0 && line.load(fromByteOffset: out - 1, as: UInt8.self) == 32 { out -= 1 }
    return out
}

func boot(_ fw: Firmware, _ system: UnsafeMutablePointer<EFI_SYSTEM_TABLE>) -> EFI_STATUS {
    guard let root = fw.openBootVolume() else { return fail("cannot open the boot volume") }

    // The command line first: it may ask for the ACPI dump.
    guard let linePage = fw.allocate(pages: 1) else { return fail("out of memory") }
    let config = UnsafeMutableRawPointer(bitPattern: UInt(linePage))!
    let configLength = readBootConfig(root, into: config)

    // ACPI: the machine description (DT-ABI v1). No tables, no boot: the
    // loader has no built-in description to fall back on.
    guard let rsdp = fw.rsdp(system) else {
        return fail("the firmware publishes no ACPI 2.0 RSDP (EFI_ACPI_20_TABLE_GUID); cannot describe this machine")
    }
    let acpi: ACPIFacts
    let layout: Platform.Layout
    do throws(ACPIError) {
        if mentions(config, configLength, dumpACPIOption) { try dumpACPI(rsdp: rsdp) }
        acpi = try ACPI.parse(PhysicalMemory(), rsdp: rsdp)
        layout = try Platform.layout(acpi, bootMPIDR: nd_mpidr())
    } catch {
        return report(error)
    }
    reportACPI(acpi, layout)
    let timerGroup = chooseTimerGroup(acpi, config, configLength)
    let psci = choosePSCIConduit(acpi, layout)
    // The firmware's framebuffer, for the kernel's video console (§2.1.7).
    var framebuffer: Framebuffer? = nil
    if hasArgument(config, configLength, gopOffOption) {
        put("neoboot: GOP: not used (gop=off); the console is serial only\n")
    } else {
        framebuffer = fw.framebuffer(system)
    }

    guard let file = EFIFile(root: root, path: kernelcachePath) else { return fail("no \\NeoDarwin\\kernelcache on the boot volume") }
    let fileSize = file.size

    // The header first: the link address decides where the collection may go.
    let headerPages: UInt64 = 4
    guard let scratch = fw.allocate(pages: headerPages) else { return fail("out of memory") }
    let headerBytes = min(fileSize, headerPages * uefiPage)
    let header = UnsafeMutableRawPointer(bitPattern: UInt(scratch))!
    guard file.read(at: 0, count: headerBytes, into: header),
          let kc = Kernelcache(header: header, count: Int(headerBytes)) else {
        return fail("the kernelcache is not an MH_FILESET kernel collection")
    }
    fw.free(scratch, pages: headerPages)
    guard kc.vmSize == fileSize else { return fail("the kernelcache is not flat; rebuild it with kcgen") }

    guard var map = MemoryMap(fw) else { return fail("cannot read the memory map") }
    var window = map.largestWindow()
    if let fb = framebuffer {
        // Where the framebuffer lies decides how the kernel may treat it.
        put("neoboot: GOP: the framebuffer's memory is ")
        if let type = map.type(at: fb.base) {
            put("UEFI memory type ")
            putDec(UInt64(type))
        } else {
            put("not in the UEFI memory map")
        }
        put("\n")
        let rest = excluding(window, fb, page: kernelPage)
        if rest != window {
            put("neoboot: GOP: the framebuffer lies in the DRAM window; the kernel gets ")
            putHex(rest.start)
            put("-")
            putHex(rest.end)
            put("\n")
            window = rest
        }
    }
    // The ramdisk (mockfs's executable until P1-08's HFS+ image): memdev
    // counts it in 4 KiB pages, so its length is rounded up and zero-filled.
    let ramdisk = EFIFile(root: root, path: ramdiskPath)
    let ramdiskSize = ramdisk.map { roundUp($0.size, kernelPage) } ?? 0
    let dtOffset = roundUp(kc.vmSize, kernelPage)
    let acpiOffset = dtOffset + treeCapacity
    let acpiCapacity = roundUp(UInt64(acpi.relocatedLength), kernelPage)
    let ramdiskOffset = acpiOffset + acpiCapacity
    let argsOffset = ramdiskOffset + ramdiskSize
    let span = argsOffset + kernelPage

    // Slide 0 for first light: the lowest free address congruent to the link
    // address modulo 32 MiB. KASLR picks among the candidates later.
    let slide: UInt64 = 0
    let congruence = (kc.linkAddress + slide) % l2Block
    var candidate = window.start - window.start % l2Block + congruence
    if candidate < window.start { candidate += l2Block }
    var base: UInt64 = 0
    while candidate + span <= window.end {
        if fw.allocate(pages: span / uefiPage, at: candidate) != nil { base = candidate; break }
        candidate += l2Block
    }
    guard base != 0 else { return fail("no free place for the kernelcache in the DRAM window") }
    let image = UnsafeMutableRawPointer(bitPattern: UInt(base))!
    guard file.read(at: 0, count: fileSize, into: image) else { return fail("cannot read the kernelcache") }
    file.close()
    guard let entryVA = Kernelcache.entry(image: image, kernelOffset: kc.kernelOffset),
          entryVA >= kc.linkAddress, entryVA < kc.linkAddress + kc.vmSize else {
        return fail("the kernel has no entry point")
    }

    if let ramdisk {
        let at = image + Int(ramdiskOffset)
        at.initializeMemory(as: UInt8.self, repeating: 0, count: Int(ramdiskSize))
        guard ramdisk.read(at: 0, count: ramdisk.size, into: at) else { return fail("cannot read the ramdisk") }
        ramdisk.close()
    }

    let virtBase = kc.linkAddress + slide
    let memSize = (window.end - base) & ~(kernelPage - 1)
    let entry = base + (entryVA - kc.linkAddress)

    // The time of day, and the counter it was read at: the kernel has no RTC
    // driver and no UEFI runtime services (arm64-sbsa-bringup.md §2.2). The
    // counter is the one the kernel reads, CNTVCT, which equals CNTPCT once
    // nd_enter_kernel has left EL2 with CNTVOFF_EL2 = 0.
    let utc = fw.utcSeconds() ?? 0
    let utcCounter = nd_current_el() == 2 ? nd_cntpct() : nd_cntvct()

    // The ACPI copy the kernel gets, checked by parsing it again where it lies.
    let acpiBase = base + acpiOffset
    let acpiLength: Int
    (image + Int(acpiOffset)).initializeMemory(as: UInt8.self, repeating: 0, count: Int(acpiCapacity))
    do throws(ACPIError) {
        acpiLength = try ACPI.relocate(PhysicalMemory(), rsdp: rsdp, into: image + Int(acpiOffset), physical: acpiBase,
                                       capacity: Int(acpiCapacity))
        let copy = try ACPI.parse(PhysicalMemory(), rsdp: acpiBase)
        guard copy.gicCount == acpi.gicCount, copy.gicdBase == acpi.gicdBase, copy.uartBase == acpi.uartBase else {
            throw ACPIError("the relocated tables read differently from the firmware's")
        }
    } catch {
        return report(error)
    }

    var tree = DeviceTreeWriter(base: image + Int(dtOffset), capacity: Int(treeCapacity))
    let facts = Platform.Facts(dramBase: base, dramSize: memSize, timebase: nd_cntfrq(), bootMPIDR: nd_mpidr(), seed: nd_cntpct(),
                               ramdiskBase: ramdiskSize == 0 ? 0 : base + ramdiskOffset, ramdiskSize: ramdiskSize,
                               utcSeconds: utc, utcCounter: utcCounter, acpiBase: acpiBase, acpiLength: UInt64(acpiLength),
                               timerGroup: timerGroup, psciConduit: psci)
    guard let treeLength = Platform.deviceTree(into: &tree, facts, acpi, layout) else { return fail("the device tree does not fit") }
    var violations = ConsoleReport()
    guard DTCheck.check(image + Int(dtOffset), length: treeLength, &violations) == 0 else {
        return fail("the synthesised device tree breaks DT-ABI v1")
    }

    // Command line: boot.cfg if present, in the spare half of the boot_args page.
    let args = image + Int(argsOffset)
    let lineBuffer = args + 0x2000
    var lineLength = configLength
    lineBuffer.copyMemory(from: config, byteCount: configLength)
    fw.free(linePage, pages: 1)
    if lineLength == 0 {
        defaultCommandLine.withUTF8Buffer { lineBuffer.copyMemory(from: $0.baseAddress!, byteCount: $0.count) }
        lineLength = defaultCommandLine.utf8CodeUnitCount
    }
    if ramdiskSize != 0 && !mentions(lineBuffer, lineLength, "rd=") {
        ramdiskRoot.withUTF8Buffer { (lineBuffer + lineLength).copyMemory(from: $0.baseAddress!, byteCount: $0.count) }
        lineLength += ramdiskRoot.utf8CodeUnitCount
    }
    if layout.cpuNodes > 1 && psci == .absent && !mentions(lineBuffer, lineLength, "cpus=") && !mentions(lineBuffer, lineLength, "cpumask=") {
        uniprocessorCap.withUTF8Buffer { (lineBuffer + lineLength).copyMemory(from: $0.baseAddress!, byteCount: $0.count) }
        lineLength += uniprocessorCap.utf8CodeUnitCount
    }
    let commandLine = UnsafeRawBufferPointer(start: lineBuffer, count: lineLength)

    var bootArgs = BootArgs()
    bootArgs.virtBase = virtBase
    bootArgs.physBase = base
    bootArgs.memSize = memSize
    bootArgs.topOfKernelData = base + span
    bootArgs.deviceTree = virtBase + dtOffset
    bootArgs.deviceTreeLength = UInt32(treeLength)
    bootArgs.memSizeActual = map.totalRAM()
    if let fb = framebuffer {
        bootArgs.video = BootArgs.Video(baseAddr: fb.base, rowBytes: fb.rowBytes, width: fb.width, height: fb.height, depth: 32)
    }
    bootArgs.write(to: args, commandLine: commandLine)

    log("  DRAM window        ", window.start)
    log("  window end         ", window.end)
    log("  kernelcache at     ", base)
    log("  link address       ", kc.linkAddress)
    log("  kernel entry       ", entry)
    log("  device tree bytes  ", UInt64(treeLength))
    log("  ACPI tables at     ", acpiBase)
    log("  ACPI tables bytes  ", UInt64(acpiLength))
    if ramdiskSize != 0 {
        log("  ramdisk at         ", base + ramdiskOffset)
        log("  ramdisk bytes      ", ramdiskSize)
    }
    log("  boot_args at       ", base + argsOffset)
    if let fb = framebuffer { log("  framebuffer at     ", fb.base) }
    log("  memSize            ", memSize)
    log("  timebase (Hz)      ", facts.timebase)
    log("  UTC seconds        ", utc)
    log("  current EL         ", nd_current_el())
    put("  command line       ")
    put(bytes: UnsafeRawBufferPointer(start: args + BootArgs.Offset.commandLine, count: lineLength))
    put("\n")

    // start.s reads all of this with the MMU and caches off. The kernel
    // maps the framebuffer uncached, so the firmware's last drawing goes to
    // memory too.
    nd_dcache_clean_poc(base, span)
    if let fb = framebuffer { nd_dcache_clean_poc(fb.base, fb.size) }
    Console.pl011Base = UInt(acpi.uartBase)  // SPCR: the console once the firmware's is gone
    guard fw.exitBootServices(&map) else { return fail("ExitBootServices failed") }
    Console.detach()
    put("neoboot: entering the kernel\n")
    nd_enter_kernel(entry, base + argsOffset)
}
