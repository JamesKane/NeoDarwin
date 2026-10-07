// SPDX-License-Identifier: BSD-2-Clause
//
// Boot environments with their own kernels (P2-03, docs/architecture/
// packaging.md §6.1). A system set's kernel collection lives in its boot
// environment; neoboot can't read ZFS yet (P3-06), so `ndpkg system` keeps a
// copy of each BE's collection and static trust cache on the ESP:
//
//   \NeoDarwin\be\<BE>\kernelcache     \NeoDarwin\be\<BE>\trustcache
//   \NeoDarwin\boot.cfg                ... rd=zfs:POOL be=<default BE>
//   \NeoDarwin\bootonce                <BE>: boot it once (an upgrade's try)
//
// neoboot chooses the BE: \NeoDarwin\bootonce if present, which it deletes
// before loading anything, so the try is one-shot whatever happens after
// (a kernel that panics or hangs, a root that never mounts, a userland that
// never confirms the boot); else boot.cfg's be=. It loads that BE's
// collection and trust cache and pins the root to it, rd=zfs:POOL/ROOT/<BE>
// (zfs.kext mounts the dataset rd= names), so the kernel and the root always
// come from the same BE. Without be= and bootonce nothing changes: the
// collection is \NeoDarwin\kernelcache and zfs.kext picks the BE (P3-03).

import UEFI

let bootOncePath: StaticString = "\\NeoDarwin\\bootonce"
let beOption: StaticString = "be="
let zfsRootOption: StaticString = "rd=zfs:"
let beDirectory: StaticString = "\\NeoDarwin\\be\\"
let beNameLimit = 32

/// A short ASCII string built at run time (a BE name, a path): 63 bytes.
struct ShortString {
    var raw: (UInt64, UInt64, UInt64, UInt64, UInt64, UInt64, UInt64, UInt64) = (0, 0, 0, 0, 0, 0, 0, 0)
    var count = 0

    mutating func append(_ bytes: UnsafeRawBufferPointer) -> Bool {
        guard count + bytes.count < 64 else { return false }
        let start = count
        withUnsafeMutableBytes(of: &raw) { r in
            for i in 0..<bytes.count { r[start + i] = bytes[i] }
        }
        count += bytes.count
        return true
    }

    mutating func append(_ s: StaticString) -> Bool {
        append(UnsafeRawBufferPointer(start: s.utf8Start, count: s.utf8CodeUnitCount))
    }

    func withBytes<R>(_ body: (UnsafeRawBufferPointer) -> R) -> R {
        var copy = raw
        let n = count
        return withUnsafeBytes(of: &copy) { body(UnsafeRawBufferPointer(rebasing: $0[0..<n])) }
    }
}

/// A BE name: 1 to 32 of [A-Za-z0-9._-], as `ndpkg system` makes them.
func validBEName(_ b: UnsafeRawBufferPointer) -> Bool {
    guard b.count >= 1, b.count <= beNameLimit, b[0] != 46 else { return false }
    for c in b {
        let ok = (c >= 48 && c <= 57) || (c >= 65 && c <= 90) || (c >= 97 && c <= 122) || c == 45 || c == 46 || c == 95
        if !ok { return false }
    }
    return true
}

func putShort(_ s: ShortString) { s.withBytes { put(bytes: $0) } }

/// \NeoDarwin\bootonce's BE, the file deleted first; nil if there is none,
/// or it can't be deleted (a try that can't be used up would repeat).
func takeBootOnce(_ root: UnsafeMutablePointer<EFI_FILE_PROTOCOL>) -> ShortString? {
    let path = UnsafeRawBufferPointer(start: bootOncePath.utf8Start, count: bootOncePath.utf8CodeUnitCount)
    guard let f = EFIFile(root: root, path: path, write: true) else { return nil }
    var text: (UInt64, UInt64, UInt64, UInt64, UInt64, UInt64) = (0, 0, 0, 0, 0, 0)
    let n = Int(min(f.size, 47))
    let read = withUnsafeMutableBytes(of: &text) { f.read(at: 0, count: UInt64(n), into: $0.baseAddress!) }
    guard f.delete() else {
        put("neoboot: \\NeoDarwin\\bootonce: cannot delete it; ignored (a try must be one-shot)\n")
        return nil
    }
    var name = ShortString()
    let ok = withUnsafeBytes(of: &text) { raw -> Bool in
        var end = n
        while end > 0 && (raw[end - 1] == 10 || raw[end - 1] == 13 || raw[end - 1] == 32) { end -= 1 }
        let b = UnsafeRawBufferPointer(rebasing: raw[0..<end])
        return read && validBEName(b) && name.append(b)
    }
    guard ok else {
        put("neoboot: \\NeoDarwin\\bootonce does not name a boot environment; deleted and ignored\n")
        return nil
    }
    return name
}

