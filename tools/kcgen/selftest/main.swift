// SPDX-License-Identifier: BSD-2-Clause
//
// kcgen/kcheck self-test on a synthetic kernel: the collection must pass
// kcheck including the round-trip, be deterministic, and every corruption
// below must be caught.

import Foundation
import KCGen
import KCheck
import MachO

var failures = 0
@MainActor
func expect(_ ok: Bool, _ what: String) {
    print("\(ok ? "ok  " : "FAIL") \(what)")
    if !ok { failures += 1 }
}

let kernel = SyntheticKernel().build()
do {
    let (kc, report) = try KernelCollection.build(kernel: kernel)
    let good = KCCheck.check(collection: kc, kernel: kernel)
    for i in good.issues { print("     \(i)") }
    expect(good.ok, "collection passes kcheck with round-trip (\(report.fixups) fixups, kernel at +0x\(String(report.kernelOffset, radix: 16)))")
    expect(report.fixups == 10, "every relocation became a fixup")
    let (again, _) = try KernelCollection.build(kernel: kernel)
    expect(again == kc, "output is deterministic")

    let img = try MachOImage(bytes: kc)
    let entry = try MachOImage(bytes: kc, base: Int(report.kernelOffset))

    func corrupted(_ what: String, roundTrip: Bool = false, _ mutate: (inout [UInt8]) throws -> Void) throws {
        var bad = kc
        try mutate(&bad)
        let r = KCCheck.check(collection: bad, kernel: roundTrip ? kernel : nil)
        expect(!r.ok, "detects \(what)\(r.issues.first.map { ": \($0)" } ?? "")")
    }

    let cf = img.command(LC.dyldChainedFixups)!
    let decoded = try KernelCacheChains.decode(image: kc, blobOffset: Int(try kc.u32(cf.offset + 8)), blobSize: Int(try kc.u32(cf.offset + 12)))
    let first = decoded.fixups[0]
    try corrupted("a fixup target outside the image") { b in
        var p = first.pointer
        p.target = UInt32(kc.count + 0x10000)
        try b.put64(Int(first.location), p.raw)
    }
    try corrupted("an authenticated fixup") { b in
        var p = first.pointer
        p.isAuth = true
        try b.put64(Int(first.location), p.raw)
    }
    try corrupted("a broken chain", roundTrip: true) { b in
        var p = first.pointer
        p.next = 0
        try b.put64(Int(first.location), p.raw)
    }
    try corrupted("a non-flat top-level segment") { b in
        let data = img.segment(named: "__DATA")!
        try b.put64(data.commandOffset + 40, data.fileoff + 0x4000)
    }
    try corrupted("a PLK_TEXT_EXEC region over the kernel") { b in
        let te = img.segment(named: "__TEXT_EXEC")!
        try b.put64(te.commandOffset + 32, 0x4000)
        try b.put64(te.commandOffset + 48, 0x4000)
    }
    try corrupted("a missing fileset flag on the kernel") { b in
        try b.put32(entry.base + 24, entry.flags & ~MH.dylibInCache)
    }
    try corrupted("a changed kernel code byte", roundTrip: true) { b in
        let text = entry.segment(named: "__TEXT_EXEC")!
        let at = Int(text.fileoff) + 0x200
        b[at] ^= 0xff
    }
    try corrupted("an untranslated symbol", roundTrip: true) { b in
        let st = try SymtabCommand(b, entry.command(LC.symtab)!)
        try b.put64(Int(st.symoff) + 8, try b.u64(Int(st.symoff) + 8) - report.kernelOffset)
    }
    try corrupted("a mismatched _PrelinkKCID") { b in
        let uuid = img.command(LC.uuid)!
        b[uuid.offset + 8] ^= 0x01
    }
} catch {
    expect(false, "kcgen builds the synthetic kernel: \(error)")
}

// A codeless kext (xnu's pseudo-kexts, built-in families) is entered in
// __PRELINK_INFO with kOSKextCodelessKextLoadAddr; the kernel stays where it was.
do {
    let plist = """
        <?xml version="1.0" encoding="UTF-8"?>
        <plist version="1.0">
        <dict>
        \t<key>CFBundleIdentifier</key>
        \t<string>com.example.codeless</string>
        \t<key>CFBundleVersion</key>
        \t<string>###KERNEL_VERSION_LONG###</string>
        </dict>
        </plist>
        """
    var options = KernelCollectionOptions()
    options.kernelVersion = "25.0.0"
    let (kc, report) = try KernelCollection.build(
        kernel: kernel, kexts: [KextInput(infoPlist: plist, executable: nil, bundlePath: "/S/L/E/Codeless.kext")], options: options)
    let r = KCCheck.check(collection: kc, kernel: kernel)
    for i in r.issues { print("     \(i)") }
    expect(r.ok, "a collection with a codeless kext passes kcheck with round-trip")
    let text = String(decoding: kc, as: UTF8.self)
    expect(text.contains("<string>com.example.codeless</string>") && text.contains("<string>25.0.0</string>")
        && text.contains("<key>_PrelinkExecutableLoadAddr</key>\n\t\t<integer size=\"64\">0x7fffffffffffffff</integer>")
        && text.contains("<string>/S/L/E/Codeless.kext</string>"),
        "the codeless kext's Info.plist, version and load address are in __PRELINK_INFO")
    expect(report.codeless == ["com.example.codeless"], "the report names the codeless kext")
} catch {
    expect(false, "kcgen builds a collection with a codeless kext: \(error)")
}

var pcrel = SyntheticKernel()
pcrel.relocationOverride = { out, locreloff in out[locreloff + 7] |= 0x01 }  // r_pcrel
do {
    _ = try KernelCollection.build(kernel: pcrel.build())
    expect(false, "kcgen rejects a PC-relative relocation")
} catch {
    expect(true, "kcgen rejects a PC-relative relocation: \(error)")
}

print(failures == 0 ? "kc selftest: ok" : "kc selftest: \(failures) failure(s)")
exit(failures == 0 ? 0 : 1)
