// SPDX-License-Identifier: BSD-2-Clause
//
// The subset of the 64-bit Mach-O format that kernel collections use, with
// the constants from EXTERNAL_HEADERS/mach-o/loader.h in the pinned xnu.

public enum MH {
    public static let magic64: UInt32 = 0xfeed_facf
    public static let execute: UInt32 = 0x2
    public static let fileset: UInt32 = 0xc
    public static let noUndefs: UInt32 = 0x1
    public static let pie: UInt32 = 0x20_0000
    /// Set on a kernel or kext inside a fileset; the kernel tests it with
    /// kernel_mach_header_is_in_fileset().
    public static let dylibInCache: UInt32 = 0x8000_0000
    public static let headerSize = 32
}

public enum CPU {
    public static let arm64: UInt32 = 0x0100_000c
}

public enum LC {
    public static let symtab: UInt32 = 0x2
    public static let unixThread: UInt32 = 0x5
    public static let dysymtab: UInt32 = 0xb
    public static let segment64: UInt32 = 0x19
    public static let uuid: UInt32 = 0x1b
    public static let codeSignature: UInt32 = 0x1d
    public static let segmentSplitInfo: UInt32 = 0x1e
    public static let dyldInfo: UInt32 = 0x22
    public static let dyldInfoOnly: UInt32 = 0x8000_0022
    public static let functionStarts: UInt32 = 0x26
    public static let dataInCode: UInt32 = 0x29
    public static let sourceVersion: UInt32 = 0x2a
    public static let buildVersion: UInt32 = 0x32
    public static let dyldChainedFixups: UInt32 = 0x8000_0034
    public static let filesetEntry: UInt32 = 0x8000_0035

    /// linkedit_data_command forms: {cmd, cmdsize, dataoff, datasize}.
    public static let linkeditData: Set<UInt32> = [
        codeSignature, segmentSplitInfo, functionStarts, dataInCode, dyldChainedFixups,
    ]
}

public enum VMProt {
    public static let read: UInt32 = 1
    public static let write: UInt32 = 2
    public static let execute: UInt32 = 4
}

public enum SectionType {
    public static let mask: UInt32 = 0xff
    public static let zeroFill: UInt32 = 0x1
    public static let gbZeroFill: UInt32 = 0xc
    public static let threadLocalZeroFill: UInt32 = 0x12

    public static func isZeroFill(_ flags: UInt32) -> Bool {
        let t = flags & mask
        return t == zeroFill || t == gbZeroFill || t == threadLocalZeroFill
    }
}

public enum NList {
    public static let size = 16
    public static let stab: UInt8 = 0xe0
    public static let typeMask: UInt8 = 0x0e
    public static let sect: UInt8 = 0x0e
}

public struct LoadCommand: Sendable {
    public let offset: Int
    public let cmd: UInt32
    public let size: Int
}

public struct Section64: Sendable {
    public static let size = 80
    public let commandOffset: Int
    public let name: String
    public let segmentName: String
    public let addr: UInt64
    public let size: UInt64
    public let offset: UInt32
    public let flags: UInt32

    public var isZeroFill: Bool { SectionType.isZeroFill(flags) }
}

public struct Segment64: Sendable {
    public static let commandSize = 72
    public let commandOffset: Int
    public let name: String
    public let vmaddr: UInt64
    public let vmsize: UInt64
    public let fileoff: UInt64
    public let filesize: UInt64
    public let maxprot: UInt32
    public let initprot: UInt32
    public let sections: [Section64]

    public var vmEnd: UInt64 { vmaddr &+ vmsize }

    public func contains(_ address: UInt64) -> Bool { address >= vmaddr && address < vmEnd }
}

/// A 64-bit Mach-O whose header starts at `base` inside `bytes`: 0 for a
/// standalone image, the entry's file offset for a fileset member.
public struct MachOImage: Sendable {
    public let bytes: [UInt8]
    public let base: Int
    public let cputype: UInt32
    public let cpusubtype: UInt32
    public let filetype: UInt32
    public let flags: UInt32
    public let sizeOfCommands: Int
    public let commands: [LoadCommand]
    public let segments: [Segment64]

