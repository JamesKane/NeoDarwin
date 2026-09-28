// SPDX-License-Identifier: BSD-2-Clause
//
// kcheck: structural verification of an MH_FILESET kernel collection against
// what the arm64 kernel assumes when it boots from one, and optionally a
// byte-level round-trip against the kernel it was built from.
//
// The rules and where the kernel relies on them:
//   flat layout, header at the link address   arm_slide_rebase_and_sign_image() (osfmk/arm/arm_init.c)
//   one chain per page, format 8, no auth     kernel_collection_slide() (osfmk/mach/dyld_kernel_fixups.h)
//   PLK_* / PRELINK_* containment             arm_vm_init() fileset block (osfmk/arm64/arm_vm_init.c)
//   __PRELINK_INFO,__info XML dictionary      KLDBootstrap::readPrelinkedExtensions() (libsa/bootstrap.cpp)
//   every fixup target inside the image       the loader never touches pointers (docs/kernel/arm64-sbsa-bringup.md §2.1)

import MachO

public struct CheckReport: Sendable {
    public var issues: [String] = []
    public var notes: [String] = []
    public var ok: Bool { issues.isEmpty }
}

public enum KCCheck {
    public static let kernelEntryID = "com.apple.kernel"

    public static func check(collection kc: [UInt8], kernel: [UInt8]? = nil) -> CheckReport {
        var r = CheckReport()
        do {
            try run(kc, kernel, &r)
        } catch {
            r.issues.append("\(error)")
        }
        return r
    }

    static func hex(_ v: UInt64) -> String { "0x" + String(v, radix: 16) }

    struct Entry {
        let id: String
        let image: MachOImage
    }

