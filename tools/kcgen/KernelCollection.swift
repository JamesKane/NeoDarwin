// SPDX-License-Identifier: BSD-2-Clause
//
// Builds an MH_FILESET boot kernel collection from a linked XNU kernel.
//
// Layout (all offsets from the collection header, which the kernel expects
// at VM_KERNEL_LINK_ADDRESS; file offset == VM offset throughout):
//
//     0  __TEXT          collection header and load commands
//     H  __PRELINK_INFO  XML plist: _PrelinkInfoDictionary, _PrelinkKCID
//     D  __PRELINK_TEXT  empty until kexts are linked in (P5)
//     D  __KERNEL        kernel __TEXT, __DATA_CONST, __TEXT_EXEC, __KLD
//        __DATA_CONST    kernel __LASTDATA_CONST
//        __TEXT_EXEC     empty, at kernel __LAST
//        __DATA          kernel __KLDDATA, __DATA (bss materialised), __BOOTDATA
//        __LINKEDIT      kernel __LINKINFO and __LINKEDIT, then the chained fixups
//
// The kernel is translated as a whole by D, so its PC-relative code needs no
// change; its local relocations become kernel-cache chained fixups, which
// arm_init() applies itself (arm_slide_rebase_and_sign_image). The top-level
// segment names are the ones arm_vm_init() looks up in the collection, sized
// so its PLK_* and PRELINK_* regions come out empty. __KERNEL is NeoDarwin's
// name for the stretch the kernel never looks up by name.

import MachO

public struct KernelCollectionOptions: Sendable {
    public var entryID = "com.apple.kernel"
    public var pageSize = KernelCacheChains.pageSize
    public init() {}
}

public struct KernelCollectionReport: Sendable {
    public var linkAddress: UInt64 = 0
    public var kernelOffset: UInt64 = 0
    public var fixups = 0
    public var size = 0
    public var uuid: [UInt8] = []
    public var segments: [(name: String, offset: UInt64, size: UInt64)] = []
}

public enum KernelCollection {
    /// Top-level segments, in order. Names come from arm_vm_init() and
    /// kernel_collection_slide(); __KERNEL is ours.
    struct TopSegment {
        let name: String
        let offset: UInt64
        let size: UInt64
        let prot: UInt32
        var section: (name: String, size: UInt64)? = nil
    }

    static let linkeditCommands: Set<UInt32> = LC.linkeditData
    static let passThrough: Set<UInt32> = [LC.uuid, LC.buildVersion, LC.sourceVersion]

