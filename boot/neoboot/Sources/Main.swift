// SPDX-License-Identifier: BSD-2-Clause
//
// neoboot v0 (P1-03): load the flat boot kernel collection from the ESP,
// describe the machine in an Apple device tree, fill boot_args, leave boot
// services and enter the kernel at EL1 with the MMU off, as iBoot does
// (docs/kernel/arm64-sbsa-bringup.md §2.1).
//
// Physical layout, from the collection's (32 MiB-congruent) base:
//
//     kernelcache (flat, kcgen) | device tree (64 KiB) | ramdisk | boot_args page | ← topOfKernelData
//
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
/// -noprogress until the loader passes a GOP framebuffer: with no display,
/// PE_init_iokit()'s progress-bar centring loop never terminates.
let defaultCommandLine: StaticString = "debug=0x14e serial=3 -v -noprogress"

let kernelPage: UInt64 = 0x4000
/// start.s maps the kernel with 16 KiB-granule L2 blocks, so the collection's
/// physical address must equal its virtual address modulo 32 MiB.
let l2Block: UInt64 = 0x200_0000
let treeCapacity: UInt64 = 0x1_0000
let efiLoadError: EFI_STATUS = 0x8000_0000_0000_0001

@_cdecl("efi_main")
func efiMain(_ image: EFI_HANDLE?, _ system: UnsafeMutablePointer<EFI_SYSTEM_TABLE>) -> EFI_STATUS {
    Console.firmware = system.pointee.ConOut
    let fw = Firmware(image: image, boot: system.pointee.BootServices)
    fw.disableWatchdog()
    put("neoboot 0.1: loading NeoDarwin\n")
    return boot(fw)  // returns only on failure
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

func boot(_ fw: Firmware) -> EFI_STATUS {
    guard let root = fw.openBootVolume() else { return fail("cannot open the boot volume") }
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
    let window = map.largestWindow()
    // The ramdisk (mockfs's executable until P1-08's HFS+ image): memdev
    // counts it in 4 KiB pages, so its length is rounded up and zero-filled.
    let ramdisk = EFIFile(root: root, path: ramdiskPath)
    let ramdiskSize = ramdisk.map { roundUp($0.size, kernelPage) } ?? 0
    let dtOffset = roundUp(kc.vmSize, kernelPage)
    let ramdiskOffset = dtOffset + treeCapacity
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

    var tree = DeviceTreeWriter(base: image + Int(dtOffset), capacity: Int(treeCapacity))
    let facts = Platform.Facts(dramBase: base, dramSize: memSize, timebase: nd_cntfrq(), mpidr: nd_mpidr(), seed: nd_cntpct(),
                                ramdiskBase: ramdiskSize == 0 ? 0 : base + ramdiskOffset, ramdiskSize: ramdiskSize)
    guard let treeLength = Platform.deviceTree(into: &tree, facts) else { return fail("the device tree does not fit") }

    // Command line: \NeoDarwin\boot.cfg if present, read into the spare half
    // of the boot_args page; whitespace runs become single spaces.
    let args = image + Int(argsOffset)
    let lineBuffer = args + 0x2000
    var lineLength = 0
    if let cfg = EFIFile(root: root, path: bootConfigPath) {
        let n = min(cfg.size, UInt64(BootArgs.commandLineLength - 1))
        if cfg.read(at: 0, count: n, into: lineBuffer) { lineLength = Int(n) }
        cfg.close()
        var out = 0
        for i in 0..<lineLength {
            var b = lineBuffer.load(fromByteOffset: i, as: UInt8.self)
            if b == 10 || b == 13 || b == 9 { b = 32 }
            if b == 32 && (out == 0 || lineBuffer.load(fromByteOffset: out - 1, as: UInt8.self) == 32) { continue }
            lineBuffer.storeBytes(of: b, toByteOffset: out, as: UInt8.self)
            out += 1
        }
        while out > 0 && lineBuffer.load(fromByteOffset: out - 1, as: UInt8.self) == 32 { out -= 1 }
        lineLength = out
    }
    if lineLength == 0 {
        defaultCommandLine.withUTF8Buffer { lineBuffer.copyMemory(from: $0.baseAddress!, byteCount: $0.count) }
        lineLength = defaultCommandLine.utf8CodeUnitCount
    }
    if ramdiskSize != 0 && !mentions(lineBuffer, lineLength, "rd=") {
        ramdiskRoot.withUTF8Buffer { (lineBuffer + lineLength).copyMemory(from: $0.baseAddress!, byteCount: $0.count) }
        lineLength += ramdiskRoot.utf8CodeUnitCount
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
    bootArgs.write(to: args, commandLine: commandLine)

    log("  DRAM window        ", window.start)
    log("  window end         ", window.end)
    log("  kernelcache at     ", base)
    log("  link address       ", kc.linkAddress)
    log("  kernel entry       ", entry)
    log("  device tree bytes  ", UInt64(treeLength))
    if ramdiskSize != 0 {
        log("  ramdisk at         ", base + ramdiskOffset)
        log("  ramdisk bytes      ", ramdiskSize)
    }
    log("  boot_args at       ", base + argsOffset)
    log("  memSize            ", memSize)
    log("  timebase (Hz)      ", facts.timebase)
    log("  current EL         ", nd_current_el())
    put("  command line       ")
    put(bytes: UnsafeRawBufferPointer(start: args + BootArgs.Offset.commandLine, count: lineLength))
    put("\n")

    // start.s reads all of this with the MMU and caches off.
    nd_dcache_clean_poc(base, span)
    guard fw.exitBootServices(&map) else { return fail("ExitBootServices failed") }
    Console.detach()
    put("neoboot: entering the kernel\n")
    nd_enter_kernel(entry, base + argsOffset)
}
