// SPDX-License-Identifier: BSD-2-Clause
//
// struct boot_args for arm64 (pexpert/pexpert/arm64/boot.h), Revision 2
// Version 2, with BOOT_LINE_LENGTH = 1024 (kernel/sdk iBoot/boot_args_abi.h).
// start.s reads it by physical address with the MMU off; the kernel keeps
// using deviceTreeP as a virtual address.

struct BootArgs {
    static let size = 1152
    static let commandLineLength = 1024

    enum Offset {
        static let revision = 0
        static let version = 2
        static let virtBase = 8
        static let physBase = 16
        static let memSize = 24
        static let topOfKernelData = 32
        static let video = 40  // six u64: baseAddr, display, rowBytes, width, height, depth
        static let machineType = 88
        static let deviceTreeP = 96
        static let deviceTreeLength = 104
        static let commandLine = 108
        static let bootFlags = 1136
        static let memSizeActual = 1144
    }

    var virtBase: UInt64 = 0
    var physBase: UInt64 = 0
    var memSize: UInt64 = 0
    var topOfKernelData: UInt64 = 0
    var deviceTree: UInt64 = 0
    var deviceTreeLength: UInt32 = 0
    var memSizeActual: UInt64 = 0

    func write(to p: UnsafeMutableRawPointer, commandLine: UnsafeRawBufferPointer) {
        p.initializeMemory(as: UInt8.self, repeating: 0, count: Self.size)
        p.storeBytes(of: UInt16(2), toByteOffset: Offset.revision, as: UInt16.self)
        p.storeBytes(of: UInt16(2), toByteOffset: Offset.version, as: UInt16.self)
        p.storeBytes(of: virtBase, toByteOffset: Offset.virtBase, as: UInt64.self)
        p.storeBytes(of: physBase, toByteOffset: Offset.physBase, as: UInt64.self)
        p.storeBytes(of: memSize, toByteOffset: Offset.memSize, as: UInt64.self)
        p.storeBytes(of: topOfKernelData, toByteOffset: Offset.topOfKernelData, as: UInt64.self)
        // Video stays zero: v_display = 0 selects the text console (GOP is later).
        p.storeBytes(of: deviceTree, toByteOffset: Offset.deviceTreeP, as: UInt64.self)
        p.storeBytes(of: deviceTreeLength, toByteOffset: Offset.deviceTreeLength, as: UInt32.self)
        let n = min(commandLine.count, Self.commandLineLength - 1)
        if n > 0 { (p + Offset.commandLine).copyMemory(from: commandLine.baseAddress!, byteCount: n) }
        p.storeBytes(of: memSizeActual, toByteOffset: Offset.memSizeActual, as: UInt64.self)
    }
}