    public static func build(kernel bytes: [UInt8], options: KernelCollectionOptions = .init()) throws -> ([UInt8], KernelCollectionReport) {
        let k = try MachOImage(bytes: bytes)
        let page = UInt64(options.pageSize)
        guard k.filetype == MH.execute, k.cputype == CPU.arm64 else {
            throw MachOError.unsupported("input is not an arm64 MH_EXECUTE kernel")
        }
        guard k.flags & MH.dylibInCache == 0 else { throw MachOError.unsupported("kernel is already a fileset member") }
        for lc in k.commands {
            switch lc.cmd {
            case LC.segment64, LC.symtab, LC.dysymtab, LC.unixThread: continue
            case _ where passThrough.contains(lc.cmd) || linkeditCommands.contains(lc.cmd):
                if lc.cmd == LC.dyldChainedFixups || lc.cmd == LC.codeSignature || lc.cmd == LC.segmentSplitInfo {
                    throw MachOError.unsupported("kernel carries load command 0x\(String(lc.cmd, radix: 16))")
                }
            default:
                throw MachOError.unsupported("kernel carries load command 0x\(String(lc.cmd, radix: 16))")
            }
        }
        guard let text = k.segments.first, text.name == "__TEXT", text.fileoff == 0,
              k.commands.first?.cmd == LC.segment64 else {
            throw MachOError.malformed("the kernel's first load command must be the __TEXT segment at file offset 0")
        }
        let link = text.vmaddr
        guard link % page == 0 else { throw MachOError.malformed("kernel link address is not page-aligned") }
        let mapped = k.segments.filter { $0.vmsize > 0 }
        for s in mapped {
            guard s.vmaddr >= link, s.vmaddr % page == 0 else { throw MachOError.malformed("segment \(s.name) is below the link address or unaligned") }
            guard s.filesize <= s.vmsize else { throw MachOError.malformed("segment \(s.name) has filesize > vmsize") }
        }
        for (a, b) in zip(mapped, mapped.dropFirst()) where b.vmaddr != a.vmEnd {
            throw MachOError.unsupported("kernel VM layout has a gap or reordering between \(a.name) and \(b.name)")
        }
        guard let lastMapped = mapped.last else { throw MachOError.malformed("kernel has no mapped segments") }
        let kernelSpan = roundUp(lastMapped.vmEnd - link, page)

        // Relocations: ARM64_RELOC_UNSIGNED, 8 bytes, local. r_address is
        // relative to the first segment (ld64's relocation base for kernels).
        guard let dysymLC = k.command(LC.dysymtab), k.command(LC.symtab) != nil,
              k.command(LC.unixThread) != nil else {
            throw MachOError.malformed("kernel lacks LC_SYMTAB, LC_DYSYMTAB or LC_UNIXTHREAD")
        }
        guard try bytes.u32(dysymLC.offset + Dysymtab.nextrel) == 0 else { throw MachOError.unsupported("kernel has external relocations") }
        let locreloff = Int(try bytes.u32(dysymLC.offset + Dysymtab.locreloff))
        let nlocrel = Int(try bytes.u32(dysymLC.offset + Dysymtab.nlocrel))
        var pointers: [(address: UInt64, value: UInt64)] = []
        pointers.reserveCapacity(nlocrel)
        for i in 0..<nlocrel {
            let r = try Relocation(bytes, locreloff + i * Relocation.size)
            guard r.type == Relocation.arm64Unsigned, r.length == 3, !r.pcRelative, !r.isExtern else {
                throw MachOError.unsupported("relocation \(i): type \(r.type) length \(r.length) pcrel \(r.pcRelative) extern \(r.isExtern)")
            }
            let address = link &+ UInt64(bitPattern: Int64(r.address))
            guard let at = k.fileOffset(of: address, width: 8) else {
                throw MachOError.malformed("relocation \(i) at 0x\(String(address, radix: 16)) is not file-backed")
            }
            let value = try bytes.u64(at)
            guard value >= link, value <= link + kernelSpan else {
                throw MachOError.malformed("relocation \(i) at 0x\(String(address, radix: 16)) holds 0x\(String(value, radix: 16)), outside the kernel")
            }
            pointers.append((address, value))
        }

        // Anchors in the kernel that the top-level segments must line up with.
        func seg(_ name: String) throws -> Segment64 {
            guard let s = k.segment(named: name) else { throw MachOError.malformed("kernel has no \(name) segment") }
            return s
        }
        let lastDataConst = try seg("__LASTDATA_CONST")
        let last = try seg("__LAST")
        let prelinkData = try seg("__PRELINK_DATA")
        for name in ["__PRELINK_TEXT", "__PRELINK_INFO", "__PRELINK_DATA", "__PLK_TEXT_EXEC", "__PLK_DATA_CONST"] {
            guard try seg(name).vmsize == 0 else { throw MachOError.unsupported("kernel \(name) is not empty; it is already a prelinked kernelcache") }
        }
        guard lastDataConst.vmsize > 0, lastDataConst.vmEnd == last.vmaddr, last.vmaddr <= prelinkData.vmaddr else {
            throw MachOError.unsupported("kernel segment order is not __LASTDATA_CONST, __LAST, ..., __PRELINK_DATA")
        }
        for s in mapped {
            let rw = s.maxprot & VMProt.write != 0
            let inData = s.vmaddr >= last.vmaddr && s.vmEnd <= prelinkData.vmaddr
            if rw != inData { throw MachOError.unsupported("segment \(s.name) is \(rw ? "writable" : "read-only") but falls \(inData ? "inside" : "outside") the __DATA region") }
        }

        // Kernel collection UUID: derived from the kernel's bytes so that a
        // rebuild of the same kernel yields the same collection.
        let uuid = contentUUID(bytes, salt: options.entryID)
        let plist = prelinkInfo(uuid: uuid)

        // Sizes of the header region, computed from the commands we emit.
        let topCount = 8
        let entryIDOffset = 32
        let entrySize = roundUp(entryIDOffset + options.entryID.utf8.count + 1, 8)
        let sizeOfCommands = topCount * Segment64.commandSize + Section64.size  // __PRELINK_INFO,__info
            + 16  // LC_DYLD_CHAINED_FIXUPS
            + 24  // LC_UUID
            + entrySize  // LC_FILESET_ENTRY
        let h = roundUp(UInt64(MH.headerSize + sizeOfCommands), page)
        let d = h + roundUp(UInt64(plist.count), page)
        func moved(_ vm: UInt64) -> UInt64 { vm - link + d }  // kernel VM address -> collection offset

        // Rebases, in collection offsets.
        let rebases = pointers.map { KernelCacheChains.Rebase(location: moved($0.address), target: moved($0.value)) }

        // Top-level segments. The fixup blob size is only known after
        // encoding, so __LINKEDIT's size is filled in afterwards.
        let kernelEnd = d + kernelSpan
        var top = [
            TopSegment(name: "__TEXT", offset: 0, size: h, prot: VMProt.read),
            TopSegment(name: "__PRELINK_INFO", offset: h, size: d - h, prot: VMProt.read | VMProt.write,
                       section: ("__info", UInt64(plist.count))),
            TopSegment(name: "__PRELINK_TEXT", offset: d, size: 0, prot: VMProt.read),
            TopSegment(name: "__KERNEL", offset: d, size: moved(lastDataConst.vmaddr) - d, prot: VMProt.read | VMProt.execute),
            TopSegment(name: "__DATA_CONST", offset: moved(lastDataConst.vmaddr), size: lastDataConst.vmsize, prot: VMProt.read),
            TopSegment(name: "__TEXT_EXEC", offset: moved(last.vmaddr), size: 0, prot: VMProt.read | VMProt.execute),
            TopSegment(name: "__DATA", offset: moved(last.vmaddr), size: prelinkData.vmaddr - last.vmaddr, prot: VMProt.read | VMProt.write),
            TopSegment(name: "__LINKEDIT", offset: moved(prelinkData.vmaddr), size: kernelEnd - moved(prelinkData.vmaddr), prot: VMProt.read),
        ]
        precondition(top.count == topCount)

        // The image: header region, plist, kernel with bss materialised.
        var out = [UInt8](repeating: 0, count: Int(kernelEnd))
        out.replaceSubrange(Int(h)..<Int(h) + plist.count, with: plist)
        for s in k.segments where s.filesize > 0 {
            let dst = Int(moved(s.vmaddr))
            out.replaceSubrange(dst..<dst + Int(s.filesize), with: bytes[Int(s.fileoff)..<Int(s.fileoff + s.filesize)])
        }
        try rewriteKernelHeader(&out, k, at: Int(d), link: link, delta: d, moved: moved)

        // Chained fixups, appended to __LINKEDIT.
        let regions = top.map { KernelCacheChains.Region(offset: $0.offset, size: $0.size) }
        let blob = try KernelCacheChains.encode(image: &out, regions: regions, rebases: rebases)
        let fixupsOffset = out.count
        out.append(contentsOf: blob)
        out.pad(toMultipleOf: Int(page))
        top[top.count - 1] = TopSegment(name: "__LINKEDIT", offset: top[top.count - 1].offset,
                                        size: UInt64(out.count) - top[top.count - 1].offset, prot: VMProt.read)

        // Collection header and load commands.
        var cmds: [UInt8] = []
        for t in top {
            let nsects: UInt32 = t.section == nil ? 0 : 1
            cmds.append32(LC.segment64)
            cmds.append32(UInt32(Segment64.commandSize + Int(nsects) * Section64.size))
            cmds.appendName16(t.name)
            cmds.append64(link + t.offset)
            cmds.append64(t.size)
            cmds.append64(t.offset)
            cmds.append64(t.size)
            cmds.append32(t.prot)
            cmds.append32(t.prot)
            cmds.append32(nsects)
            cmds.append32(0)
            if let sect = t.section {
                cmds.appendName16(sect.name)
                cmds.appendName16(t.name)
                cmds.append64(link + t.offset)
                cmds.append64(sect.size)
                cmds.append32(UInt32(t.offset))
                cmds.append32(0)  // align 2^0
                cmds.append32(0)  // reloff
                cmds.append32(0)  // nreloc
                cmds.append32(0)  // flags: S_REGULAR
                cmds.append32(0)
                cmds.append32(0)
                cmds.append32(0)
            }
        }
        cmds.append32(LC.dyldChainedFixups)
        cmds.append32(16)
        cmds.append32(UInt32(fixupsOffset))
        cmds.append32(UInt32(blob.count))
        cmds.append32(LC.uuid)
        cmds.append32(24)
        cmds.append(contentsOf: uuid)
        cmds.append32(LC.filesetEntry)
        cmds.append32(UInt32(entrySize))
        cmds.append64(link + d)  // vmaddr of the entry's mach header
        cmds.append64(d)  // fileoff
        cmds.append32(UInt32(entryIDOffset))
        cmds.append32(0)  // reserved
        cmds.append(contentsOf: Array(options.entryID.utf8) + [0])
        cmds.pad(toMultipleOf: 8)
        if cmds.count % 8 != 0 || cmds.count != sizeOfCommands {
            throw MachOError.malformed("internal: load commands are \(cmds.count) bytes, planned \(sizeOfCommands)")
        }

        try out.put32(0, MH.magic64)
        try out.put32(4, CPU.arm64)
        try out.put32(8, k.cpusubtype)
        try out.put32(12, MH.fileset)
        try out.put32(16, UInt32(topCount + 3))
        try out.put32(20, UInt32(cmds.count))
        try out.put32(24, MH.noUndefs)
        try out.put32(28, 0)
        out.replaceSubrange(MH.headerSize..<MH.headerSize + cmds.count, with: cmds)

        var report = KernelCollectionReport()
        report.linkAddress = link
        report.kernelOffset = d
        report.fixups = rebases.count
        report.size = out.count
        report.uuid = uuid
        report.segments = top.map { ($0.name, $0.offset, $0.size) }
        return (out, report)
    }

