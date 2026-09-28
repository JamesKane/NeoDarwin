// SPDX-License-Identifier: BSD-2-Clause
//
// kcheck: verify an MH_FILESET kernel collection (see Check.swift).
//
// Usage:
//     kcheck KERNELCACHE [--kernel KERNEL]
//
// With --kernel, also checks that the collection's kernel round-trips to
// the source kernel. Exits 1 on any issue.

import Foundation
import KCheck

func fail(_ message: String, code: Int32) -> Never {
    FileHandle.standardError.write(Data("kcheck: \(message)\n".utf8))
    exit(code)
}

let usage = "usage: kcheck KERNELCACHE [--kernel KERNEL]"
var args = Array(CommandLine.arguments.dropFirst())
var collectionPath: String?
var kernelPath: String?
while !args.isEmpty {
    let a = args.removeFirst()
    if a == "--kernel" {
        guard !args.isEmpty else { fail(usage, code: 2) }
        kernelPath = args.removeFirst()
    } else if collectionPath == nil {
        collectionPath = a
    } else {
        fail(usage, code: 2)
    }
}
guard let collectionPath else { fail(usage, code: 2) }
guard let kc = FileManager.default.contents(atPath: collectionPath) else { fail("cannot read \(collectionPath)", code: 1) }
var kernel: [UInt8]?
if let kernelPath {
    guard let k = FileManager.default.contents(atPath: kernelPath) else { fail("cannot read \(kernelPath)", code: 1) }
    kernel = [UInt8](k)
}

let report = KCCheck.check(collection: [UInt8](kc), kernel: kernel)
for n in report.notes { print("kcheck: \(n)") }
for i in report.issues { print("kcheck: FAIL: \(i)") }
print("kcheck: \(collectionPath): \(report.ok ? "ok" : "\(report.issues.count) issue(s)")")
exit(report.ok ? 0 : 1)
