// SPDX-License-Identifier: BSD-2-Clause
//
// Builds an MH_FILESET boot kernel collection from a linked XNU kernel and,
// optionally, kexts (MH_KEXT_BUNDLE) and codeless kexts (an Info.plist only).
//
// Layout (all offsets from the collection header, which the kernel expects
// at VM_KERNEL_LINK_ADDRESS; file offset == VM offset throughout):
//
//     0  __TEXT          collection header and load commands
//     H  __PRELINK_INFO  XML plist: _PrelinkInfoDictionary, _PrelinkKCID
//     D  __PRELINK_TEXT  each kext's __TEXT (its mach header first)
//     K  __KERNEL        kernel __TEXT, __DATA_CONST, __TEXT_EXEC, __KLD
//        __DATA_CONST    kernel __LASTDATA_CONST, then each kext's __DATA_CONST
//        __TEXT_EXEC     kernel __LAST (empty), then each kext's __TEXT_EXEC
//        __DATA          kernel __KLDDATA, __DATA, __BOOTDATA (bss materialised),
//                        kernel __PRELINK_DATA (empty), then each kext's __DATA
//        __LINKEDIT      kernel __LINKINFO and __LINKEDIT, each kext's
//                        __LINKEDIT, then the chained fixups
//
// arm_vm_init() derives the kext regions from these: PLK_DATA_CONST runs from
// the kernel's __LASTDATA_CONST to the end of the top-level __DATA_CONST,
// PLK_TEXT_EXEC from the kernel's __LAST to the end of __TEXT_EXEC, and
// PRELINK_DATA from the kernel's __PRELINK_DATA to the end of __DATA. With
// kexts the kernel's segments therefore move apart: each image's segments
// move by their own constant, and the references between them are fixed up
// from the image's split-segment info (LC_SEGMENT_SPLIT_INFO, the format
// kmutil uses: ADRP and branch immediates, 32- and 64-bit deltas, image
// offsets). All moves are multiples of 16 KiB, so page offsets (the ADD and
// LDR halves of ADRP pairs) never change. With no kexts every kernel segment
// moves by D and the collection is the kernel translated by one constant.
//
// Pointers become kernel-cache chained fixups, which arm_init() applies
// itself (arm_slide_rebase_and_sign_image): the kernel's and each kext's
// local relocations, and each kext's external relocations, resolved against
// the kernel's exported symbols. A kext is entered in __PRELINK_INFO as
// kmutil enters it (_PrelinkExecutableLoadAddr, _PrelinkKmodInfo, ...) and
// its kmod_info gets its __TEXT address and size; a codeless kext carries
// kOSKextCodelessKextLoadAddr. __KERNEL is NeoDarwin's name for the stretch
// the kernel never looks up by name.

import MachO

public struct KernelCollectionOptions: Sendable {
    public var entryID = "com.apple.kernel"
    public var pageSize = KernelCacheChains.pageSize
    /// Replaces ###KERNEL_VERSION_LONG### and ###KERNEL_VERSION_SHORT### in
    /// Info.plists (xnu's System.kext pseudo-kexts carry them).
    public var kernelVersion = ""
    public init() {}
}

/// A kext for the collection: its Info.plist, and its executable unless it
/// is codeless.
public struct KextInput: Sendable {
    public var infoPlist: String
    public var executable: [UInt8]?
    /// The bundle's path on the system, e.g. /System/Library/Extensions/zfs.kext.
    public var bundlePath: String
    /// The executable's path inside the bundle, e.g. Contents/MacOS/zfs.
    public var executableRelativePath: String?

    public init(infoPlist: String, executable: [UInt8]?, bundlePath: String, executableRelativePath: String? = nil) {
        self.infoPlist = infoPlist
        self.executable = executable
        self.bundlePath = bundlePath
        self.executableRelativePath = executableRelativePath
    }
}

public struct KernelCollectionReport: Sendable {
    public struct Kext: Sendable {
        public var identifier: String
        public var textAddress: UInt64
        public var imports: Int
        public var adjusted: Int
    }
    public var linkAddress: UInt64 = 0
    public var kernelOffset: UInt64 = 0
    public var fixups = 0
    public var size = 0
    public var uuid: [UInt8] = []
    public var segments: [(name: String, offset: UInt64, size: UInt64)] = []
    public var kexts: [Kext] = []
    public var codeless: [String] = []
    /// References the kernel's split-segment info fixed up because the
    /// kernel's segments moved apart.
    public var kernelAdjusted = 0
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
    static let codelessLoadAddress: UInt64 = 0x7fff_ffff_ffff_ffff  // kOSKextCodelessKextLoadAddr
    static let kextSegments: Set<String> = ["__TEXT", "__TEXT_EXEC", "__DATA", "__DATA_CONST", "__LINKEDIT"]

