// SPDX-License-Identifier: BSD-2-Clause
// Smoke test: Swift 6 language mode calling C23 through the C ABI.

import NDShim

@main
struct Smoke {
    static func main() async {
        // An actor hop keeps Swift 6 strict concurrency honest in the smoke path.
        let answer = await Counter().add(nd_shim_answer(2))
        print("neodarwin smoke: \(answer)")
    }
}

actor Counter {
    private var total: UInt32 = 0
    func add(_ n: UInt32) -> UInt32 {
        total += n
        return total
    }
}
