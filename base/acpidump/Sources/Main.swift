// SPDX-License-Identifier: BSD-2-Clause
//
// acpidump: FreeBSD's ACPI table dumper over the I/O Registry (P4-21
// checkpoint 6b, docs/architecture/freebsd-parity.md §2.1). FreeBSD maps
// the tables from /dev/mem; arm64 xnu has none, so NeoDarwin's ACPI
// platform publishes them as "ACPI Tables" (docs/kernel/acpi.md): a
// dictionary from signature (FACP, DSDT, SSDT, SSDT-1, ..., plus RSDP and
// XSDT) to the whole table as data. The entry that carries it is found by
// searching the service plane from the root.
//
// -t prints the RSDP, the XSDT and every table's header as FreeBSD does,
// with FreeBSD's MCFG decoding; the FADT is followed by the FACS and the
// DSDT, which hang off it. Other tables' bodies aren't decoded. The tables
// come in signature order, FACP first: the dictionary doesn't keep the
// XSDT's order. -T SIG prints one signature's tables. -o FILE writes the
// DSDT with the SSDTs' bodies appended and the checksum redone, as FreeBSD
// does; -d and -s disassemble it (or, with -f, a DSDT file) with
// /usr/sbin/iasl -d.
//
// Test seam: -X PLIST reads the dictionary from a property list file
// instead of the registry (base/acpidump/host_test.sh).
// Embedded Swift (language policy T1 tool, built as T3) over NDIOKit.

import NDIOKit

struct Table {
    var key: String
    var bytes: [UInt8]

    var signature: String { String(decoding: bytes.prefix(4), as: UTF8.self) }
    func u8(_ o: Int) -> UInt32 { o < bytes.count ? UInt32(bytes[o]) : 0 }
    func u16(_ o: Int) -> UInt32 { u8(o) | u8(o + 1) << 8 }
    func u32(_ o: Int) -> UInt32 { u16(o) | u16(o + 2) << 16 }
    func u64(_ o: Int) -> UInt64 { UInt64(u32(o)) | UInt64(u32(o + 4)) << 32 }
    var length: Int { bytes.count >= 8 ? Int(u32(4)) : 0 }
    /// A fixed-width string field without its trailing spaces and NULs.
    func string(_ o: Int, _ n: Int) -> String {
        var s = Array(bytes[min(o, bytes.count)..<min(o + n, bytes.count)])
        while let last = s.last, last == 0x20 || last == 0 { s.removeLast() }
        return String(decoding: s, as: UTF8.self)
    }
}

let headerSize = 36
nonisolated(unsafe) var verbose = false

func warn(_ message: String) {
    nd_warn("acpidump: " + message)
}

func fail(_ message: String) -> Never {
    warn(message)
    exit(1)
}

func hex(_ value: UInt64, _ width: Int) -> String {
    var s = String(value, radix: 16)
    while s.count < width { s = "0" + s }
    return s
}

func checksum(_ bytes: ArraySlice<UInt8>) -> UInt8 {
    bytes.reduce(0, &+)
}

// MARK: - Loading

func tablesFrom(_ dict: nd_cf_t) -> [Table] {
    let n = nd_cf_dictionary_count(dict)
    var keys = [nd_cf_t?](repeating: nil, count: n)
    var values = [nd_cf_t?](repeating: nil, count: n)
    nd_cf_dictionary_entries(dict, &keys, &values)
    var result: [Table] = []
    var buf = [CChar](repeating: 0, count: 64)
    for i in 0..<n {
        guard let k = keys[i], let v = values[i], nd_cf_kind(k) == ND_CF_STRING, nd_cf_kind(v) == ND_CF_DATA,
            nd_cf_string_copy(k, &buf, buf.count)
        else { continue }
        let key = buf.withUnsafeBufferPointer { String(cString: $0.baseAddress!) }
        let len = nd_cf_data_length(v)
        var bytes = [UInt8](repeating: 0, count: len)
        if len > 0, let p = nd_cf_data_bytes(v) {
            for j in 0..<len { bytes[j] = p[j] }
        }
        result.append(Table(key: key, bytes: bytes))
    }
    return result
}

