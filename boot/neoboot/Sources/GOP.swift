// SPDX-License-Identifier: BSD-2-Clause
//
// The firmware's framebuffer, for the kernel's video console. neoboot finds
// a Graphics Output Protocol with a linear framebuffer, keeps the mode the
// firmware set (a mode switch can blank a monitor on real boards), and
// passes it in boot_args.Video; the kernel draws text on it
// (osfmk/console/video_console.c, patch 0021). Design:
// docs/kernel/arm64-sbsa-bringup.md §2.1.7.

import UEFI

/// `gop=off` in boot.cfg: pass no framebuffer; the console stays serial only.
let gopOffOption: StaticString = "gop=off"

struct Framebuffer {
    var base: UInt64
    var width: UInt64
    var height: UInt64
    var rowBytes: UInt64
    /// What the kernel may treat as the framebuffer: all rows, and the
    /// firmware's FrameBufferSize if that is larger.
    var size: UInt64
    /// PixelBlueGreenRedReserved8BitPerColor: the xRGB little-endian word
    /// video_console.c writes (vc_colors, depth 32). RGBx swaps red and blue
    /// in coloured text; white on black looks the same.
    var bgr: Bool
    var mode: UInt32
    var modes: UInt32
}

extension Firmware {
    /// The framebuffer of the GOP that is part of the console, else of the
    /// first GOP with a linear framebuffer; nil without one. Reports its
    /// choice on the console.
    func framebuffer(_ system: UnsafeMutablePointer<EFI_SYSTEM_TABLE>) -> Framebuffer? {
        var gopGUID = EFI_GUID(Data1: 0x9042_a9de, Data2: 0x23dc, Data3: 0x4a38,
                               Data4: (0x96, 0xfb, 0x7a, 0xde, 0xd0, 0x80, 0x51, 0x6a))
        var consoleOutGUID = EFI_GUID(Data1: 0xd3b3_6f2c, Data2: 0xd551, Data3: 0x11d4,
                                      Data4: (0x9a, 0x46, 0x00, 0x90, 0x27, 0x3f, 0xc1, 0x4d))
        var count: UInt64 = 0
        var handles: UnsafeMutablePointer<EFI_HANDLE?>? = nil
        var best: Framebuffer? = nil
        var bestIsConsole = false
        var unusable = 0
        if boot.pointee.LocateHandleBuffer(UInt32(ByProtocol), &gopGUID, nil, &count, &handles) == efiSuccess, let handles {
            for i in 0..<Int(count) {
                var gop: UnsafeMutableRawPointer? = nil
                guard boot.pointee.HandleProtocol(handles[i], &gopGUID, &gop) == efiSuccess, let gop else { continue }
                guard let fb = Self.linear(gop.assumingMemoryBound(to: EFI_GRAPHICS_OUTPUT_PROTOCOL.self)) else {
                    unusable += 1
                    continue
                }
                var console: UnsafeMutableRawPointer? = nil
                let isConsole = boot.pointee.HandleProtocol(handles[i], &consoleOutGUID, &console) == efiSuccess
                if best == nil || (isConsole && !bestIsConsole) {
                    best = fb
                    bestIsConsole = isConsole
                }
            }
            _ = boot.pointee.FreePool(handles)
        }
        guard let fb = best else {
            put(count == 0 ? "neoboot: GOP: none; the console is serial only\n"
                           : "neoboot: GOP: no linear framebuffer (BltOnly or PixelBitMask); the console is serial only\n")
            return nil
        }
        put("neoboot: GOP: ")
        putDec(fb.width)
        put("x")
        putDec(fb.height)
        put(fb.bgr ? " BGRx" : " RGBx")
        put(", mode ")
        putDec(UInt64(fb.mode))
        put(" of ")
        putDec(UInt64(fb.modes))
        put(", ")
        putDec(fb.rowBytes)
        put(" bytes per row, framebuffer ")
        putHex(fb.base)
        put(" (")
        putHex(fb.size)
        put(" bytes)")
        if unusable > 0 { put("; skipped a GOP with no linear framebuffer") }
        put("\n")
        return fb
    }

    /// The current mode's framebuffer if it is linear 32-bit RGB.
    static func linear(_ gop: UnsafeMutablePointer<EFI_GRAPHICS_OUTPUT_PROTOCOL>) -> Framebuffer? {
        guard let mode = gop.pointee.Mode, let info = mode.pointee.Info,
              mode.pointee.SizeOfInfo >= UInt64(MemoryLayout<EFI_GRAPHICS_OUTPUT_MODE_INFORMATION>.size) else { return nil }
        let i = info.pointee
        let format = Int(i.PixelFormat)
        guard format == PixelRedGreenBlueReserved8BitPerColor || format == PixelBlueGreenRedReserved8BitPerColor else { return nil }
        let width = UInt64(i.HorizontalResolution), height = UInt64(i.VerticalResolution)
        let rowBytes = UInt64(i.PixelsPerScanLine) * 4
        let base = mode.pointee.FrameBufferBase
        guard base != 0, base & 3 == 0, width >= 8, height >= 16, UInt64(i.PixelsPerScanLine) >= width else { return nil }
        return Framebuffer(base: base, width: width, height: height, rowBytes: rowBytes,
                           size: max(rowBytes * height, mode.pointee.FrameBufferSize),
                           bgr: format == PixelBlueGreenRedReserved8BitPerColor,
                           mode: mode.pointee.Mode, modes: mode.pointee.MaxMode)
    }
}

/// The DRAM window without the framebuffer, rounded out to kernel pages:
/// the kernel owns every page of its window, and maps the framebuffer as
/// memory it does not own. When the framebuffer splits the window the
/// larger side is kept, as for any other hole (arm64-sbsa-bringup.md §2.1).
func excluding(_ window: (start: UInt64, end: UInt64), _ fb: Framebuffer, page: UInt64) -> (start: UInt64, end: UInt64) {
    let lo = fb.base - fb.base % page
    let hi = roundUp(fb.base + fb.size, page)
    guard lo < window.end, hi > window.start else { return window }
    let below = lo > window.start ? lo - window.start : 0
    let above = window.end > hi ? window.end - hi : 0
    return below >= above ? (window.start, max(lo, window.start)) : (hi, window.end)
}