    /// One Mach-O image placed in the collection: the new VM address of each
    /// of its segments.
    struct Placed {
        let image: MachOImage
        var newVM: [UInt64]
        /// Old and new VM address of each split-segment section index (0 is the header).
        var sectionOld: [UInt64] = []
        var sectionNew: [UInt64] = []

        mutating func indexSections() {
            sectionOld = [image.segments[0].vmaddr]
            sectionNew = [newVM[0]]
            for (si, sec) in image.allSections {
                sectionOld.append(sec.addr)
                sectionNew.append(newVM[si] &+ (sec.addr &- image.segments[si].vmaddr))
            }
        }

        /// New address of an old one, by the segment holding it. An address on
        /// the boundary of two segments that moved apart is ambiguous.
        func map(_ old: UInt64, what: String) throws -> UInt64 {
            var found: UInt64? = nil
            for (i, s) in image.segments.enumerated() where s.vmsize > 0 && old >= s.vmaddr && old <= s.vmEnd {
                let n = newVM[i] &+ (old - s.vmaddr)
                if let f = found, f != n {
                    throw MachOError.unsupported("\(what): 0x\(String(old, radix: 16)) is on the boundary of segments that moved apart, and no split-segment info names its section")
                }
                found = n
            }
            guard let found else { throw MachOError.malformed("\(what): 0x\(String(old, radix: 16)) is outside the image") }
            return found
        }

        /// New address of an 8-byte slot (a relocation's location).
        func mapSlot(_ old: UInt64, what: String) throws -> UInt64 {
            for (i, s) in image.segments.enumerated() where s.vmsize >= 8 && old >= s.vmaddr && old <= s.vmEnd - 8 {
                return newVM[i] &+ (old - s.vmaddr)
            }
            throw MachOError.malformed("\(what): slot 0x\(String(old, radix: 16)) is outside the image")
        }

        /// New collection offset of an old file offset.
        func movedFileOffset(_ off: UInt32, link: UInt64) throws -> UInt32 {
            if off == 0 { return 0 }
            let o = UInt64(off)
            for (i, s) in image.segments.enumerated() where s.filesize > 0 && o >= s.fileoff && o <= s.fileoff + s.filesize {
                return UInt32(newVM[i] - link + (o - s.fileoff))
            }
            throw MachOError.malformed("file offset \(off) is outside every segment")
        }
    }

    struct Kext {
        let input: KextInput
        let identifier: String
        let body: String  // the Info.plist's top-level dictionary, without <dict> and </dict>
        var placed: Placed?
    }

    public static func build(kernel bytes: [UInt8], options: KernelCollectionOptions = .init()) throws -> ([UInt8], KernelCollectionReport) {
        try build(kernel: bytes, kexts: [], options: options)
    }

