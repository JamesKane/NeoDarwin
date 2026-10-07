// SPDX-License-Identifier: BSD-2-Clause
//
// Test-only: rewrites one member of a .ndpkg, leaving its signatures as they
// are, so ndsign_test can check that verification notices.
//     tamper IN MEMBER flip|FILE OUT
// "flip" inverts the member's last byte; otherwise FILE replaces it.

import Foundation
import NDPkg

let a = CommandLine.arguments
guard a.count == 5, let input = FileManager.default.contents(atPath: a[1]) else {
    FileHandle.standardError.write(Data("usage: tamper IN MEMBER flip|FILE OUT\n".utf8)); exit(2)
}
do {
    var members = try readArchive([UInt8](input))
    guard let i = members.firstIndex(where: { $0.path == a[2] }), case .file(var data) = members[i].kind else {
        FileHandle.standardError.write(Data("tamper: no file member \(a[2])\n".utf8)); exit(1)
    }
    if a[3] == "flip" {
        data[data.count - 1] ^= 0xff
    } else {
        data = [UInt8](FileManager.default.contents(atPath: a[3]) ?? Data())
    }
    members[i].kind = .file(data)
    FileManager.default.createFile(atPath: a[4], contents: Data(try writeArchive(members)))
} catch {
    FileHandle.standardError.write(Data("tamper: \(error)\n".utf8)); exit(1)
}