func loadTables(plist: String?) -> [Table] {
    let dict: nd_cf_t
    if let path = plist {
        if verbose { warn("loading tables from \(path)") }
        guard let p = nd_cf_property_list_read(path) else { fail("\(path): can't read a property list") }
        dict = p
    } else {
        if verbose { warn("loading ACPI Tables from the I/O Registry") }
        let root = nd_io_root()
        defer { nd_io_release(root) }
        guard let p = nd_io_search_property(root, "ACPI Tables", false) else {
            fail("Can't find ACPI information: no \"ACPI Tables\" in the I/O Registry")
        }
        dict = p
    }
    defer { nd_cf_release(dict) }
    var top = dict
    if nd_cf_kind(dict) == ND_CF_DICTIONARY, let inner = nd_cf_dictionary_value(dict, "ACPI Tables") { top = inner }
    guard nd_cf_kind(top) == ND_CF_DICTIONARY else { fail("ACPI Tables isn't a dictionary") }
    return tablesFrom(top)
}

/// FACP first, then by signature, then SIG before SIG-1, SIG-2, ... (their order in the XSDT).
func order(_ a: Table, _ b: Table) -> Bool {
    func rank(_ t: Table) -> (Int, String, Int) {
        let parts = t.key.split(separator: "-")
        let base = parts.first.map(String.init) ?? t.key
        let n = parts.count > 1 ? Int(parts[1]) ?? 0 : 0
        return (base == "FACP" ? 0 : 1, base, n)
    }
    let (ra, rb) = (rank(a), rank(b))
    if ra.0 != rb.0 { return ra.0 < rb.0 }
    if ra.1 != rb.1 { return ra.1 < rb.1 }
    return ra.2 < rb.2
}

// MARK: - Printing (FreeBSD's acpi.c formats)

func printSDT(_ t: Table) {
    print("  \(t.string(0, 4)): Length=\(Int32(bitPattern: t.u32(4))), Revision=\(t.u8(8)), Checksum=\(t.u8(9)),")
    print("\tOEMID=\(t.string(10, 6)), OEM Table ID=\(t.string(16, 8)), OEM Revision=0x\(String(t.u32(24), radix: 16)),")
    print("\tCreator ID=\(t.string(28, 4)), Creator Revision=0x\(String(t.u32(32), radix: 16))")
}

func printRSDP(_ t: Table) {
    print("/*")
    let rev = t.u8(15)
    print("  RSD PTR: OEM=\(t.string(9, 6)), ACPI_Rev=\(rev < 2 ? "1.0x" : "2.0x") (\(rev))")
    if rev < 2 {
        print("\tRSDT=0x\(hex(UInt64(t.u32(16)), 8)), cksum=\(t.u8(8))")
    } else {
        print("\tXSDT=0x\(hex(t.u64(24), 16)), length=\(t.u32(20)), cksum=\(t.u8(32))")
    }
    print(" */")
}

func printXSDT(_ t: Table) {
    print("/*")
    printSDT(t)
    let size = t.signature == "RSDT" ? 4 : 8
    var entries: [String] = []
    var o = headerSize
    while o + size <= t.bytes.count {
        entries.append(size == 4 ? "0x\(hex(UInt64(t.u32(o)), 8))" : "0x\(hex(t.u64(o), 16))")
        o += size
    }
    var line = "\tEntries={ "
    for (i, e) in entries.enumerated() { line += (i > 0 ? ", " : "") + e }
    print(line + " }")
    print(" */")
}

func printGeneric(_ t: Table) {
    print("/*")
    printSDT(t)
    print(" */")
}

func printMCFG(_ t: Table) {
    print("/*")
    printSDT(t)
    var o = headerSize + 8
    while o + 16 <= t.bytes.count {
        print("")
        print("\tBase Address=0x\(hex(t.u64(o), 16))")
        print("\tSegment Group=0x\(hex(UInt64(t.u16(o + 8)), 4))")
        print("\tStart Bus=\(t.u8(o + 10))")
        print("\tEnd Bus=\(t.u8(o + 11))")
        o += 16
    }
    print(" */")
}

func printFACS(_ t: Table) {
    print("/*")
    print("  FACS:\tLength=\(t.u32(4)), HwSig=0x\(hex(UInt64(t.u32(8)), 8)), Firm_Wake_Vec=0x\(hex(UInt64(t.u32(12)), 8))")
    print("\tVersion=\(t.u8(32))")
    print(" */")
}