/// Removes bytes [start, end) and the space before them from the line.
func removeArgument(_ line: UnsafeMutableRawPointer, _ length: inout Int, _ start: Int, _ end: Int) {
    let from = start > 0 ? start - 1 : start
    let to = (start == 0 && end < length) ? end + 1 : end
    var i = 0
    while to + i < length {
        line.storeBytes(of: line.load(fromByteOffset: to + i, as: UInt8.self), toByteOffset: from + i, as: UInt8.self)
        i += 1
    }
    length -= to - from
}

func appendText(_ line: UnsafeMutableRawPointer, _ length: inout Int, _ b: UnsafeRawBufferPointer) -> Bool {
    guard length + b.count < BootArgs.commandLineLength - 1 else { return false }
    for i in 0..<b.count { line.storeBytes(of: b[i], toByteOffset: length + i, as: UInt8.self) }
    length += b.count
    return true
}

/// The BE to boot, if the ESP has per-BE kernels: \NeoDarwin\bootonce
/// (consumed), else boot.cfg's be=. The command line loses be= and its
/// rd=zfs:POOL[/…] becomes rd=zfs:POOL/ROOT/<BE>.
func chooseBootEnvironment(_ root: UnsafeMutablePointer<EFI_FILE_PROTOCOL>, _ line: UnsafeMutableRawPointer,
                           _ length: inout Int) -> ShortString? {
    var chosen = takeBootOnce(root)
    let once = chosen != nil
    if let v = argumentValue(line, length, beOption) {
        let start = v.baseAddress! - UnsafeRawPointer(line) - beOption.utf8CodeUnitCount
        if chosen == nil {
            var name = ShortString()
            if validBEName(v) && name.append(v) { chosen = name } else { put("neoboot: boot.cfg: be= is not a boot environment name; ignored\n") }
        }
        removeArgument(line, &length, start, start + beOption.utf8CodeUnitCount + v.count)
    }
    guard let be = chosen else { return nil }
    put("neoboot: boot environment ")
    putShort(be)
    put(once ? ": tried once (\\NeoDarwin\\bootonce, now deleted)\n" : " (boot.cfg be=)\n")
    guard let v = argumentValue(line, length, zfsRootOption) else {
        put("neoboot: boot.cfg has no rd=zfs:POOL; the root is not pinned to the boot environment\n")
        return be
    }
    var pool = ShortString()
    var poolLength = 0
    while poolLength < v.count && v[poolLength] != 47 { poolLength += 1 }
    guard pool.append(UnsafeRawBufferPointer(rebasing: v[0..<poolLength])) else { return be }
    let start = v.baseAddress! - UnsafeRawPointer(line) - zfsRootOption.utf8CodeUnitCount
    removeArgument(line, &length, start, start + zfsRootOption.utf8CodeUnitCount + v.count)
    var arg = ShortString()
    var ok = arg.append(" ") && arg.append(zfsRootOption)
    ok = ok && pool.withBytes { arg.append($0) } && arg.append("/ROOT/") && be.withBytes { arg.append($0) }
    if !(ok && arg.withBytes({ appendText(line, &length, $0) })) {
        put("neoboot: the command line is too long for the boot environment's rd=zfs:\n")
    }
    return be
}

/// \NeoDarwin\be\<BE>\<leaf> with a BE, else `plain`.
func openBootFile(_ root: UnsafeMutablePointer<EFI_FILE_PROTOCOL>, _ be: ShortString?, _ plain: StaticString,
                  _ leaf: StaticString) -> EFIFile? {
    guard let be else { return EFIFile(root: root, path: plain) }
    var path = ShortString()
    guard path.append(beDirectory), be.withBytes({ path.append($0) }), path.append("\\"), path.append(leaf) else { return nil }
    return path.withBytes { EFIFile(root: root, path: $0) }
}