    static func run(_ kc: [UInt8], _ kernel: [UInt8]?, _ r: inout CheckReport) throws {
        let img = try MachOImage(bytes: kc)
        guard img.filetype == MH.fileset else { throw MachOError.malformed("not an MH_FILESET (filetype \(img.filetype))") }
        guard img.cputype == CPU.arm64 else { throw MachOError.unsupported("cputype \(hex(UInt64(img.cputype)))") }
        let top = img.segments
        guard let text = top.first, text.name == "__TEXT", text.fileoff == 0 else {
            throw MachOError.malformed("first top-level segment must be __TEXT at file offset 0")
        }
        let link = text.vmaddr
        func off(_ vm: UInt64) -> UInt64 { vm &- link }

        // Flat layout: file offset == VM offset, everything file-backed.
        var end: UInt64 = 0
        for s in top where s.vmsize > 0 {
            if s.fileoff != off(s.vmaddr) { r.issues.append("\(s.name): file offset \(hex(s.fileoff)) != VM offset \(hex(off(s.vmaddr)))") }
            if s.filesize != s.vmsize { r.issues.append("\(s.name): filesize \(hex(s.filesize)) != vmsize \(hex(s.vmsize))") }
            end = max(end, s.fileoff + s.filesize)
        }
        if end != UInt64(kc.count) { r.issues.append("file is \(hex(UInt64(kc.count))) bytes, segments end at \(hex(end))") }
        let nonEmpty = top.filter { $0.vmsize > 0 }
        for (i, a) in nonEmpty.enumerated() {
            for b in nonEmpty[(i + 1)...] where a.vmaddr < b.vmEnd && b.vmaddr < a.vmEnd {
                r.issues.append("top-level \(a.name) and \(b.name) overlap")
            }
        }
        func topSeg(_ name: String) -> Segment64? {
            let s = img.segment(named: name)
            if s == nil { r.issues.append("collection has no top-level \(name)") }
            return s
        }

        // Fileset entries.
        var entries: [Entry] = []
        for lc in img.commands(LC.filesetEntry) {
            let vmaddr = try kc.u64(lc.offset + 8)
            let fileoff = try kc.u64(lc.offset + 16)
            let id = try kc.cString(lc.offset + Int(try kc.u32(lc.offset + 24)), limit: lc.offset + lc.size)
            if fileoff != off(vmaddr) { r.issues.append("entry \(id): file offset \(hex(fileoff)) != VM offset \(hex(off(vmaddr)))") }
            let e = try MachOImage(bytes: kc, base: Int(fileoff))
            if e.flags & MH.dylibInCache == 0 { r.issues.append("entry \(id): MH_DYLIB_IN_CACHE is not set") }
            guard e.commands.first?.cmd == LC.segment64, let first = e.segments.first, first.name == "__TEXT",
                  first.vmaddr == vmaddr, first.fileoff == fileoff else {
                r.issues.append("entry \(id): first load command must be __TEXT at the entry's address")
                continue
            }
            for s in e.segments where s.vmsize > 0 {
                if s.fileoff != off(s.vmaddr) || s.filesize != s.vmsize {
                    r.issues.append("entry \(id) \(s.name): not flat (fileoff \(hex(s.fileoff)), filesize \(hex(s.filesize)))")
                }
                let holders = nonEmpty.filter { $0.vmaddr <= s.vmaddr && s.vmEnd <= $0.vmEnd }
                if holders.count != 1 { r.issues.append("entry \(id) \(s.name): contained in \(holders.count) top-level segments") }
            }
            entries.append(Entry(id: id, image: e))
        }
        let kernels = entries.filter { $0.id == kernelEntryID }
        guard kernels.count == 1, let k = kernels.first?.image else {
            throw MachOError.malformed("expected one \(kernelEntryID) entry, found \(kernels.count)")
        }

        // What arm_vm_init() derives from the collection for kexts must be
        // well-formed and must not overlap the kernel.
        if let kte = topSeg("__TEXT_EXEC"), let kdc = topSeg("__DATA_CONST"), let kdata = topSeg("__DATA"),
           topSeg("__PRELINK_TEXT") != nil, topSeg("__LINKEDIT") != nil {
            func kseg(_ name: String, empty: Bool) -> Segment64? {
                guard let s = k.segment(named: name) else { r.issues.append("kernel has no \(name)"); return nil }
                if empty && s.vmsize != 0 { r.issues.append("kernel \(name) must be empty") }
                if s.vmaddr == 0 { r.issues.append("kernel \(name) has no address") }
                return s
            }
            for name in ["__PLK_TEXT_EXEC", "__PLK_DATA_CONST", "__PRELINK_TEXT", "__PRELINK_INFO"] { _ = kseg(name, empty: true) }
            let kernelMapped = k.segments.filter { $0.vmsize > 0 }
            func region(_ what: String, _ container: Segment64, _ anchor: Segment64?) {
                guard let anchor else { return }
                guard container.vmaddr <= anchor.vmaddr, anchor.vmEnd <= container.vmEnd else {
                    r.issues.append("top-level \(container.name) does not contain kernel \(anchor.name)")
                    return
                }
                let lo = anchor.vmEnd, hi = container.vmEnd
                for s in kernelMapped where s.vmaddr < hi && lo < s.vmEnd {
                    r.issues.append("\(what) region [\(hex(lo)), \(hex(hi))) overlaps kernel \(s.name)")
                }
                r.notes.append("\(what): \(hex(hi - lo)) bytes")
            }
            region("PLK_TEXT_EXEC", kte, kseg("__LAST", empty: false))
            if let ldc = kseg("__LASTDATA_CONST", empty: false), ldc.vmsize == 0 { r.issues.append("kernel __LASTDATA_CONST is empty") }
            region("PLK_DATA_CONST", kdc, k.segment(named: "__LASTDATA_CONST"))
            region("PRELINK_DATA", kdata, kseg("__PRELINK_DATA", empty: true))
        }
        if let lc = k.command(LC.unixThread), let pc = try? kc.u64(lc.offset + ThreadState.arm64PC) {
            if k.segment(containing: pc).map({ $0.maxprot & VMProt.execute != 0 }) != true {
                r.issues.append("kernel entry point \(hex(pc)) is not in an executable segment")
            }
        } else {
            r.issues.append("kernel has no LC_UNIXTHREAD entry point")
        }

        // __PRELINK_INFO,__info: the XML dictionary, carrying the collection UUID.
        let uuid = img.command(LC.uuid).map { lc in Array(kc[(lc.offset + 8)..<(lc.offset + 24)]) }
        if uuid == nil { r.issues.append("collection has no LC_UUID") }
        if let info = img.segment(named: "__PRELINK_INFO")?.sections.first(where: { $0.name == "__info" }) {
            let start = Int(off(info.addr)), size = Int(info.size)
            try kc.check(start, size)
            let text = String(decoding: kc[start..<start + size].prefix { $0 != 0 }, as: UTF8.self)
            if size == 0 || kc[start + size - 1] != 0 { r.issues.append("__PRELINK_INFO,__info is not NUL-terminated") }
            if !text.contains("<key>_PrelinkInfoDictionary</key>") { r.issues.append("__PRELINK_INFO,__info has no _PrelinkInfoDictionary") }
            if let uuid, !text.contains("<data>\(Base64.encode(uuid))</data>") { r.issues.append("_PrelinkKCID does not match LC_UUID") }
        } else {
            r.issues.append("collection has no __PRELINK_INFO,__info section")
        }

        // Chained fixups.
        guard let cf = img.command(LC.dyldChainedFixups) else { throw MachOError.malformed("collection has no LC_DYLD_CHAINED_FIXUPS") }
        let dataoff = Int(try kc.u32(cf.offset + 8)), datasize = Int(try kc.u32(cf.offset + 12))
        if let le = img.segment(named: "__LINKEDIT"), !(UInt64(dataoff) >= le.fileoff && UInt64(dataoff + datasize) <= le.fileoff + le.filesize) {
            r.issues.append("chained fixups lie outside __LINKEDIT")
        }
        let decoded = try KernelCacheChains.decode(image: kc, blobOffset: dataoff, blobSize: datasize)
        if decoded.regionCount != top.count { r.issues.append("chained starts describe \(decoded.regionCount) segments, collection has \(top.count)") }
        for s in decoded.starts where s.regionIndex < top.count {
            let t = top[s.regionIndex]
            if s.segmentOffset != off(t.vmaddr) { r.issues.append("starts for \(t.name) at \(hex(s.segmentOffset)), segment at \(hex(off(t.vmaddr)))") }
            if Int(s.pageSize) != KernelCacheChains.pageSize { r.issues.append("starts for \(t.name) use page size \(s.pageSize)") }
            let pages = (t.vmsize + UInt64(s.pageSize) - 1) / UInt64(s.pageSize)
            if UInt64(s.pageStarts.count) != pages { r.issues.append("starts for \(t.name) cover \(s.pageStarts.count) pages of \(pages)") }
        }
        let allSegments = entries.flatMap { $0.image.segments } + top
        var seen = Set<UInt64>()
        var bad = 0
        for f in decoded.fixups {
            var problems: [String] = []
            let t = top[f.regionIndex]
            if !(f.location >= off(t.vmaddr) && f.location + 8 <= off(t.vmEnd)) { problems.append("outside \(t.name)") }
            if !seen.insert(f.location).inserted { problems.append("visited twice") }
            let p = f.pointer
            if p.isAuth || p.key != 0 || p.diversity != 0 || p.addrDiv { problems.append("authenticated (no PAC on SBSA)") }
            if p.cacheLevel != 0 { problems.append("cache level \(p.cacheLevel); only the boot collection exists") }
            let target = UInt64(p.target)
            if !allSegments.contains(where: { $0.vmsize > 0 && target >= off($0.vmaddr) && target <= off($0.vmEnd) }) {
                problems.append("target +\(hex(target)) is outside every segment")
            }
            if let holder = entries.lazy.compactMap({ $0.image.segment(containing: link + f.location) }).first,
               holder.maxprot & VMProt.execute != 0 {
                problems.append("location is in executable \(holder.name)")
            }
            if !problems.isEmpty {
                bad += 1
                if bad <= 20 { r.issues.append("fixup at +\(hex(f.location)): " + problems.joined(separator: ", ")) }
            }
        }
        if bad > 20 { r.issues.append("... \(bad - 20) more bad fixups") }
        r.notes.append("\(decoded.fixups.count) fixups in \(decoded.chains) chains; collection \(hex(UInt64(kc.count))) bytes at \(hex(link)); \(entries.count) entr\(entries.count == 1 ? "y" : "ies")")

        if let kernel {
            try roundTrip(kc, link: link, entry: k, fixups: decoded.fixups, source: kernel, &r)
        }
    }