    /// Rewrites the kernel's own header in place after translating it by
    /// `delta`: segment and section addresses, flat file offsets, symbol
    /// values, the entry point, linkedit offsets, and the fileset flag.
    static func rewriteKernelHeader(_ out: inout [UInt8], _ k: MachOImage, at base: Int, link: UInt64, delta: UInt64,
                                    moved: (UInt64) -> UInt64) throws {
        guard let oldLinkedit = k.segment(named: "__LINKEDIT") else { throw MachOError.malformed("kernel has no __LINKEDIT") }
        let newLinkeditOff = moved(oldLinkedit.vmaddr)
        func movedFileOffset(_ off: UInt32, _ what: String) throws -> UInt32 {
            if off == 0 { return 0 }
            let o = UInt64(off)
            guard o >= oldLinkedit.fileoff, o <= oldLinkedit.fileoff + oldLinkedit.filesize else {
                throw MachOError.malformed("\(what) offset \(off) is outside the kernel __LINKEDIT")
            }
            return UInt32(o - oldLinkedit.fileoff + newLinkeditOff)
        }

        try out.put32(base + 24, k.flags | MH.dylibInCache)
        for lc in k.commands {
            let at = base + lc.offset  // the kernel's header sits at file offset 0 of the kernel
            switch lc.cmd {
            case LC.segment64:
                let s = try MachOImage.segment(k.bytes, lc.offset, lc.size)
                let vm = s.vmaddr + delta
                let fileoff: UInt64 = s.vmaddr >= link ? moved(s.vmaddr) : 0
                try out.put64(at + 24, vm)
                try out.put64(at + 40, fileoff)
                try out.put64(at + 48, s.vmsize)  // flat: filesize == vmsize
                for sec in s.sections {
                    let so = base + sec.commandOffset
                    try out.put64(so + 32, sec.addr + delta)
                    let offset: UInt64 = sec.isZeroFill || sec.addr < link ? 0 : moved(sec.addr)
                    try out.put32(so + 48, UInt32(offset))
                    try out.put32(so + 56, 0)  // reloff
                    try out.put32(so + 60, 0)  // nreloc
                }
            case LC.symtab:
                let st = try SymtabCommand(k.bytes, lc)
                let symoff = try movedFileOffset(st.symoff, "symbol table")
                try out.put32(at + 8, symoff)
                try out.put32(at + 16, try movedFileOffset(st.stroff, "string table"))
                for i in 0..<Int(st.nsyms) {
                    let e = Int(symoff) + i * NList.size
                    let type = try out.u8(e + 4)
                    if type & NList.stab == 0 && type & NList.typeMask == NList.sect {
                        try out.put64(e + 8, try out.u64(e + 8) + delta)
                    }
                }
            case LC.dysymtab:
                // The local relocations are consumed; the chained fixups replace them.
                try out.put32(at + Dysymtab.locreloff, 0)
                try out.put32(at + Dysymtab.nlocrel, 0)
                for field in [Dysymtab.tocoff, Dysymtab.modtaboff, Dysymtab.extrefsymoff, Dysymtab.indirectsymoff, Dysymtab.extreloff] {
                    try out.put32(at + field, try movedFileOffset(try k.bytes.u32(lc.offset + field), "dysymtab"))
                }
            case LC.unixThread:
                guard try k.bytes.u32(lc.offset + 8) == ThreadState.arm64Flavor else { throw MachOError.unsupported("LC_UNIXTHREAD flavor") }
                try out.put64(at + ThreadState.arm64PC, try k.bytes.u64(lc.offset + ThreadState.arm64PC) + delta)
            case _ where linkeditCommands.contains(lc.cmd):
                try out.put32(at + 8, try movedFileOffset(try k.bytes.u32(lc.offset + 8), "linkedit data"))
            default:
                break
            }
        }
    }