func report(_ t: Table, _ all: [Table]) {
    if checksum(t.bytes[...]) != 0 { warn("\(t.key) is corrupt (checksum)") }
    switch t.signature {
    case "MCFG": printMCFG(t)
    case "FACP":
        printGeneric(t)
        // FreeBSD's acpi_handle_fadt: the FACS and the DSDT follow the FADT.
        if let facs = all.first(where: { $0.key == "FACS" }) { printFACS(facs) }
        if let dsdt = all.first(where: { $0.key == "DSDT" }) { printGeneric(dsdt) }
    default: printGeneric(t)
    }
}

func printTables(_ tables: [Table], only sig: String?) {
    let sorted = tables.sorted(by: order)
    guard let sig else {
        if let rsdp = tables.first(where: { $0.key == "RSDP" }) { printRSDP(rsdp) }
        if let xsdt = tables.first(where: { $0.key == "XSDT" }) { printXSDT(xsdt) }
        // The DSDT and FACS print with the FADT.
        for t in sorted where !["RSDP", "XSDT", "DSDT", "FACS"].contains(t.key) { report(t, tables) }
        return
    }
    for t in sorted where t.key != "RSDP" && t.signature == sig {
        if t.key == "XSDT" { printXSDT(t) } else { report(t, []) }
    }
}

// MARK: - The DSDT for -o and -d

/// The DSDT with the SSDTs' bodies appended and its checksum redone (write_dsdt).
func combinedDSDT(_ dsdt: Table, ssdts: [Table]) -> [UInt8] {
    var out = Array(dsdt.bytes.prefix(dsdt.length))
    guard !ssdts.isEmpty, out.count >= headerSize else { return out }
    for s in ssdts where s.length > headerSize {
        out += s.bytes[headerSize..<min(s.length, s.bytes.count)]
    }
    let len = UInt32(out.count)
    for i in 0..<4 { out[4 + i] = UInt8(truncatingIfNeeded: len >> (8 * UInt32(i))) }
    out[9] = 0
    out[9] = 0 &- checksum(out[...])
    return out
}

func writeFile(_ path: String, _ bytes: [UInt8]) -> Bool {
    guard let f = fopen(path, "w") else { return false }
    let n = bytes.withUnsafeBufferPointer { fwrite($0.baseAddress, 1, $0.count, f) }
    return fclose(f) == 0 && n == bytes.count
}

func errnoString() -> String {
    String(cString: strerror(__error().pointee))
}

/// FreeBSD's aml_disassemble: iasl -d on a file in a temporary directory, its .dsl to standard output.
func disassemble(_ aml: [UInt8]) {
    let tmp = getenv("TMPDIR").map { String(cString: $0) } ?? "/tmp"
    var template = Array((tmp + "/acpidump.XXXXXX").utf8CString)
    guard template.withUnsafeMutableBufferPointer({ mkdtemp($0.baseAddress!) }) != nil else {
        warn("mkdtemp tmp working dir: \(errnoString())")
        return
    }
    let dir = template.withUnsafeBufferPointer { String(cString: $0.baseAddress!) }
    let input = dir + "/acpdump.din", output = dir + "/acpdump.dsl"
    defer { unlink(input); unlink(output); rmdir(dir) }
    guard writeFile(input, aml) else {
        warn("iasl tmp file: \(errnoString())")
        return
    }
    var pid: pid_t = 0
    var actions: posix_spawn_file_actions_t? = nil
    posix_spawn_file_actions_init(&actions)
    defer { posix_spawn_file_actions_destroy(&actions) }
    posix_spawn_file_actions_addopen(&actions, 1, "/dev/null", O_WRONLY, 0)
    if !verbose { posix_spawn_file_actions_addopen(&actions, 2, "/dev/null", O_WRONLY, 0) }
    let args = ["iasl", "-d", input]
    var cargs = args.map { strdup($0) }
    cargs.append(nil)
    defer { for p in cargs { free(p) } }
    let iasl = "/usr/sbin/iasl"
    let rc = posix_spawn(&pid, iasl, &actions, nil, &cargs, nil)
    guard rc == 0 else {
        warn("exec \(iasl): \(String(cString: strerror(rc)))")
        return
    }
    var status: Int32 = 0
    waitpid(pid, &status, 0)
    if status != 0 { warn("iasl exit status = \(status)") }
    guard let f = fopen(output, "r") else {
        warn("iasl tmp file (read): \(errnoString())")
        return
    }
    fflush(nd_stdout())
    var buf = [UInt8](repeating: 0, count: 4096)
    while true {
        let n = buf.withUnsafeMutableBufferPointer { fread($0.baseAddress, 1, $0.count, f) }
        if n == 0 { break }
        _ = buf.withUnsafeBufferPointer { fwrite($0.baseAddress, 1, n, nd_stdout()) }
    }
    fclose(f)
}

