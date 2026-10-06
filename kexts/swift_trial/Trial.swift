// SPDX-License-Identifier: BSD-2-Clause
//
// The kext_swift trial (P0-10, docs/architecture/language-policy.md §3):
// kext logic in Embedded Swift (language policy T3) behind C entry points,
// called from the C++ IOService shell in glue.cpp. No Swift runtime, no
// metadata, no heap: built with -no-allocations, and the kext's undefined
// symbols are only kernel exports.
//
// The logic is the shape the policy reserves for Swift in kexts: a parser
// over a borrowed table, a state machine and a checksum. ndswift_trial_kpi
// also calls kernel KPIs directly, imported from Kernel.framework's C
// headers (KernelKPI/) by the clang importer with -mkernel.

import KernelKPI

/// A record-table walk's outcome. Typed throws: no existential is boxed.
enum TableError: Error {
    case truncated(at: Int)
    case badChecksum(expected: UInt32, actual: UInt32)
}

/// What the walk found; a plain struct with fixed-width fields.
struct Summary {
    var records: UInt32 = 0
    var payloadBytes: UInt32 = 0
    var kinds = InlineArray<4, UInt16>(repeating: 0)
}

/// Fletcher-32 over 16-bit little-endian words (an odd tail byte is padded).
func fletcher32(_ bytes: Span<UInt8>) -> UInt32 {
    var a: UInt32 = 0xffff, b: UInt32 = 0xffff
    var i = 0
    while i < bytes.count {
        let lo = UInt32(bytes[i])
        let hi = i + 1 < bytes.count ? UInt32(bytes[i + 1]) : 0
        a = (a &+ (lo | hi << 8)) % 65535
        b = (b &+ a) % 65535
        i += 2
    }
    return b << 16 | a
}

/// Walks a table of records, [kind: u8][length: u8][payload], ended by a
/// record of kind 0 whose 4-byte payload is the Fletcher-32 of everything
/// before it. A state machine, one byte at a time.
func walk(_ table: Span<UInt8>) throws(TableError) -> Summary {
    enum State { case kind, length(UInt8), payload(kind: UInt8, left: Int) }
    var state = State.kind
    var summary = Summary()
    var i = 0
    while i < table.count {
        let byte = table[i]
        switch state {
        case .kind:
            state = .length(byte)
        case .length(let kind):
            if kind == 0 {
                guard byte == 4, i + 4 < table.count else { throw .truncated(at: i) }
                var stored: UInt32 = 0
                for k in 0..<4 { stored |= UInt32(table[i + 1 + k]) << (8 * k) }
                let actual = fletcher32(table.extracting(0..<(i - 1)))
                guard stored == actual else { throw .badChecksum(expected: stored, actual: actual) }
                return summary
            }
            summary.records += 1
            summary.payloadBytes += UInt32(byte)
            summary.kinds[Int(kind & 3)] &+= 1
            state = byte == 0 ? .kind : .payload(kind: kind, left: Int(byte))
        case .payload(let kind, let left):
            state = left == 1 ? .kind : .payload(kind: kind, left: left - 1)
        }
        i += 1
    }
    throw .truncated(at: i)
}

/// The C ABI the kext's C++ calls: report(tag, value) for each result,
/// tag a NUL-terminated C string.
/// Returns 0 when the table walks cleanly, or a negative error.
@_cdecl("ndswift_trial_run")
public func trialRun(
    _ base: UnsafePointer<UInt8>, _ count: Int,
    _ report: @convention(c) (UnsafePointer<UInt8>, UInt64) -> Void
) -> Int32 {
    // A String literal converted to a C pointer allocates (it bridges
    // through String's storage); a StaticString is a pointer into __cstring.
    func say(_ tag: StaticString, _ value: UInt64) { report(tag.utf8Start, value) }
    let table = UnsafeBufferPointer(start: base, count: count).span
    say("table bytes", UInt64(count))
    say("fletcher32", UInt64(fletcher32(table)))
    do throws(TableError) {
        let s = try walk(table)
        say("records", UInt64(s.records))
        say("payload bytes", UInt64(s.payloadBytes))
        say("kind 1 records", UInt64(s.kinds[1]))
        return 0
    } catch {
        switch error {
        case .truncated(let at):
            say("truncated at", UInt64(at))
            return -1
        case .badChecksum(let expected, _):
            say("bad checksum, expected", UInt64(expected))
            return -2
        }
    }
}

/// Kernel KPIs called from Swift: an atomic add on the caller's counter,
/// a 1 ms IOSleep and the system clock. Returns the counter's new value,
/// or -1 if the clock reads zero.
@_cdecl("ndswift_trial_kpi")
public func trialKPI(_ counter: UnsafeMutablePointer<Int32>) -> Int32 {
    let old = OSAddAtomic(41, counter)
    IOSleep(1)
    var secs: clock_sec_t = 0
    var usecs: clock_usec_t = 0
    clock_get_system_microtime(&secs, &usecs)
    guard secs != 0 || usecs != 0 else { return -1 }
    return old &+ 41
}
