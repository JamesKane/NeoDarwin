// SPDX-License-Identifier: BSD-2-Clause
//
// Writer for Apple's flattened device tree (pexpert/pexpert/device_tree.h),
// which is not FDT: a node is {u32 nProperties; u32 nChildren}, then its
// properties {char name[32]; u32 length; value padded to 4 bytes}, then its
// children. Counts are patched as properties and children are added, so the
// tree is written in one pass into loader-owned memory with no heap.

struct DeviceTreeWriter {
    static let maxDepth = 8
    static let nameLength = 32

    let base: UnsafeMutableRawPointer
    let capacity: Int
    private(set) var offset = 0
    private(set) var failed = false
    private var depth = 0
    // Header offset of each open node, and whether it already has children
    // (properties must come first).
    private var open: (Int, Int, Int, Int, Int, Int, Int, Int) = (0, 0, 0, 0, 0, 0, 0, 0)
    private var hasChildren: UInt8 = 0

    init(base: UnsafeMutableRawPointer, capacity: Int) {
        self.base = base
        self.capacity = capacity
    }

    private func header(_ level: Int) -> Int {
        var o = open
        return withUnsafeBytes(of: &o) { $0.load(fromByteOffset: level * 8, as: Int.self) }
    }

    private mutating func bump(_ at: Int) {
        let v = base.load(fromByteOffset: at, as: UInt32.self)
        base.storeBytes(of: v + 1, toByteOffset: at, as: UInt32.self)
    }

    private mutating func reserve(_ n: Int) -> Bool {
        guard !failed, offset + n <= capacity else { failed = true; return false }
        return true
    }

    mutating func begin() {
        guard depth < Self.maxDepth, reserve(8) else { failed = true; return }
        if depth > 0 {
            bump(header(depth - 1) + 4)
            hasChildren |= 1 << UInt8(depth - 1)
        }
        base.storeBytes(of: UInt64(0), toByteOffset: offset, as: UInt64.self)
        let at = offset
        withUnsafeMutableBytes(of: &open) { $0.storeBytes(of: at, toByteOffset: depth * 8, as: Int.self) }
        hasChildren &= ~(1 << UInt8(depth))
        depth += 1
        offset += 8
    }

    mutating func end() {
        guard depth > 0 else { failed = true; return }
        depth -= 1
    }

    /// A property whose value `fill` writes into `length` bytes.
    mutating func property(_ name: StaticString, length: Int, _ fill: (UnsafeMutableRawPointer) -> Void) {
        let padded = (length + 3) & ~3
        guard depth > 0, hasChildren & (1 << UInt8(depth - 1)) == 0,
              name.utf8CodeUnitCount < Self.nameLength, reserve(Self.nameLength + 4 + padded) else {
            failed = true
            return
        }
        let p = base + offset
        p.initializeMemory(as: UInt8.self, repeating: 0, count: Self.nameLength + 4 + padded)
        name.withUTF8Buffer { p.copyMemory(from: $0.baseAddress!, byteCount: $0.count) }
        p.storeBytes(of: UInt32(length), toByteOffset: Self.nameLength, as: UInt32.self)
        fill(p + Self.nameLength + 4)
        bump(header(depth - 1))
        offset += Self.nameLength + 4 + padded
    }

    mutating func property(_ name: StaticString, string: StaticString) {
        property(name, length: string.utf8CodeUnitCount + 1) { v in
            string.withUTF8Buffer { v.copyMemory(from: $0.baseAddress!, byteCount: $0.count) }
        }
    }

    mutating func property(_ name: StaticString, u32: UInt32) {
        property(name, length: 4) { $0.storeBytes(of: u32, as: UInt32.self) }
    }

    mutating func property(_ name: StaticString, u64: UInt64) {
        property(name, length: 8) { $0.storeBytes(of: u64, toByteOffset: 0, as: UInt64.self) }
    }

    mutating func property(_ name: StaticString, _ a: UInt64, _ b: UInt64) {
        property(name, length: 16) { v in
            v.storeBytes(of: a, toByteOffset: 0, as: UInt64.self)
            v.storeBytes(of: b, toByteOffset: 8, as: UInt64.self)
        }
    }

    mutating func property(_ name: StaticString, _ a: UInt64, _ b: UInt64, _ c: UInt64) {
        property(name, length: 24) { v in
            v.storeBytes(of: a, toByteOffset: 0, as: UInt64.self)
            v.storeBytes(of: b, toByteOffset: 8, as: UInt64.self)
            v.storeBytes(of: c, toByteOffset: 16, as: UInt64.self)
        }
    }

    mutating func property(_ name: StaticString, _ a: UInt64, _ b: UInt64, _ c: UInt64, _ d: UInt64) {
        property(name, length: 32) { v in
            v.storeBytes(of: a, toByteOffset: 0, as: UInt64.self)
            v.storeBytes(of: b, toByteOffset: 8, as: UInt64.self)
            v.storeBytes(of: c, toByteOffset: 16, as: UInt64.self)
            v.storeBytes(of: d, toByteOffset: 24, as: UInt64.self)
        }
    }

    mutating func property(_ name: StaticString, u32s a: UInt32, _ b: UInt32, _ c: UInt32) {
        property(name, length: 12) { v in
            v.storeBytes(of: a, toByteOffset: 0, as: UInt32.self)
            v.storeBytes(of: b, toByteOffset: 4, as: UInt32.self)
            v.storeBytes(of: c, toByteOffset: 8, as: UInt32.self)
        }
    }

    var complete: Bool { !failed && depth == 0 }
}
