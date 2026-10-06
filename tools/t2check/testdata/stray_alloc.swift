// SPDX-License-Identifier: BSD-2-Clause
// T2 negative fixture: the entry point is clean, an unannotated helper allocates.
@_noLocks
public func hot(_ x: Int) -> Int { x &+ 1 }

func cold(_ n: Int) -> Int { [Int](repeating: 1, count: n).count }
