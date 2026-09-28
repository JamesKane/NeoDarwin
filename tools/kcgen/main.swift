// SPDX-License-Identifier: BSD-2-Clause
//
// kcgen: link a kernel (and, from P5, kexts) into an MH_FILESET boot kernel
// collection. See docs/architecture/build-system.md §5.
//
// Usage:
//     kcgen --kernel KERNEL --output KERNELCACHE

import Foundation
import KCGen
import MachO

func fail(_ message: String, code: Int32 = 1) -> Never {
    FileHandle.standardError.write(Data("kcgen: \(message)\n".utf8))
    exit(code)
}

var kernelPath: String?
var outputPath: String?
var args = Array(CommandLine.arguments.dropFirst())
while !args.isEmpty {
    let flag = args.removeFirst()
    guard !args.isEmpty else { fail("usage: kcgen --kernel KERNEL --output KERNELCACHE", code: 2) }
    switch flag {
    case "--kernel": kernelPath = args.removeFirst()
    case "--output": outputPath = args.removeFirst()
    default: fail("unknown option \(flag)", code: 2)
    }
}
guard let kernelPath, let outputPath else { fail("usage: kcgen --kernel KERNEL --output KERNELCACHE", code: 2) }

guard let data = FileManager.default.contents(atPath: kernelPath) else { fail("cannot read \(kernelPath)") }
do {
    let (kc, report) = try KernelCollection.build(kernel: [UInt8](data))
    guard FileManager.default.createFile(atPath: outputPath, contents: Data(kc)) else { fail("cannot write \(outputPath)") }
    let hex = { (v: UInt64) in "0x" + String(v, radix: 16) }
    print("kcgen: \(outputPath): \(report.size) bytes at \(hex(report.linkAddress)), kernel at +\(hex(report.kernelOffset)), \(report.fixups) fixups")
    for s in report.segments {
        print("  \(s.name.padding(toLength: 16, withPad: " ", startingAt: 0)) +\(hex(s.offset).padding(toLength: 10, withPad: " ", startingAt: 0)) \(hex(s.size))")
    }
} catch {
    fail("\(kernelPath): \(error)")
}
