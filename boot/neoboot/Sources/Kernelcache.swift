// SPDX-License-Identifier: BSD-2-Clause
//
// The boot kernel collection built by kcgen (arm64-sbsa-bringup.md §2.1.1).
// It is flat, so loading it is one copy; neoboot only needs its link address
// (the top-level __TEXT vmaddr, = VM_KERNEL_LINK_ADDRESS), its size, and the
// kernel's entry point. The kernel applies the chained fixups itself.

struct Kernelcache {
    static let magic64: UInt32 = 0xfeed_facf
    static let filesetType: UInt32 = 0xc
    static let segment64: UInt32 = 0x19
    static let unixThread: UInt32 = 0x5
    static let filesetEntry: UInt32 = 0x8000_0035
    static let threadPC = 16 + 32 * 8

    let linkAddress: UInt64
    let vmSize: UInt64
    /// File offset of the kernel's own mach header (its LC_FILESET_ENTRY).
    let kernelOffset: UInt64

    /// Parse from the first bytes of the file (`header` holds `count` bytes),
    /// or from the whole image once loaded.
    init?(header p: UnsafeRawPointer, count: Int) {
        guard count >= 32, p.loadUnaligned(as: UInt32.self) == Self.magic64,
              p.loadUnaligned(fromByteOffset: 12, as: UInt32.self) == Self.filesetType else { return nil }
        let ncmds = Int(p.loadUnaligned(fromByteOffset: 16, as: UInt32.self))
        let sizeofcmds = Int(p.loadUnaligned(fromByteOffset: 20, as: UInt32.self))
        guard 32 + sizeofcmds <= count else { return nil }
        var link: UInt64? = nil
        var end: UInt64 = 0
        var kernelOffset: UInt64? = nil
        var at = 32
        for _ in 0..<ncmds {
            let cmd = p.loadUnaligned(fromByteOffset: at, as: UInt32.self)
            let size = Int(p.loadUnaligned(fromByteOffset: at + 4, as: UInt32.self))
            guard size >= 8, at + size <= 32 + sizeofcmds else { return nil }
            if cmd == Self.segment64 {
                let vmaddr = p.loadUnaligned(fromByteOffset: at + 24, as: UInt64.self)
                let vmsize = p.loadUnaligned(fromByteOffset: at + 32, as: UInt64.self)
                if link == nil { link = vmaddr }  // __TEXT comes first
                if let link, vmsize > 0 { end = max(end, vmaddr - link + vmsize) }
            } else if cmd == Self.filesetEntry {
                let idOffset = Int(p.loadUnaligned(fromByteOffset: at + 24, as: UInt32.self))
                if Self.equals(p + at + idOffset, "com.apple.kernel") {
                    kernelOffset = p.loadUnaligned(fromByteOffset: at + 16, as: UInt64.self)
                }
            }
            at += size
        }
        guard let link, let kernelOffset, end > 0 else { return nil }
        linkAddress = link
        vmSize = end
        self.kernelOffset = kernelOffset
    }

    /// The kernel's LC_UNIXTHREAD pc, read once the image is in memory.
    static func entry(image p: UnsafeRawPointer, kernelOffset: UInt64) -> UInt64? {
        let mh = p + Int(kernelOffset)
        guard mh.loadUnaligned(as: UInt32.self) == magic64 else { return nil }
        let ncmds = Int(mh.loadUnaligned(fromByteOffset: 16, as: UInt32.self))
        var at = 32
        for _ in 0..<ncmds {
            let cmd = mh.loadUnaligned(fromByteOffset: at, as: UInt32.self)
            let size = Int(mh.loadUnaligned(fromByteOffset: at + 4, as: UInt32.self))
            if cmd == unixThread { return mh.loadUnaligned(fromByteOffset: at + threadPC, as: UInt64.self) }
            guard size >= 8 else { return nil }
            at += size
        }
        return nil
    }

    static func equals(_ p: UnsafeRawPointer, _ s: StaticString) -> Bool {
        var same = true
        s.withUTF8Buffer { bytes in
            for i in 0..<bytes.count where p.load(fromByteOffset: i, as: UInt8.self) != bytes[i] { same = false }
            if p.load(fromByteOffset: bytes.count, as: UInt8.self) != 0 { same = false }
        }
        return same
    }
}
