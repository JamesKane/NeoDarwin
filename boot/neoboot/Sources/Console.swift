// SPDX-License-Identifier: BSD-2-Clause
//
// Loader output. Before ExitBootServices it goes through the firmware console
// (which on QEMU and most boards is also the serial port); after it, straight
// to the PL011, the only device the loader still owns.

import UEFI
import _Volatile

enum Console {
    nonisolated(unsafe) static var firmware: UnsafeMutablePointer<EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL>? = nil
    nonisolated(unsafe) static var pl011Base: UInt = Platform.uartBase

    // Firmware output is batched through a small stack-free buffer: eight
    // UTF-16 code units and a terminator.
    nonisolated(unsafe) static var buffer: (UInt16, UInt16, UInt16, UInt16, UInt16, UInt16, UInt16, UInt16, UInt16) =
        (0, 0, 0, 0, 0, 0, 0, 0, 0)
    nonisolated(unsafe) static var used = 0

    static func byte(_ b: UInt8) {
        guard let con = firmware else {
            let dr = VolatileMappedRegister<UInt32>(unsafeBitPattern: pl011Base + 0x00)
            let fr = VolatileMappedRegister<UInt32>(unsafeBitPattern: pl011Base + 0x18)
            while fr.load() & (1 << 5) != 0 {}  // UARTFR.TXFF
            dr.store(UInt32(b))
            return
        }
        withUnsafeMutableBytes(of: &buffer) { $0.storeBytes(of: UInt16(b), toByteOffset: used * 2, as: UInt16.self) }
        used += 1
        if used == 8 || b == 10 { flush(con) }
    }

    static func flush(_ con: UnsafeMutablePointer<EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL>) {
        guard used > 0 else { return }
        withUnsafeMutableBytes(of: &buffer) { raw in
            raw.storeBytes(of: 0, toByteOffset: used * 2, as: UInt16.self)
            _ = con.pointee.OutputString(con, raw.baseAddress!.assumingMemoryBound(to: UInt16.self))
        }
        used = 0
    }

    /// Switch to the PL011 once the firmware is gone.
    static func detach() {
        if let con = firmware { flush(con) }
        firmware = nil
    }
}

func put(_ s: StaticString) {
    s.withUTF8Buffer { for b in $0 { if b == 10 { Console.byte(13) }; Console.byte(b) } }
}

func put(bytes: UnsafeRawBufferPointer) {
    for b in bytes { if b == 10 { Console.byte(13) }; Console.byte(b) }
}

func putHex(_ v: UInt64) {
    put("0x")
    var started = false
    for i in stride(from: 60, through: 0, by: -4) {
        let d = UInt8((v >> UInt64(i)) & 0xf)
        if d != 0 || started || i == 0 {
            started = true
            Console.byte(d < 10 ? 48 + d : 87 + d)
        }
    }
}

func putDec(_ v: UInt64) {
    var divisor: UInt64 = 1
    while v / divisor >= 10 { divisor *= 10 }
    var rest = v
    while divisor > 0 {
        Console.byte(UInt8(48 + rest / divisor))
        rest %= divisor
        divisor /= 10
    }
}

/// "label value\n" in hex, the loader's usual log line.
func log(_ label: StaticString, _ value: UInt64) {
    put(label)
    putHex(value)
    put("\n")
}
