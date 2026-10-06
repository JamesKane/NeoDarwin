// SPDX-License-Identifier: BSD-2-Clause
// T2 positive fixture: hashing over a borrowed span, no allocation or lock.

/// FNV-1a, 64-bit.
@_noLocks
public func fnv1a(_ bytes: Span<UInt8>) -> UInt64 {
    var h: UInt64 = 0xcbf2_9ce4_8422_2325
    for i in bytes.indices {
        h = (h ^ UInt64(bytes[i])) &* 0x100_0000_01b3
    }
    return h
}

@_noLocks
public func sum(_ words: borrowing InlineArray<4, UInt32>) -> UInt32 {
    var s: UInt32 = 0
    for i in words.indices { s &+= words[i] }
    return s
}

func rotate(_ x: UInt64, _ n: UInt64) -> UInt64 { (x << n) | (x >> (64 &- n)) }
