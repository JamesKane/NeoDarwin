// SPDX-License-Identifier: BSD-2-Clause
//
// A small arm64 MH_EXECUTE with the segment layout, relocation style and
// load commands of an XNU kernel, so kcgen and kcheck can be exercised in
// seconds without a kernel build.

import MachO

struct SyntheticKernel {
    static let link: UInt64 = 0xffff_fe00_0700_4000
    static let page: UInt64 = 0x4000

    struct Seg {
        let name: String
        let pages: UInt64  // vmsize in pages
        let filePages: UInt64
        let prot: UInt32
        var vmaddr: UInt64 = 0
        var fileoff: UInt64 = 0
    }

    /// Pointers to plant: (segment, offset in segment, target segment, offset in target).
    typealias Pointer = (String, UInt64, String, UInt64)

    var relocationOverride: ((inout [UInt8], Int) -> Void)? = nil

    func build() -> [UInt8] {
        let r = VMProt.read, w = VMProt.write, x = VMProt.execute
        var segs = [
            Seg(name: "__TEXT", pages: 2, filePages: 2, prot: r),
            Seg(name: "__DATA_CONST", pages: 2, filePages: 2, prot: r),
            Seg(name: "__TEXT_EXEC", pages: 3, filePages: 3, prot: r | x),
            Seg(name: "__KLD", pages: 1, filePages: 1, prot: r | x),
            Seg(name: "__LASTDATA_CONST", pages: 1, filePages: 1, prot: r),
            Seg(name: "__LAST", pages: 0, filePages: 0, prot: r | x),
            Seg(name: "__KLDDATA", pages: 1, filePages: 1, prot: r | w),
            Seg(name: "__DATA", pages: 3, filePages: 1, prot: r | w),  // two pages of bss
            Seg(name: "__BOOTDATA", pages: 1, filePages: 1, prot: r | w),
            Seg(name: "__PRELINK_TEXT", pages: 0, filePages: 0, prot: r | x),
            Seg(name: "__PRELINK_INFO", pages: 0, filePages: 0, prot: r | w),
            Seg(name: "__PLK_TEXT_EXEC", pages: 0, filePages: 0, prot: r | w),
            Seg(name: "__PRELINK_DATA", pages: 0, filePages: 0, prot: r | w),
            Seg(name: "__PLK_DATA_CONST", pages: 0, filePages: 0, prot: r | w),
            Seg(name: "__LINKINFO", pages: 1, filePages: 1, prot: r),
            Seg(name: "__LINKEDIT", pages: 1, filePages: 1, prot: r),
        ]
        var vm = Self.link, file: UInt64 = 0
        for i in segs.indices {
            segs[i].vmaddr = segs[i].name == "__PRELINK_TEXT" ? Self.link - 0x300_0000 : vm
            segs[i].fileoff = file
            vm += segs[i].pages * Self.page
            file += segs[i].filePages * Self.page
        }
        func seg(_ n: String) -> Seg { segs.first { $0.name == n }! }

        let pointers: [Pointer] = [
            ("__DATA_CONST", 0x10, "__TEXT_EXEC", 0x100),
            ("__DATA_CONST", 0x18, "__TEXT", 0x0),
            ("__DATA_CONST", 0x3ff8, "__DATA", 0x4000),  // last slot of a page, into bss
            ("__DATA_CONST", 0x4000, "__DATA_CONST", 0x10),
            ("__DATA_CONST", 0x7ff0, "__BOOTDATA", 0x4000),  // one past the end
            ("__LASTDATA_CONST", 0x8, "__KLD", 0x20),
            ("__KLDDATA", 0x0, "__TEXT_EXEC", 0x2000),
            ("__DATA", 0x100, "__DATA", 0x8000),
            ("__DATA", 0x108, "__LINKINFO", 0x0),
            ("__BOOTDATA", 0x40, "__TEXT_EXEC", 0x8ffc),
        ]

        var out = [UInt8](repeating: 0, count: Int(file))
        // Deterministic noise so byte comparisons mean something.
        var state: UInt32 = 0x1234_5678
        for i in 0..<out.count {
            state = state &* 1_664_525 &+ 1_013_904_223
            out[i] = UInt8(truncatingIfNeeded: state >> 24)
        }

        // __LINKEDIT: symbols, strings, relocations, function starts.
        let le = seg("__LINKEDIT")
        var linkedit: [UInt8] = []
        let symoff = Int(le.fileoff)
        let strings: [UInt8] = Array("\0_start\0_version\0kernel.c\0".utf8)
        // nlist_64: strx, type, sect, desc, value
        func nlist(_ strx: UInt32, _ type: UInt8, _ sect: UInt8, _ value: UInt64) {
            linkedit.append32(strx); linkedit.append(type); linkedit.append(sect); linkedit.append16(0); linkedit.append64(value)
        }
        nlist(1, 0x0f, 1, seg("__TEXT_EXEC").vmaddr + 0x100)  // N_SECT | N_EXT
        nlist(8, 0x03, 0, 0x2a)  // N_ABS | N_EXT: must not move
        nlist(17, 0x64, 0, 0)  // N_SO stab: must not move
        let stroff = symoff + linkedit.count
        linkedit.append(contentsOf: strings)
        linkedit.pad(toMultipleOf: 8)
        let locreloff = symoff + linkedit.count
        for p in pointers {
            let loc = seg(p.0).vmaddr + p.1
            linkedit.append32(UInt32(truncatingIfNeeded: Int64(loc - Self.link)))
            linkedit.append32(3 << 25)  // length 3 (8 bytes), local, ARM64_RELOC_UNSIGNED
            let at = Int(seg(p.0).fileoff + p.1)
            let value = seg(p.2).vmaddr + p.3
            for j in 0..<8 { out[at + j] = UInt8(truncatingIfNeeded: value >> (8 * UInt64(j))) }
        }
        let funcStarts = symoff + linkedit.count
        linkedit.append(contentsOf: [0x80, 0x02, 0, 0, 0, 0, 0, 0])
        out.replaceSubrange(symoff..<symoff + linkedit.count, with: linkedit)
        for i in (symoff + linkedit.count)..<Int(le.fileoff + le.filePages * Self.page) { out[i] = 0 }
        if let override = relocationOverride { override(&out, locreloff) }

        // Load commands.
        var cmds: [UInt8] = []
        var ncmds: UInt32 = 0
        for s in segs {
            let sects: [(String, UInt64, UInt64, UInt32)] = switch s.name {
            case "__TEXT_EXEC": [("__text", 0, s.pages * Self.page, 0x8000_0400)]
            case "__DATA": [("__data", 0, Self.page, 0), ("__bss", Self.page, 2 * Self.page, SectionType.zeroFill)]
            default: []
            }
            cmds.append32(LC.segment64)
            cmds.append32(UInt32(Segment64.commandSize + sects.count * Section64.size))
            cmds.appendName16(s.name)
            cmds.append64(s.vmaddr)
            cmds.append64(s.pages * Self.page)
            cmds.append64(s.fileoff)
            cmds.append64(s.filePages * Self.page)
            cmds.append32(s.prot)
            cmds.append32(s.prot)
            cmds.append32(UInt32(sects.count))
            cmds.append32(0)
            for (name, offset, size, flags) in sects {
                cmds.appendName16(name)
                cmds.appendName16(s.name)
                cmds.append64(s.vmaddr + offset)
                cmds.append64(size)
                cmds.append32(SectionType.isZeroFill(flags) ? 0 : UInt32(s.fileoff + offset))
                cmds.append32(2)
                cmds.append32(0); cmds.append32(0); cmds.append32(flags)
                cmds.append32(0); cmds.append32(0); cmds.append32(0)
            }
            ncmds += 1
        }
        cmds.append32(LC.symtab); cmds.append32(24)
        cmds.append32(UInt32(symoff)); cmds.append32(3); cmds.append32(UInt32(stroff)); cmds.append32(UInt32(strings.count))
        ncmds += 1
        cmds.append32(LC.dysymtab); cmds.append32(80)
        for field in [0, 0, 0, 2, 2, 0] { cmds.append32(UInt32(field)) }  // ilocalsym ... nundefsym
        for _ in 0..<10 { cmds.append32(0) }  // toc, modtab, extref, indirect, extrel
        cmds.append32(UInt32(locreloff)); cmds.append32(UInt32(pointers.count))
        ncmds += 1
        cmds.append32(LC.uuid); cmds.append32(24)
        cmds.append(contentsOf: (0..<16).map { UInt8($0 * 11) })
        ncmds += 1
        cmds.append32(LC.unixThread); cmds.append32(288)
        cmds.append32(ThreadState.arm64Flavor); cmds.append32(68)
        for i in 0..<34 { cmds.append64(i == 32 ? seg("__TEXT_EXEC").vmaddr + 0x100 : 0) }  // x0-x28, fp, lr, sp, pc, cpsr+pad
        ncmds += 1
        cmds.append32(LC.functionStarts); cmds.append32(16)
        cmds.append32(UInt32(funcStarts)); cmds.append32(8)
        ncmds += 1

        var header: [UInt8] = []
        header.append32(MH.magic64); header.append32(CPU.arm64); header.append32(0); header.append32(MH.execute)
        header.append32(ncmds); header.append32(UInt32(cmds.count)); header.append32(MH.noUndefs | MH.pie); header.append32(0)
        out.replaceSubrange(0..<header.count + cmds.count, with: header + cmds)
        return out
    }
}
