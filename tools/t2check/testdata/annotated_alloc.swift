// SPDX-License-Identifier: BSD-2-Clause
// T2 negative fixture: an annotated entry point builds an Array.
@_noLocks
public func total(_ n: Int) -> Int {
    let a = [Int](repeating: 1, count: n)
    var s = 0
    for x in a { s &+= x }
    return s
}