func readFile(_ path: String) -> [UInt8]? {
    guard let f = fopen(path, "r") else { return nil }
    defer { fclose(f) }
    var out: [UInt8] = []
    var buf = [UInt8](repeating: 0, count: 4096)
    while true {
        let n = buf.withUnsafeMutableBufferPointer { fread($0.baseAddress, 1, $0.count, f) }
        if n == 0 { break }
        out += buf[0..<n]
    }
    return out
}

// MARK: - main

func usage() -> Never {
    nd_warn("usage: acpidump [-d] [-s] [-t] [-T NAME] [-h] [-v] [-f dsdt_input] [-o dsdt_output]\nTo send ASL:\n\tacpidump -dt | gzip -c9 > foo.asl.gz")
    exit(1)
}

@_cdecl("main")
func acpidumpMain(_ argc: Int32, _ argv: UnsafeMutablePointer<UnsafeMutablePointer<CChar>?>) -> Int32 {
    var args: [String] = []
    for i in 1..<Int(argc) {
        if let p = argv[i] { args.append(String(cString: p)) }
    }
    if args.isEmpty { usage() }
    var dflag = 0, tflag = false
    var tbl: String? = nil, input: String? = nil, output: String? = nil, plist: String? = nil
    var i = 0
    while i < args.count {
        let a = args[i]
        i += 1
        guard a.hasPrefix("-"), a.count > 1, a != "--" else { usage() }
        var opts = Array(a.utf8.dropFirst())
        while !opts.isEmpty {
            let c = opts.removeFirst()
            func value() -> String {
                if !opts.isEmpty {
                    defer { opts = [] }
                    return String(decoding: opts, as: UTF8.self)
                }
                guard i < args.count else { usage() }
                i += 1
                return args[i - 1]
            }
            switch c {
            case UInt8(ascii: "d"): dflag = 1
            case UInt8(ascii: "s"): dflag = 2
            case UInt8(ascii: "t"): tflag = true
            case UInt8(ascii: "v"): verbose = true
            case UInt8(ascii: "T"):
                let t = value()
                if t.utf8.count != 4 {
                    warn("Illegal table name \(t)")
                    usage()
                }
                tbl = t
            case UInt8(ascii: "f"): input = value()
            case UInt8(ascii: "o"): output = value()
            case UInt8(ascii: "X"): plist = value()
            default: usage()
            }
        }
    }
    if input != nil {
        if dflag == 0 && !tflag {
            warn("Need to specify -d or -t with DSDT input file")
            usage()
        } else if tflag {
            warn("Can't use -t with DSDT input file")
            usage()
        }
    }

    var dsdt: [UInt8]
    var ssdts: [Table] = []
    if let path = input {
        if verbose { warn("loading DSDT file: \(path)") }
        guard let b = readFile(path) else { fail("\(path): \(errnoString())") }
        dsdt = b
    } else {
        let tables = loadTables(plist: plist)
        if tflag || tbl != nil {
            if verbose { warn("printing various SDT tables") }
            printTables(tables, only: tbl)
        }
        guard let d = tables.first(where: { $0.key == "DSDT" }) else {
            if output != nil || dflag != 0 { fail("no DSDT") }
            return 0
        }
        ssdts = tables.filter { $0.signature == "SSDT" }.sorted(by: order)
        dsdt = combinedDSDT(d, ssdts: dflag == 2 ? [] : ssdts)
        if let path = output {
            if verbose { warn("saving DSDT file: \(path)") }
            if !writeFile(path, combinedDSDT(d, ssdts: ssdts)) { warn("dsdt_save_file: \(errnoString())") }
        }
    }
    if dflag != 0 {
        if verbose { warn("disassembling DSDT, iasl messages follow") }
        disassemble(dsdt)
        if dflag == 2 {
            for s in ssdts { disassemble(Array(s.bytes.prefix(s.length))) }
        }
        if verbose { warn("iasl processing complete") }
    }
    return 0
}