    public init(bytes: [UInt8], base: Int = 0) throws {
        self.bytes = bytes
        self.base = base
        guard try bytes.u32(base) == MH.magic64 else {
            throw MachOError.malformed("no MH_MAGIC_64 at offset \(base)")
        }
        cputype = try bytes.u32(base + 4)
        cpusubtype = try bytes.u32(base + 8)
        filetype = try bytes.u32(base + 12)
        let ncmds = Int(try bytes.u32(base + 16))
        sizeOfCommands = Int(try bytes.u32(base + 20))
        flags = try bytes.u32(base + 24)

        let first = base + MH.headerSize
        let end = first + sizeOfCommands
        try bytes.check(first, sizeOfCommands)
        var commands: [LoadCommand] = []
        var segments: [Segment64] = []
        var at = first
        for i in 0..<ncmds {
            let cmd = try bytes.u32(at)
            let size = Int(try bytes.u32(at + 4))
            guard size >= 8, size % 8 == 0, at + size <= end else {
                throw MachOError.malformed("load command \(i) (0x\(String(cmd, radix: 16))) has size \(size)")
            }
            commands.append(LoadCommand(offset: at, cmd: cmd, size: size))
            if cmd == LC.segment64 {
                segments.append(try MachOImage.segment(bytes, at, size))
            }
            at += size
        }
        guard at == end else { throw MachOError.malformed("load commands end at \(at), sizeofcmds says \(end)") }
        self.commands = commands
        self.segments = segments
    }

    public static func segment(_ b: [UInt8], _ at: Int, _ size: Int) throws -> Segment64 {
        let nsects = Int(try b.u32(at + 64))
        guard size == Segment64.commandSize + nsects * Section64.size else {
            throw MachOError.malformed("segment command at \(at) has \(nsects) sections but size \(size)")
        }
        var sections: [Section64] = []
        for s in 0..<nsects {
            let o = at + Segment64.commandSize + s * Section64.size
            sections.append(Section64(
                commandOffset: o,
                name: try b.name16(o),
                segmentName: try b.name16(o + 16),
                addr: try b.u64(o + 32),
                size: try b.u64(o + 40),
                offset: try b.u32(o + 48),
                flags: try b.u32(o + 64)))
        }
        return Segment64(
            commandOffset: at,
            name: try b.name16(at + 8),
            vmaddr: try b.u64(at + 24),
            vmsize: try b.u64(at + 32),
            fileoff: try b.u64(at + 40),
            filesize: try b.u64(at + 48),
            maxprot: try b.u32(at + 56),
            initprot: try b.u32(at + 60),
            sections: sections)
    }

    public func segment(named name: String) -> Segment64? {
        segments.first { $0.name == name }
    }

    public func commands(_ cmd: UInt32) -> [LoadCommand] {
        commands.filter { $0.cmd == cmd }
    }

    public func command(_ cmd: UInt32) -> LoadCommand? {
        commands.first { $0.cmd == cmd }
    }

    /// The segment whose VM range holds `address`, ignoring empty segments.
    public func segment(containing address: UInt64) -> Segment64? {
        segments.first { $0.vmsize > 0 && $0.contains(address) }
    }

    /// The file offset (in `bytes`) backing `address`, if it is file-backed.
    public func fileOffset(of address: UInt64, width: UInt64 = 1) -> Int? {
        guard let seg = segment(containing: address) else { return nil }
        let delta = address - seg.vmaddr
        guard delta + width <= seg.filesize else { return nil }
        return Int(seg.fileoff + delta)
    }
}

/// A classic relocation_info entry (the kernel's local relocations).
public struct Relocation: Sendable {
    public let address: Int32
    public let symbolNum: UInt32
    public let pcRelative: Bool
    public let length: UInt8
    public let isExtern: Bool
    public let type: UInt8

    public static let size = 8
    public static let arm64Unsigned: UInt8 = 0

    public init(_ b: [UInt8], _ at: Int) throws {
        address = Int32(bitPattern: try b.u32(at))
        let info = try b.u32(at + 4)
        symbolNum = info & 0x00ff_ffff
        pcRelative = (info >> 24) & 1 == 1
        length = UInt8((info >> 25) & 3)
        isExtern = (info >> 27) & 1 == 1
        type = UInt8(info >> 28)
    }
}

/// Symbol table and dynamic symbol table load-command fields.
public struct SymtabCommand: Sendable {
    public let offset: Int
    public let symoff: UInt32
    public let nsyms: UInt32
    public let stroff: UInt32
    public let strsize: UInt32

    public init(_ b: [UInt8], _ lc: LoadCommand) throws {
        offset = lc.offset
        symoff = try b.u32(lc.offset + 8)
        nsyms = try b.u32(lc.offset + 12)
        stroff = try b.u32(lc.offset + 16)
        strsize = try b.u32(lc.offset + 20)
    }
}

public enum Dysymtab {
    // Field offsets inside dysymtab_command.
    public static let extreloff = 64
    public static let nextrel = 68
    public static let locreloff = 72
    public static let nlocrel = 76
    public static let indirectsymoff = 56
    public static let nindirectsyms = 60
    public static let tocoff = 32
    public static let modtaboff = 40
    public static let extrefsymoff = 48
}

public enum ThreadState {
    public static let arm64Flavor: UInt32 = 6
    /// Offset of `pc` in an LC_UNIXTHREAD carrying one ARM_THREAD_STATE64:
    /// cmd, cmdsize, flavor, count, then x0-x28, fp, lr, sp.
    public static let arm64PC = 16 + 32 * 8
}