    static func prelinkInfo(uuid: [UInt8]) -> [UInt8] {
        let xml = """
            <?xml version="1.0" encoding="UTF-8"?>
            <!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
            <plist version="1.0">
            <dict>
            \t<key>_PrelinkInfoDictionary</key>
            \t<array/>
            \t<key>_PrelinkKCID</key>
            \t<data>\(Base64.encode(uuid))</data>
            </dict>
            </plist>

            """
        return Array(xml.utf8) + [0]
    }

    /// Two FNV-1a-64 passes with different offsets, shaped as an RFC 4122
    /// name-based UUID. Stable, not cryptographic: it names a collection.
    static func contentUUID(_ bytes: [UInt8], salt: String) -> [UInt8] {
        func fnv(_ seed: UInt64) -> UInt64 {
            var h = seed
            for b in Array(salt.utf8) + bytes {
                h ^= UInt64(b)
                h = h &* 0x0000_0100_0000_01b3
            }
            return h
        }
        var out: [UInt8] = []
        for h in [fnv(0xcbf2_9ce4_8422_2325), fnv(0x84222325_cbf29ce4)] {
            for i in 0..<8 { out.append(UInt8(truncatingIfNeeded: h >> (56 - 8 * i))) }
        }
        out[6] = out[6] & 0x0f | 0x50
        out[8] = out[8] & 0x3f | 0x80
        return out
    }
}
