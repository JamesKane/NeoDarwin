// SPDX-License-Identifier: BSD-2-Clause
//
// kcgen: link a kernel and kexts into an MH_FILESET boot kernel collection.
// See docs/architecture/build-system.md §5.
//
// Usage:
//     kcgen --kernel KERNEL --output KERNELCACHE [--kernel-version VERSION]
//           [--kext BUNDLE]... [--codeless PLIST@BUNDLEPATH]...
//
// --kext names a kext bundle directory (Contents/Info.plist and
// Contents/MacOS/<CFBundleExecutable>), entered as
// /System/Library/Extensions/<bundle name>. --codeless enters an Info.plist
// with no executable under the given bundle path (xnu's System.kext
// pseudo-kexts, NeoDarwin's built-in families).

import Foundation
import KCGen
import MachO

func fail(_ message: String, code: Int32 = 1) -> Never {
    FileHandle.standardError.write(Data("kcgen: \(message)\n".utf8))
    exit(code)
}

let usage = "usage: kcgen --kernel KERNEL --output KERNELCACHE [--kernel-version VERSION] [--kext BUNDLE]... [--codeless PLIST@BUNDLEPATH]..."
var kernelPath: String?
var outputPath: String?
var options = KernelCollectionOptions()
var kexts: [KextInput] = []

func read(_ path: String) -> Data {
    guard let d = FileManager.default.contents(atPath: path) else { fail("cannot read \(path)") }
    return d
}

func plistString(_ text: String, _ key: String) -> String? {
    guard let k = text.range(of: "<key>\(key)</key>"),
          let open = text.range(of: "<string>", range: k.upperBound..<text.endIndex),
          let close = text.range(of: "</string>", range: open.upperBound..<text.endIndex) else { return nil }
    return String(text[open.upperBound..<close.lowerBound])
}

var args = Array(CommandLine.arguments.dropFirst())
while !args.isEmpty {
    let flag = args.removeFirst()
    guard !args.isEmpty else { fail(usage, code: 2) }
    let value = args.removeFirst()
    switch flag {
    case "--kernel": kernelPath = value
    case "--output": outputPath = value
    case "--kernel-version": options.kernelVersion = value
    case "--kext":
        let bundle = URL(fileURLWithPath: value)
        guard let plist = String(data: read(bundle.appendingPathComponent("Contents/Info.plist").path), encoding: .utf8) else {
            fail("\(value): Info.plist is not UTF-8")
        }
        guard let exe = plistString(plist, "CFBundleExecutable") else { fail("\(value): Info.plist has no CFBundleExecutable") }
        let rel = "Contents/MacOS/\(exe)"
        kexts.append(KextInput(infoPlist: plist, executable: [UInt8](read(bundle.appendingPathComponent(rel).path)),
                               bundlePath: "/System/Library/Extensions/\(bundle.lastPathComponent)",
                               executableRelativePath: rel))
    case "--codeless":
        guard let at = value.lastIndex(of: "@") else { fail("--codeless takes PLIST@BUNDLEPATH", code: 2) }
        let path = String(value[..<at]), bundlePath = String(value[value.index(after: at)...])
        guard let plist = String(data: read(path), encoding: .utf8) else { fail("\(path) is not UTF-8") }
        kexts.append(KextInput(infoPlist: plist, executable: nil, bundlePath: bundlePath))
    default: fail("unknown option \(flag)", code: 2)
    }
}
guard let kernelPath, let outputPath else { fail(usage, code: 2) }

do {
    let (kc, report) = try KernelCollection.build(kernel: [UInt8](read(kernelPath)), kexts: kexts, options: options)
    guard FileManager.default.createFile(atPath: outputPath, contents: Data(kc)) else { fail("cannot write \(outputPath)") }
    let hex = { (v: UInt64) in "0x" + String(v, radix: 16) }
    print("kcgen: \(outputPath): \(report.size) bytes at \(hex(report.linkAddress)), kernel at +\(hex(report.kernelOffset)), \(report.fixups) fixups")
    for s in report.segments {
        print("  \(s.name.padding(toLength: 16, withPad: " ", startingAt: 0)) +\(hex(s.offset).padding(toLength: 10, withPad: " ", startingAt: 0)) \(hex(s.size))")
    }
    if !report.kexts.isEmpty {
        print("  kernel: \(report.kernelAdjusted) references moved")
        for k in report.kexts {
            print("  kext \(k.identifier) at \(hex(k.textAddress)): \(k.imports) imports, \(k.adjusted) references moved")
        }
    }
    if !report.codeless.isEmpty { print("  codeless: \(report.codeless.joined(separator: " "))") }
} catch {
    fail("\(kernelPath): \(error)")
}