    /// The collection's kernel must be the source kernel translated by one
    /// constant: same bytes except its own load commands and the fixup
    /// slots, one fixup per source relocation with the translated target,
    /// translated symbols and entry point, and zeroed bss.
    static func roundTrip(_ kc: [UInt8], link: UInt64, entry: MachOImage, fixups: [KernelCacheChains.Fixup],
                          source: [UInt8], _ r: inout CheckReport) throws {
        let s = try MachOImage(bytes: source)
        guard let sText = s.segments.first, sText.fileoff == 0 else { throw MachOError.malformed("source kernel has no leading __TEXT") }
        if sText.vmaddr != link {
            r.issues.append("collection header at \(hex(link)), but the kernel is linked at \(hex(sText.vmaddr)) (VM_KERNEL_LINK_ADDRESS)")
        }
        let delta = entry.segments[0].vmaddr &- sText.vmaddr
        let base = entry.base
        func moved(_ vm: UInt64) -> UInt64 { vm - sText.vmaddr + UInt64(base) }

        // Segments.
        if s.segments.count != entry.segments.count { r.issues.append("kernel has \(entry.segments.count) segments, source has \(s.segments.count)") }
        for (a, b) in zip(s.segments, entry.segments) {
            if a.name != b.name || b.vmaddr != a.vmaddr &+ delta || a.vmsize != b.vmsize || a.maxprot != b.maxprot || a.initprot != b.initprot {
                r.issues.append("kernel segment \(b.name) does not match source \(a.name) translated by \(hex(delta))")
            }
        }

        // Fixups against relocations.
        var expected: [UInt64: UInt64] = [:]
        if let dys = s.command(LC.dysymtab) {
            let locreloff = Int(try source.u32(dys.offset + Dysymtab.locreloff))
            let n = Int(try source.u32(dys.offset + Dysymtab.nlocrel))
            for i in 0..<n {
                let rel = try Relocation(source, locreloff + i * Relocation.size)
                let address = sText.vmaddr &+ UInt64(bitPattern: Int64(rel.address))
                guard let at = s.fileOffset(of: address, width: 8) else { r.issues.append("source relocation \(i) is not file-backed"); continue }
                expected[moved(address)] = moved(try source.u64(at))
            }
        }
        var matched = 0
        for f in fixups {
            if let want = expected[f.location], UInt64(f.pointer.target) == want { matched += 1 }
        }
        if matched != expected.count || fixups.count != expected.count {
            r.issues.append("\(matched) of \(expected.count) source relocations round-trip; collection has \(fixups.count) fixups")
        }

        // Bytes: everything but the kernel's load commands, symbol values and fixup slots.
        var skip = [Bool](repeating: false, count: kc.count)
        for f in fixups where f.location + 8 <= UInt64(kc.count) {
            for j in 0..<8 { skip[Int(f.location) + j] = true }
        }
        for j in base..<min(kc.count, base + MH.headerSize + s.sizeOfCommands) { skip[j] = true }
        if let lc = entry.command(LC.symtab) {  // n_value fields; checked below
            let st = try SymtabCommand(kc, lc)
            for i in 0..<Int(st.nsyms) {
                let v = Int(st.symoff) + i * NList.size + 8
                for j in v..<min(kc.count, v + 8) { skip[j] = true }
            }
        }
        var compared = 0, differing = 0
        for seg in s.segments where seg.vmsize > 0 {
            let dst = Int(moved(seg.vmaddr)), filesize = Int(seg.filesize), fileoff = Int(seg.fileoff)
            try kc.check(dst, Int(seg.vmsize))
            for i in 0..<Int(seg.vmsize) where !skip[dst + i] {
                let want: UInt8 = i < filesize ? source[fileoff + i] : 0
                if kc[dst + i] != want {
                    differing += 1
                    if differing <= 5 { r.issues.append("kernel byte at +\(hex(UInt64(dst + i))) (\(seg.name)+\(hex(UInt64(i)))) differs from source") }
                }
                compared += 1
            }
        }
        if differing > 5 { r.issues.append("... \(differing) differing bytes in all") }

        // Symbols and entry point.
        if let a = s.command(LC.symtab), let b = entry.command(LC.symtab) {
            let sa = try SymtabCommand(source, a), sb = try SymtabCommand(kc, b)
            if sa.nsyms != sb.nsyms || sa.strsize != sb.strsize {
                r.issues.append("symbol table sizes differ")
            } else {
                var wrong = 0
                for i in 0..<Int(sa.nsyms) {
                    let ea = Int(sa.symoff) + i * NList.size, eb = Int(sb.symoff) + i * NList.size
                    let type = try source.u8(ea + 4)
                    let want = try source.u64(ea + 8) &+ (type & NList.stab == 0 && type & NList.typeMask == NList.sect ? delta : 0)
                    if try kc.u64(eb + 8) != want || source[ea..<ea + 8] != kc[eb..<eb + 8] { wrong += 1 }
                }
                if source[Int(sa.stroff)..<Int(sa.stroff + sa.strsize)] != kc[Int(sb.stroff)..<Int(sb.stroff + sb.strsize)] { wrong += 1 }
                if wrong > 0 { r.issues.append("\(wrong) symbol(s) not translated by \(hex(delta))") }
            }
        } else {
            r.issues.append("symbol table missing")
        }
        if let a = s.command(LC.unixThread), let b = entry.command(LC.unixThread),
           try kc.u64(b.offset + ThreadState.arm64PC) != (try source.u64(a.offset + ThreadState.arm64PC)) &+ delta {
            r.issues.append("entry point not translated by \(hex(delta))")
        }
        r.notes.append("round-trip: \(matched)/\(expected.count) relocations, \(compared) bytes, kernel moved by \(hex(delta))")
    }
}
