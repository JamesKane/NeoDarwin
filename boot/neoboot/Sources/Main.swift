// SPDX-License-Identifier: BSD-2-Clause
//
// neoboot, P0-09 stage: prove the Embedded Swift UEFI toolchain end to end.
// Prints through the firmware console and directly to the PL011, then powers
// off. The loader proper (P1-03) grows from here.

import UEFI
import _Volatile

typealias ConBuffer = (UInt16, UInt16, UInt16, UInt16, UInt16, UInt16, UInt16, UInt16, UInt16)

/// Terminate the first `n` code units in `buf` and hand them to the firmware console.
/// A separate function rather than a nested closure: captured locals would be
/// boxed on the heap, which -no-allocations rejects.
func flush(_ con: UnsafeMutablePointer<EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL>, _ buf: inout ConBuffer, _ n: inout Int) {
	withUnsafeMutableBytes(of: &buf) { raw in
		raw.storeBytes(of: 0, toByteOffset: n * 2, as: UInt16.self)
		_ = con.pointee.OutputString(con, raw.baseAddress!.assumingMemoryBound(to: UInt16.self))
	}
	n = 0
}

/// Print ASCII through the firmware console, eight UTF-16 code units at a time,
/// from a stack buffer (no heap: this module builds with -no-allocations).
func put(_ con: UnsafeMutablePointer<EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL>, _ s: StaticString) {
	var buf: ConBuffer = (0, 0, 0, 0, 0, 0, 0, 0, 0)
	var n = 0
	let bytes = UnsafeBufferPointer(start: s.utf8Start, count: s.utf8CodeUnitCount)
	for b in bytes {
		withUnsafeMutableBytes(of: &buf) { raw in raw.storeBytes(of: UInt16(b), toByteOffset: n * 2, as: UInt16.self) }
		n += 1
		if n == 8 { flush(con, &buf, &n) }
	}
	if n > 0 { flush(con, &buf, &n) }
}

/// Write straight to the QEMU virt PL011 (identity-mapped under UEFI): wait while
/// the transmit FIFO is full (UARTFR.TXFF, bit 5), then write UARTDR. After
/// ExitBootServices this is the only console the loader has.
func pl011(_ s: StaticString) {
	let base: UInt = 0x0900_0000
	let dr = VolatileMappedRegister<UInt32>(unsafeBitPattern: base + 0x00)
	let fr = VolatileMappedRegister<UInt32>(unsafeBitPattern: base + 0x18)
	s.withUTF8Buffer { bytes in
		for b in bytes {
			while fr.load() & (1 << 5) != 0 {}
			dr.store(UInt32(b))
		}
	}
}

@_cdecl("efi_main")
func efiMain(_ image: EFI_HANDLE?, _ system: UnsafeMutablePointer<EFI_SYSTEM_TABLE>) -> EFI_STATUS {
	put(system.pointee.ConOut, "neoboot: Embedded Swift on UEFI (ConOut)\r\n")
	pl011("neoboot: Embedded Swift on UEFI (PL011)\r\n")
	system.pointee.RuntimeServices.pointee.ResetSystem(EfiResetShutdown, 0, 0, nil)
	return 0
}
