// SPDX-License-Identifier: BSD-2-Clause
//
// The UEFI boot services neoboot uses: page allocation, the memory map,
// file reads from the volume it was loaded from, and ExitBootServices.

import UEFI

let efiSuccess: EFI_STATUS = 0
let efiBufferTooSmall: EFI_STATUS = 0x8000_0000_0000_0005
let uefiPage: UInt64 = 0x1000

struct Firmware {
    let image: EFI_HANDLE?
    let boot: UnsafeMutablePointer<EFI_BOOT_SERVICES>

    func allocate(pages: UInt64, at address: UInt64? = nil) -> UInt64? {
        var memory = address ?? 0
        let type: UInt32 = address == nil ? UInt32(AllocateAnyPages) : UInt32(AllocateAddress)
        let status = boot.pointee.AllocatePages(type, UInt32(EfiLoaderData), pages, &memory)
        return status == efiSuccess ? memory : nil
    }

    func free(_ address: UInt64, pages: UInt64) {
        _ = boot.pointee.FreePages(address, pages)
    }

    func disableWatchdog() {
        _ = boot.pointee.SetWatchdogTimer(0, 0, 0, nil)
    }

    /// The root directory of the volume neoboot was loaded from (the ESP).
    func openBootVolume() -> UnsafeMutablePointer<EFI_FILE_PROTOCOL>? {
        var loadedImageGUID = EFI_GUID(Data1: 0x5b1b_31a1, Data2: 0x9562, Data3: 0x11d2,
                                       Data4: (0x8e, 0x3f, 0x00, 0xa0, 0xc9, 0x69, 0x72, 0x3b))
        var fileSystemGUID = EFI_GUID(Data1: 0x964e_5b22, Data2: 0x6459, Data3: 0x11d2,
                                      Data4: (0x8e, 0x39, 0x00, 0xa0, 0xc9, 0x69, 0x72, 0x3b))
        var loaded: UnsafeMutableRawPointer? = nil
        guard boot.pointee.HandleProtocol(image, &loadedImageGUID, &loaded) == efiSuccess, let loaded else { return nil }
        let device = loaded.assumingMemoryBound(to: EFI_LOADED_IMAGE_PROTOCOL.self).pointee.DeviceHandle
        var fs: UnsafeMutableRawPointer? = nil
        guard boot.pointee.HandleProtocol(device, &fileSystemGUID, &fs) == efiSuccess, let fs else { return nil }
        let sfs = fs.assumingMemoryBound(to: EFI_SIMPLE_FILE_SYSTEM_PROTOCOL.self)
        var root: UnsafeMutablePointer<EFI_FILE_PROTOCOL>? = nil
        guard sfs.pointee.OpenVolume(sfs, &root) == efiSuccess else { return nil }
        return root
    }

    /// Exit boot services, fetching a fresh memory map key each attempt: the
    /// key goes stale whenever the firmware changes the map.
    func exitBootServices(_ map: inout MemoryMap) -> Bool {
        for _ in 0..<4 {
            guard map.refresh(self) else { return false }
            if boot.pointee.ExitBootServices(image, map.key) == efiSuccess { return true }
        }
        return false
    }
}

/// An open file on the boot volume.
struct EFIFile {
    let handle: UnsafeMutablePointer<EFI_FILE_PROTOCOL>

    /// `path` is ASCII with backslashes, e.g. "\\NeoDarwin\\kernelcache".
    init?(root: UnsafeMutablePointer<EFI_FILE_PROTOCOL>, path: StaticString) {
        // UTF-16 copy of the path on the stack: 64 code units.
        var name: (UInt64, UInt64, UInt64, UInt64, UInt64, UInt64, UInt64, UInt64,
                   UInt64, UInt64, UInt64, UInt64, UInt64, UInt64, UInt64, UInt64) =
            (0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0)
        guard path.utf8CodeUnitCount < 63 else { return nil }
        var opened: UnsafeMutablePointer<EFI_FILE_PROTOCOL>? = nil
        let status = withUnsafeMutableBytes(of: &name) { raw in
            path.withUTF8Buffer { for i in 0..<$0.count { raw.storeBytes(of: UInt16($0[i]), toByteOffset: i * 2, as: UInt16.self) } }
            return root.pointee.Open(root, &opened, raw.baseAddress!.assumingMemoryBound(to: UInt16.self), UInt64(EFI_FILE_MODE_READ), 0)
        }
        guard status == efiSuccess, let opened else { return nil }
        handle = opened
    }

    var size: UInt64 {
        var position: UInt64 = 0
        _ = handle.pointee.SetPosition(handle, UInt64.max)  // end of file
        _ = handle.pointee.GetPosition(handle, &position)
        _ = handle.pointee.SetPosition(handle, 0)
        return position
    }

    /// Read `count` bytes from `offset` into `buffer`; true if all arrived.
    func read(at offset: UInt64, count: UInt64, into buffer: UnsafeMutableRawPointer) -> Bool {
        guard handle.pointee.SetPosition(handle, offset) == efiSuccess else { return false }
        var remaining = count
        var cursor = buffer
        while remaining > 0 {
            var chunk = min(remaining, 0x10_0000)  // some firmware caps single reads
            guard handle.pointee.Read(handle, &chunk, cursor) == efiSuccess, chunk > 0 else { return false }
            remaining -= chunk
            cursor += Int(chunk)
        }
        return true
    }

    func close() { _ = handle.pointee.Close(handle) }
}
