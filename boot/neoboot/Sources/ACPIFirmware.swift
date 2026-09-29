// SPDX-License-Identifier: BSD-2-Clause
//
// The firmware side of ACPI: finding the RSDP through the UEFI
// configuration table, reading tables where the firmware left them, the
// dump-acpi listing, and the loader's report of what it found. The parsing
// itself is target-independent (Portable/ACPI.swift).

import UEFI

/// Tables in place: UEFI maps memory one to one while boot services run.
struct PhysicalMemory: ACPIMemory {
    func bytes(at address: UInt64, count: Int) -> UnsafeRawPointer? {
        guard address != 0, count >= 0, address <= UInt64(UInt.max) - UInt64(count) else { return nil }
        return UnsafeRawPointer(bitPattern: UInt(address))
    }
}

extension Firmware {
    /// The ACPI 2.0+ RSDP from the configuration table (EFI_ACPI_20_TABLE_GUID,
    /// UEFI 2.10 §4.6.1), or nil if the firmware publishes none.
    func rsdp(_ system: UnsafeMutablePointer<EFI_SYSTEM_TABLE>) -> UInt64? {
        guard let table = system.pointee.ConfigurationTable else { return nil }
        for i in 0..<Int(system.pointee.NumberOfTableEntries) {
            let e = table[i]
            let g = e.VendorGuid
            if g.Data1 == 0x8868_e871, g.Data2 == 0xe4f1, g.Data3 == 0x11d3,
               g.Data4.0 == 0xbc, g.Data4.1 == 0x22, g.Data4.2 == 0x00, g.Data4.3 == 0x80,
               g.Data4.4 == 0xc7, g.Data4.5 == 0x3c, g.Data4.6 == 0x88, g.Data4.7 == 0x81,
               let v = e.VendorTable {
                return UInt64(UInt(bitPattern: v))
            }
        }
        return nil
    }
}

func putSignature(_ s: UInt32) {
    for i in 0..<4 {
        let c = UInt8(truncatingIfNeeded: s >> (UInt32(i) * 8))
        Console.byte(c >= 0x20 && c < 0x7f ? c : 0x3f)
    }
}

func putHexDigits(_ v: UInt64, _ digits: Int) {
    for i in stride(from: digits - 1, through: 0, by: -1) {
        let d = UInt8((v >> UInt64(i * 4)) & 0xf)
        Console.byte(d < 10 ? 48 + d : 55 + d)
    }
}

func report(_ e: ACPIError) -> EFI_STATUS {
    put("neoboot: ACPI: ")
    put(e.message)
    if e.signature != 0 {
        put(" [")
        putSignature(e.signature)
        put("]")
    }
    if e.value != 0 {
        put(" ")
        putHex(e.value)
    }
    put("\nneoboot: cannot describe this machine to the kernel without valid ACPI tables\n")
    return efiLoadError
}

/// Every table in Linux acpidump's text format, which acpixtract reads and
/// dtdump takes as input: "SIG @ 0xADDRESS", rows of sixteen bytes as hex
/// and ASCII, and a blank line after each table.
func dumpACPI(rsdp: UInt64) throws(ACPIError) {
    put("neoboot: dump-acpi: begin\n")
    try ACPI.forEachTable(PhysicalMemory(), rsdp: rsdp) { signature, address, p, n in
        put(signature == ACPI.rsdp ? "RSDP" : "")
        if signature != ACPI.rsdp { putSignature(signature) }
        put(" @ 0x")
        putHexDigits(address, 16)
        put("\n")
        var row = 0
        while row < n {
            put("    ")
            putHexDigits(UInt64(row), 4)
            put(": ")
            for i in 0..<16 {
                if row + i < n {
                    putHexDigits(UInt64(p.load(fromByteOffset: row + i, as: UInt8.self)), 2)
                    put(" ")
                } else {
                    put("   ")
                }
            }
            put(" ")
            for i in 0..<min(16, n - row) {
                let c = p.load(fromByteOffset: row + i, as: UInt8.self)
                Console.byte(c >= 0x20 && c < 0x7f ? c : 0x2e)
            }
            put("\n")
            row += 16
        }
        put("\n")
    }
    put("neoboot: dump-acpi: end\n")
}

/// What the loader read, for the serial log.
func reportACPI(_ a: ACPIFacts, _ l: Platform.Layout) {
    put("neoboot: ACPI: ")
    putDec(UInt64(a.enabledCPUs))
    put(" CPUs enabled of ")
    putDec(UInt64(a.gicCount))
    put(" GICCs; boot CPU MPIDR ")
    putHex(a.cpus[l.bootIndex].mpidr)
    put("\nneoboot: ACPI: GICv3 GICD ")
    putHex(a.gicdBase)
    put(", GICR ")
    putHex(a.gicrBase)
    put(" (")
    putDec(UInt64(a.gicrFrames))
    put(a.gicrFromGICC ? " frames from GICC entries)\n" : " frames in a GICR range)\n")
    put("neoboot: ACPI: timer PPI ")
    putDec(UInt64(a.timerGSIV))
    put((a.timerFlags & 1) != 0 ? " edge" : " level")
    put(", UART ")
    put(a.uartType == ACPI.uartPL011 ? "PL011" : "SBSA generic")
    put(" at ")
    putHex(a.uartBase)
    put(", PSCI ")
    put((a.armBootFlags & 1) == 0 ? "absent" : (a.armBootFlags & 2) != 0 ? "HVC" : "SMC")
    put("\n")
    if l.droppedCPUs != 0 {
        put("neoboot: ACPI: ")
        putDec(UInt64(l.droppedCPUs))
        put(" CPUs past the kernel's MAX_CPUS (32) are left out of /cpus\n")
    }
}

/// neoboot's own tree check: DT-ABI violations go to the console.
struct ConsoleReport: DTReport {
    mutating func violation(_ rule: StaticString, _ value: UInt64, hasValue: Bool) {
        put("neoboot: DT-ABI v1 violation: ")
        put(rule)
        if hasValue {
            put(" ")
            putHex(value)
        }
        put("\n")
    }
}
