// SPDX-License-Identifier: BSD-2-Clause
// T2 negative fixture: a class instance on the hot path (allocation, metadata, refcounting).
final class Counter { var n = 0 }

@_noLocks
public func bump(_ x: Int) -> Int {
    let c = Counter()
    c.n = x
    return c.n
}