    public static func build(kernel bytes: [UInt8], kexts inputs: [KextInput],
                             options: KernelCollectionOptions = .init()) throws -> ([UInt8], KernelCollectionReport) {
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
                if lc.cmd == LC.dyldChainedFixups || lc.cmd == LC.codeSignature {
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

        guard let kdys = k.command(LC.dysymtab), k.command(LC.symtab) != nil, k.command(LC.unixThread) != nil else {
            throw MachOError.malformed("kernel lacks LC_SYMTAB, LC_DYSYMTAB or LC_UNIXTHREAD")
        }
        guard try bytes.u32(kdys.offset + Dysymtab.nextrel) == 0 else { throw MachOError.unsupported("kernel has external relocations") }

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

        // Kexts: Info.plists, and the images of those with code.
        var kexts: [Kext] = []
        for input in inputs {
            let plist = input.infoPlist
                .replacingAll("###KERNEL_VERSION_LONG###", with: options.kernelVersion)
                .replacingAll("###KERNEL_VERSION_SHORT###", with: options.kernelVersion)
            let body = try plistDictionaryBody(plist)
            let id = try plistString(body, key: "CFBundleIdentifier")
            if kexts.contains(where: { $0.identifier == id }) || id == options.entryID {
                throw MachOError.malformed("two kexts are named \(id)")
            }
            var kext = Kext(input: input, identifier: id, body: body, placed: nil)
            if let exe = input.executable {
                let img = try MachOImage(bytes: exe)
                guard img.filetype == MH.kextBundle, img.cputype == CPU.arm64 else {
                    throw MachOError.unsupported("\(id): executable is not an arm64 MH_KEXT_BUNDLE")
                }
                guard let first = img.segments.first, first.name == "__TEXT", first.vmaddr == 0, first.fileoff == 0 else {
                    throw MachOError.malformed("\(id): the first segment must be __TEXT at address 0")
                }
                for s in img.segments {
                    guard kextSegments.contains(s.name) else { throw MachOError.unsupported("\(id): segment \(s.name)") }
                    guard s.vmaddr % page == 0, s.filesize <= s.vmsize || s.name == "__LINKEDIT" else {
                        throw MachOError.malformed("\(id): segment \(s.name) is unaligned or larger on disk than in memory")
                    }
                    guard img.segments.filter({ $0.name == s.name }).count == 1 else { throw MachOError.malformed("\(id): two \(s.name) segments") }
                }
                for lc in img.commands where lc.cmd == LC.dyldChainedFixups || lc.cmd == LC.dyldInfo || lc.cmd == LC.dyldInfoOnly {
                    throw MachOError.unsupported("\(id): load command 0x\(String(lc.cmd, radix: 16)); kcgen links kexts with classic relocations")
                }
                guard img.command(LC.segmentSplitInfo) != nil else {
                    throw MachOError.unsupported("\(id): no split-segment info; its segments cannot move apart")
                }
                kext.placed = Placed(image: img, newVM: Array(repeating: 0, count: img.segments.count))
            }
            kexts.append(kext)
        }
        let coded = kexts.indices.filter { kexts[$0].placed != nil }
        func kextSegment(_ ki: Int, _ name: String) -> Int? { kexts[ki].placed!.image.segments.firstIndex { $0.name == name } }
        func kextSize(_ ki: Int, _ name: String) -> UInt64 {
            guard let si = kextSegment(ki, name) else { return 0 }
            let s = kexts[ki].placed!.image.segments[si]
            return roundUp(name == "__LINKEDIT" ? s.filesize : s.vmsize, page)
        }

        // Kernel collection UUID: derived from the inputs' bytes so that a
        // rebuild of the same inputs yields the same collection.
        var uuidInput = bytes
        for kext in kexts {
            uuidInput += Array(kext.identifier.utf8) + Array(kext.body.utf8)
            if let exe = kext.input.executable { uuidInput += exe }
        }
        let uuid = contentUUID(uuidInput, salt: options.entryID)

        // Sizes of the header region and the plist (whose numbers are fixed-width).
        let topCount = 8
        let entryIDOffset = 32
        func entrySize(_ id: String) -> Int { roundUp(entryIDOffset + id.utf8.count + 1, 8) }
        let entryIDs = [options.entryID] + coded.map { kexts[$0].identifier }
        let sizeOfCommands = topCount * Segment64.commandSize + Section64.size  // __PRELINK_INFO,__info
            + 16  // LC_DYLD_CHAINED_FIXUPS
            + 24  // LC_UUID
            + entryIDs.reduce(0) { $0 + entrySize($1) }  // LC_FILESET_ENTRY
        let h = roundUp(UInt64(MH.headerSize + sizeOfCommands), page)
        let plistSize = prelinkInfo(uuid: uuid, kexts: kexts).count
        let d = h + roundUp(UInt64(plistSize), page)

        // Placement. Kext segments go into the top-level regions in input order.
        var at = d
        for ki in coded {
            kexts[ki].placed!.newVM[0] = link + at
            at += kextSize(ki, "__TEXT")
        }
        let kd = at  // the kernel's mach header
        let dcKexts = coded.reduce(0) { $0 + kextSize($1, "__DATA_CONST") }
        let teKexts = coded.reduce(0) { $0 + kextSize($1, "__TEXT_EXEC") }
        // arm_vm_init() wants PLK_DATA_CONST and PLK_TEXT_EXEC both empty or both not.
        let dcPad: UInt64 = dcKexts == 0 && teKexts > 0 ? page : 0
        let tePad: UInt64 = teKexts == 0 && dcKexts > 0 ? page : 0
        at = kd + (lastDataConst.vmEnd - link)
        for ki in coded {
            if let si = kextSegment(ki, "__DATA_CONST") { kexts[ki].placed!.newVM[si] = link + at; at += kextSize(ki, "__DATA_CONST") }
        }
        let dcEnd = at + dcPad
        at = dcEnd + last.vmsize
        for ki in coded {
            if let si = kextSegment(ki, "__TEXT_EXEC") { kexts[ki].placed!.newVM[si] = link + at; at += kextSize(ki, "__TEXT_EXEC") }
        }
        let teEnd = at + tePad
        let cDelta = teEnd &- (last.vmEnd - link)  // kernel __KLDDATA ... __PRELINK_DATA
        at = teEnd + (prelinkData.vmaddr - last.vmEnd)
        for ki in coded {
            if let si = kextSegment(ki, "__DATA") { kexts[ki].placed!.newVM[si] = link + at; at += kextSize(ki, "__DATA") }
        }
        let daEnd = at
        let dDelta = daEnd &- (prelinkData.vmaddr - link)  // kernel __LINKINFO, __LINKEDIT
        at = daEnd + (link + kernelSpan - prelinkData.vmaddr)
        for ki in coded {
            if let si = kextSegment(ki, "__LINKEDIT") { kexts[ki].placed!.newVM[si] = link + at; at += kextSize(ki, "__LINKEDIT") }
        }
        let imageEnd = at

        var kp = Placed(image: k, newVM: [])
        for s in k.segments {
            let newVM: UInt64
            if s.name == "__LAST" {
                newVM = link + dcEnd
            } else if s.vmaddr < last.vmaddr || (s.vmaddr == last.vmaddr && s.name == "__LASTDATA_CONST") {
                newVM = s.vmaddr &+ kd
            } else if s.vmsize == 0 || s.vmaddr < prelinkData.vmaddr {
                newVM = s.vmaddr &+ cDelta
            } else {
                newVM = s.vmaddr &+ dDelta
            }
            kp.newVM.append(newVM)
        }
        kp.indexSections()
        for ki in coded { kexts[ki].placed!.indexSections() }

        // The image: header region, kernel and kexts with bss materialised.
        var out = [UInt8](repeating: 0, count: Int(imageEnd))
        func copySegments(_ p: Placed) {
            for (i, s) in p.image.segments.enumerated() where s.filesize > 0 {
                let dst = Int(p.newVM[i] - link)
                out.replaceSubrange(dst..<dst + Int(s.filesize), with: p.image.bytes[Int(s.fileoff)..<Int(s.fileoff + s.filesize)])
            }
        }
        copySegments(kp)
        for ki in coded { copySegments(kexts[ki].placed!) }

        // References between sections that moved apart; pointer targets by section.
        var report = KernelCollectionReport()
        func adjust(_ p: Placed, _ what: String) throws -> (pointers: [UInt64: UInt64], adjusted: Int) {
            var pointers: [UInt64: UInt64] = [:]
            var adjusted = 0
            guard let refs = try SplitSegInfo.references(p.image) else { return (pointers, 0) }
            let imageNew = p.sectionNew[0], imageOld = p.sectionOld[0]
            for r in refs {
                let fromOld = p.sectionOld[r.fromSection] &+ r.fromOffset, fromNew = p.sectionNew[r.fromSection] &+ r.fromOffset
                let toOld = p.sectionOld[r.toSection] &+ r.toOffset, toNew = p.sectionNew[r.toSection] &+ r.toOffset
                let delta = Int64(bitPattern: (toNew &- toOld) &- (fromNew &- fromOld))
                let loc = Int(fromNew &- link)
                let place = "\(what): split-segment kind \(r.kind) at 0x\(String(fromOld, radix: 16))"
                switch r.kind {
                case SplitSegKind.pointer64:
                    pointers[fromOld] = toNew
                case SplitSegKind.delta32:
                    guard delta != 0 else { continue }
                    let v = Int64(Int32(bitPattern: try out.u32(loc))) + delta
                    guard v >= Int64(Int32.min), v <= Int64(Int32.max) else { throw MachOError.unsupported("\(place): delta out of range") }
                    try out.put32(loc, UInt32(bitPattern: Int32(v)))
                    adjusted += 1
                case SplitSegKind.delta64:
                    guard delta != 0 else { continue }
                    try out.put64(loc, try out.u64(loc) &+ UInt64(bitPattern: delta))
                    adjusted += 1
                case SplitSegKind.arm64ADRP:
                    guard delta != 0 else { continue }
                    let ins = try out.u32(loc)
                    guard ins & 0x9f00_0000 == 0x9000_0000 else { throw MachOError.unsupported("\(place): not an ADRP (0x\(String(ins, radix: 16)))") }
                    let pages = (Int64(bitPattern: toNew & ~0xfff) - Int64(bitPattern: fromNew & ~0xfff)) >> 12
                    guard pages >= -(1 << 20), pages < (1 << 20) else { throw MachOError.unsupported("\(place): ADRP out of range") }
                    let imm = UInt32(truncatingIfNeeded: pages)
                    try out.put32(loc, ins & 0x9f00_001f | (imm & 0x3) << 29 | ((imm >> 2) & 0x7ffff) << 5)
                    adjusted += 1
                case SplitSegKind.arm64Off12:
                    guard toNew & 0xfff == toOld & 0xfff else { throw MachOError.unsupported("\(place): page offset changed") }
                case SplitSegKind.arm64BR26:
                    guard delta != 0 else { continue }
                    let ins = try out.u32(loc)
                    let dist = Int64(bitPattern: toNew &- fromNew)
                    guard ins & 0x7c00_0000 == 0x1400_0000, dist >= -(1 << 27), dist < (1 << 27), dist % 4 == 0 else {
                        throw MachOError.unsupported("\(place): branch out of range or not a B/BL")
                    }
                    try out.put32(loc, ins & 0xfc00_0000 | UInt32(truncatingIfNeeded: dist >> 2) & 0x03ff_ffff)
                    adjusted += 1
                case SplitSegKind.imageOff32:
                    let oldValue = toOld &- imageOld, newValue = toNew &- imageNew
                    guard oldValue != newValue else { continue }
                    guard newValue <= UInt64(UInt32.max), UInt64(try out.u32(loc)) == oldValue else {
                        throw MachOError.unsupported("\(place): image offset 0x\(String(newValue, radix: 16))")
                    }
                    try out.put32(loc, UInt32(newValue))
                    adjusted += 1
                default:
                    guard delta == 0 else { throw MachOError.unsupported("\(place): this kind cannot be moved") }
                }
            }
            return (pointers, adjusted)
        }

        // Rebases: local relocations (the target's section from the
        // split-segment info, else by address), and kext imports.
        var rebases: [KernelCacheChains.Rebase] = []
        func localRebases(_ p: Placed, _ pointers: [UInt64: UInt64], _ what: String) throws {
            let img = p.image
            guard let dys = img.command(LC.dysymtab) else { return }
            let locreloff = Int(try img.bytes.u32(dys.offset + Dysymtab.locreloff))
            let nlocrel = Int(try img.bytes.u32(dys.offset + Dysymtab.nlocrel))
            let base = img.segments[0].vmaddr
            for i in 0..<nlocrel {
                let r = try Relocation(img.bytes, locreloff + i * Relocation.size)
                guard r.type == Relocation.arm64Unsigned, r.length == 3, !r.pcRelative, !r.isExtern else {
                    throw MachOError.unsupported("\(what) relocation \(i): type \(r.type) length \(r.length) pcrel \(r.pcRelative) extern \(r.isExtern)")
                }
                let address = base &+ UInt64(bitPattern: Int64(r.address))
                guard let at = img.fileOffset(of: address, width: 8) else {
                    throw MachOError.malformed("\(what) relocation \(i) at 0x\(String(address, radix: 16)) is not file-backed")
                }
                let value = try img.bytes.u64(at)
                guard img.segments.contains(where: { $0.vmsize > 0 && value >= $0.vmaddr && value <= $0.vmEnd }) else {
                    throw MachOError.malformed("\(what) relocation \(i) at 0x\(String(address, radix: 16)) holds 0x\(String(value, radix: 16)), outside the image")
                }
                let target = try pointers[address] ?? p.map(value, what: "\(what) relocation \(i)")
                rebases.append(.init(location: try p.mapSlot(address, what: what) - link, target: target - link))
            }
        }
        let kernelAdjust = try adjust(kp, "kernel")
        report.kernelAdjusted = kernelAdjust.adjusted
        try localRebases(kp, kernelAdjust.pointers, "kernel")

        // The kernel's exports, at their new addresses.
        var exports: [String: UInt64] = [:]
        for s in try k.symbols() where s.isDefinedInSection && s.isExternal && s.section > 0 && Int(s.section) < kp.sectionNew.count {
            exports[s.name] = kp.sectionNew[Int(s.section)] &+ (s.value &- kp.sectionOld[Int(s.section)])
        }

        for ki in coded {
            let p = kexts[ki].placed!
            let id = kexts[ki].identifier
            let a = try adjust(p, id)
            try localRebases(p, a.pointers, id)
            let syms = try p.image.symbols()
            guard let dys = p.image.command(LC.dysymtab) else { throw MachOError.malformed("\(id) has no LC_DYSYMTAB") }
            let extreloff = Int(try p.image.bytes.u32(dys.offset + Dysymtab.extreloff))
            let nextrel = Int(try p.image.bytes.u32(dys.offset + Dysymtab.nextrel))
            var missing = Set<String>()
            for i in 0..<nextrel {
                let r = try Relocation(p.image.bytes, extreloff + i * Relocation.size)
                guard r.type == Relocation.arm64Unsigned, r.length == 3, !r.pcRelative, r.isExtern, Int(r.symbolNum) < syms.count else {
                    throw MachOError.unsupported("\(id) external relocation \(i): type \(r.type) length \(r.length) pcrel \(r.pcRelative)")
                }
                let address = UInt64(bitPattern: Int64(r.address))
                guard let fo = p.image.fileOffset(of: address, width: 8) else { throw MachOError.malformed("\(id) external relocation \(i) is not file-backed") }
                let addend = try p.image.bytes.u64(fo)
                let sym = syms[Int(r.symbolNum)]
                let target: UInt64
                if sym.isDefinedInSection, sym.section > 0, Int(sym.section) < p.sectionNew.count {
                    target = p.sectionNew[Int(sym.section)] &+ (sym.value &- p.sectionOld[Int(sym.section)])
                } else if let t = exports[sym.name] {
                    target = t
                } else {
                    missing.insert(sym.name)
                    continue
                }
                rebases.append(.init(location: try p.mapSlot(address, what: id) - link, target: target &+ addend &- link))
            }
            guard missing.isEmpty else {
                throw MachOError.unsupported("\(id) imports \(missing.count) symbol(s) the kernel does not export: " + missing.sorted().prefix(40).joined(separator: " "))
            }
            report.kexts.append(.init(identifier: id, textAddress: p.newVM[0], imports: nextrel, adjusted: a.adjusted))

            // kmod_info: its __TEXT address and size, as kmutil sets them.
            if let kmodVM = kmodInfoAddress(p) {
                let at = Int(kmodVM - link)
                guard try out.u32(at + 8) == 1 else { throw MachOError.unsupported("\(id): kmod_info version is not 1") }
                guard !rebases.contains(where: { $0.location + 8 > UInt64(at + 156) && $0.location < UInt64(at + 172) }) else {
                    throw MachOError.malformed("\(id): kmod_info address or size has a relocation")
                }
                try out.put64(at + 156, p.newVM[0])  // address (kmod_info_t is #pragma pack(4))
                try out.put64(at + 164, p.image.segments[0].vmsize)  // size
            }
        }

        // Headers: kernel, then each kext.
        try rewriteHeader(&out, kp, link: link)
        for ki in coded { try rewriteHeader(&out, kexts[ki].placed!, link: link) }

        // Top-level segments.
        let dcStart = kd + (lastDataConst.vmaddr - link)
        var top = [
            TopSegment(name: "__TEXT", offset: 0, size: h, prot: VMProt.read),
            TopSegment(name: "__PRELINK_INFO", offset: h, size: d - h, prot: VMProt.read | VMProt.write,
                       section: ("__info", UInt64(plistSize))),
            TopSegment(name: "__PRELINK_TEXT", offset: d, size: kd - d, prot: VMProt.read),
            TopSegment(name: "__KERNEL", offset: kd, size: dcStart - kd, prot: VMProt.read | VMProt.execute),
            TopSegment(name: "__DATA_CONST", offset: dcStart, size: dcEnd - dcStart, prot: VMProt.read),
            TopSegment(name: "__TEXT_EXEC", offset: dcEnd, size: teEnd - dcEnd, prot: VMProt.read | VMProt.execute),
            TopSegment(name: "__DATA", offset: teEnd, size: daEnd - teEnd, prot: VMProt.read | VMProt.write),
            TopSegment(name: "__LINKEDIT", offset: daEnd, size: imageEnd - daEnd, prot: VMProt.read),
        ]
        precondition(top.count == topCount)

        // The plist, now that every address is known.
        let plist = prelinkInfo(uuid: uuid, kexts: kexts)
        precondition(plist.count == plistSize)
        out.replaceSubrange(Int(h)..<Int(h) + plist.count, with: plist)

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
        let entryAddresses = [link + kd] + coded.map { kexts[$0].placed!.newVM[0] }
        for (id, vm) in zip(entryIDs, entryAddresses) {
            cmds.append32(LC.filesetEntry)
            cmds.append32(UInt32(entrySize(id)))
            cmds.append64(vm)  // vmaddr of the entry's mach header
            cmds.append64(vm - link)  // fileoff
            cmds.append32(UInt32(entryIDOffset))
            cmds.append32(0)  // reserved
            cmds.append(contentsOf: Array(id.utf8) + [0])
            cmds.pad(toMultipleOf: 8)
        }
        if cmds.count % 8 != 0 || cmds.count != sizeOfCommands {
            throw MachOError.malformed("internal: load commands are \(cmds.count) bytes, planned \(sizeOfCommands)")
        }

        try out.put32(0, MH.magic64)
        try out.put32(4, CPU.arm64)
        try out.put32(8, k.cpusubtype)
        try out.put32(12, MH.fileset)
        try out.put32(16, UInt32(topCount + 2 + entryIDs.count))
        try out.put32(20, UInt32(cmds.count))
        try out.put32(24, MH.noUndefs)
        try out.put32(28, 0)
        out.replaceSubrange(MH.headerSize..<MH.headerSize + cmds.count, with: cmds)

        report.linkAddress = link
        report.kernelOffset = kd
        report.fixups = rebases.count
        report.size = out.count
        report.uuid = uuid
        report.segments = top.map { ($0.name, $0.offset, $0.size) }
        report.codeless = kexts.filter { $0.placed == nil }.map { $0.identifier }
        return (out, report)
    }

    static func hasKmodInfo(_ p: Placed) -> Bool {
        ((try? p.image.symbols()) ?? []).contains { $0.name == "_kmod_info" && $0.isDefinedInSection }
    }

    /// The new address of a placed kext's kmod_info, if it has one.
    static func kmodInfoAddress(_ p: Placed) -> UInt64? {
        guard let syms = try? p.image.symbols(),
              let s = syms.first(where: { $0.name == "_kmod_info" && $0.isDefinedInSection }),
              s.section > 0, Int(s.section) < p.sectionNew.count else { return nil }
        return p.sectionNew[Int(s.section)] &+ (s.value &- p.sectionOld[Int(s.section)])
    }

    /// Rewrites a placed image's own header in place: segment and section
    /// addresses, flat file offsets, symbol values, the entry point, linkedit
    /// offsets, and the fileset flag. Relocations are consumed (the chained
    /// fixups replace them).
    static func rewriteHeader(_ out: inout [UInt8], _ p: Placed, link: UInt64) throws {
        let img = p.image
        let base = Int(p.newVM[0] - link)  // the header starts the image's first segment
        try out.put32(base + 24, img.flags | MH.dylibInCache)
        for lc in img.commands {
            let at = base + lc.offset  // header and load commands are at file offset 0 of the image
            switch lc.cmd {
            case LC.segment64:
                let i = img.segments.firstIndex { $0.commandOffset == lc.offset }!
                let s = img.segments[i]
                let vm = p.newVM[i]
                try out.put64(at + 24, vm)
                try out.put64(at + 40, vm >= link ? vm - link : 0)
                try out.put64(at + 48, s.vmsize)  // flat: filesize == vmsize
                for sec in s.sections {
                    let so = base + sec.commandOffset
                    let addr = vm &+ (sec.addr &- s.vmaddr)
                    try out.put64(so + 32, addr)
                    try out.put32(so + 48, sec.isZeroFill || addr < link ? 0 : UInt32(addr - link))
                    try out.put32(so + 56, 0)  // reloff
                    try out.put32(so + 60, 0)  // nreloc
                }
            case LC.symtab:
                let st = try SymtabCommand(img.bytes, lc)
                let symoff = try p.movedFileOffset(st.symoff, link: link)
                try out.put32(at + 8, symoff)
                try out.put32(at + 16, try p.movedFileOffset(st.stroff, link: link))
                for i in 0..<Int(st.nsyms) {
                    let e = Int(symoff) + i * NList.size
                    let type = try out.u8(e + 4)
                    let n = Int(try out.u8(e + 5))
                    if type & NList.stab == 0 && type & NList.typeMask == NList.sect {
                        guard n > 0, n < p.sectionNew.count else { throw MachOError.malformed("symbol \(i) names section \(n)") }
                        try out.put64(e + 8, p.sectionNew[n] &+ (try out.u64(e + 8) &- p.sectionOld[n]))
                    }
                }
            case LC.dysymtab:
                for field in [Dysymtab.locreloff, Dysymtab.nlocrel, Dysymtab.extreloff, Dysymtab.nextrel] {
                    try out.put32(at + field, 0)
                }
                for field in [Dysymtab.tocoff, Dysymtab.modtaboff, Dysymtab.extrefsymoff, Dysymtab.indirectsymoff] {
                    try out.put32(at + field, try p.movedFileOffset(try img.bytes.u32(lc.offset + field), link: link))
                }
            case LC.unixThread:
                guard try img.bytes.u32(lc.offset + 8) == ThreadState.arm64Flavor else { throw MachOError.unsupported("LC_UNIXTHREAD flavor") }
                try out.put64(at + ThreadState.arm64PC, try p.map(try img.bytes.u64(lc.offset + ThreadState.arm64PC), what: "entry point"))
            case _ where linkeditCommands.contains(lc.cmd):
                try out.put32(at + 8, try p.movedFileOffset(try img.bytes.u32(lc.offset + 8), link: link))
            default:
                break
            }
        }
    }

    // MARK: - __PRELINK_INFO

    /// The prelink plist. Addresses are written as fixed-width hex, so the
    /// plist has the same size before and after placement.
    static func prelinkInfo(uuid: [UInt8], kexts: [Kext]) -> [UInt8] {
        func hex(_ v: UInt64) -> String {
            let s = String(v, radix: 16)
            return "<integer size=\"64\">0x" + String(repeating: "0", count: 16 - s.count) + s + "</integer>"
        }
        var entries = ""
        for kext in kexts {
            var extra = "\t\t<key>_PrelinkBundlePath</key>\n\t\t<string>\(xmlEscape(kext.input.bundlePath))</string>\n"
            if let p = kext.placed {
                let textVM = p.newVM[0]
                extra += "\t\t<key>_PrelinkExecutableLoadAddr</key>\n\t\t\(hex(textVM))\n"
                if let rel = kext.input.executableRelativePath {
                    extra += "\t\t<key>_PrelinkExecutableRelativePath</key>\n\t\t<string>\(xmlEscape(rel))</string>\n"
                }
                extra += "\t\t<key>_PrelinkExecutableSize</key>\n\t\t\(hex(p.image.segments[0].vmsize))\n"
                extra += "\t\t<key>_PrelinkExecutableSourceAddr</key>\n\t\t\(hex(textVM))\n"
                if hasKmodInfo(p) {
                    // Before placement there are no section addresses; the value is sized then.
                    extra += "\t\t<key>_PrelinkKmodInfo</key>\n\t\t\(hex(kmodInfoAddress(p) ?? 0))\n"
                }
            } else {
                extra += "\t\t<key>_PrelinkExecutableLoadAddr</key>\n\t\t\(hex(codelessLoadAddress))\n"
            }
            entries += "\t\t<dict>\n" + kext.body + "\n" + extra + "\t\t</dict>\n"
        }
        let array = kexts.isEmpty ? "\t<array/>" : "\t<array>\n" + entries + "\t</array>"
        let xml = """
            <?xml version="1.0" encoding="UTF-8"?>
            <!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
            <plist version="1.0">
            <dict>
            \t<key>_PrelinkInfoDictionary</key>
            \(array)
            \t<key>_PrelinkKCID</key>
            \t<data>\(Base64.encode(uuid))</data>
            </dict>
            </plist>

            """
        return Array(xml.utf8) + [0]
    }

    static func xmlEscape(_ s: String) -> String {
        s.replacingAll("&", with: "&amp;").replacingAll("<", with: "&lt;").replacingAll(">", with: "&gt;")
    }

    /// The contents of an Info.plist's top-level <dict>, without the tags.
    static func plistDictionaryBody(_ plist: String) throws -> String {
        let s = Array(plist.utf8)
        guard let p = s.firstRange(of: Array("<plist".utf8)), let gt = s[p.upperBound...].firstIndex(of: UInt8(ascii: ">")),
              let endPlist = s.lastRange(of: Array("</plist>".utf8)), endPlist.lowerBound > gt else {
            throw MachOError.malformed("Info.plist has no <plist> element")
        }
        let inner = String(decoding: s[(gt + 1)..<endPlist.lowerBound], as: UTF8.self).trimmingWhitespace()
        guard inner.hasPrefix("<dict>"), inner.hasSuffix("</dict>") else { throw MachOError.malformed("Info.plist's top level is not a <dict>") }
        return String(inner.dropFirst("<dict>".count).dropLast("</dict>".count))
    }

    /// The <string> value of a top-level key.
    static func plistString(_ body: String, key: String) throws -> String {
        let b = Array(body.utf8)
        guard let k = b.firstRange(of: Array("<key>\(key)</key>".utf8)),
              let open = b[k.upperBound...].firstRange(of: Array("<string>".utf8)),
              let close = b[open.upperBound...].firstRange(of: Array("</string>".utf8)),
              b[k.upperBound..<open.lowerBound].allSatisfy({ $0 == 0x20 || $0 == 0x09 || $0 == 0x0a || $0 == 0x0d }) else {
            throw MachOError.malformed("Info.plist has no \(key) string")
        }
        return String(decoding: b[open.upperBound..<close.lowerBound], as: UTF8.self)
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

// Foundation-free string helpers.
extension String {
    func replacingAll(_ target: String, with replacement: String) -> String {
        let t = Array(target.utf8), r = Array(replacement.utf8), s = Array(self.utf8)
        guard !t.isEmpty, s.count >= t.count else { return self }
        var out: [UInt8] = []
        out.reserveCapacity(s.count)
        var i = 0
        while i < s.count {
            if i + t.count <= s.count && s[i..<i + t.count].elementsEqual(t) {
                out += r
                i += t.count
            } else {
                out.append(s[i])
                i += 1
            }
        }
        return String(decoding: out, as: UTF8.self)
    }

    func trimmingWhitespace() -> String {
        let ws: Set<UInt8> = [0x20, 0x09, 0x0a, 0x0d]
        let s = Array(self.utf8)
        guard let a = s.firstIndex(where: { !ws.contains($0) }), let b = s.lastIndex(where: { !ws.contains($0) }) else { return "" }
        return String(decoding: s[a...b], as: UTF8.self)
    }
}

extension Array where Element == UInt8 {
    func lastRange(of needle: [UInt8]) -> Range<Int>? {
        guard !needle.isEmpty, needle.count <= count else { return nil }
        var i = count - needle.count
        while i >= 0 {
            if self[i..<i + needle.count].elementsEqual(needle) { return i..<i + needle.count }
            i -= 1
        }
        return nil
    }
}
