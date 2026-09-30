// SPDX-License-Identifier: BSD-2-Clause
//
// cs_probe (P1-15, docs/kernel/amfi-provider.md): what the kernel's
// code-signing policy grants the process running it. The session images
// carry it three times under /usr/local/libexec/cs_probe, all signed ad hoc
// with the entitlements in entitlements.plist: `listed`, in the image's
// trust cache, and `unlisted`, which is not (a different signing identifier,
// so a different cdhash), with `unlisted.dylib`, a copy of libutil that the
// trust cache also leaves out. It prints
//
//   1. its code-signing status: platform binary or not (csops);
//   2. the entitlements the kernel grants it (csops CS_OPS_ENTITLEMENTS_BLOB,
//      answered by ndamfi only for a trusted binary);
//   3. a check the kernel makes with the entitlement: as uid -2, forcing
//      case-sensitive lookups needs com.apple.private.iopol.case_sensitivity;
//   4. with --dlopen PATH, whether dyld may load PATH.
//
// Under enforcement `listed` is a platform binary with its entitlements,
// and `unlisted` never runs. Without enforcement (nd_cs_enforcement=0),
// `unlisted` runs with none.

import Probe

@main
struct CSProbe {
    static func main() {
        var flags: UInt32 = 0
        guard nd_cs_status(&flags) else {
            print("cs_probe: csops failed")
            exit(1)
        }
        print("cs_probe: \(nd_cs_platform(flags) ? "platform binary" : "not a platform binary"), "
            + "\(nd_cs_valid(flags) ? "valid" : "invalid")\(nd_cs_kill(flags) ? ", CS_KILL" : "")")
        var names = false
        let granted = nd_cs_granted_xml("com.apple.private.iopol.case_sensitivity", &names)
        if granted > 0 {
            print("cs_probe: entitlements granted: \(granted) bytes of XML\(names ? ", com.apple.private.iopol.case_sensitivity among them" : "")")
        } else if granted == 0 {
            print("cs_probe: entitlements granted: none")
        } else {
            print("cs_probe: entitlements: csops failed")
        }
        if let option = nd_argument(1), strcmp(option, "--dlopen") == 0, let path = nd_argument(2) {
            let name = String(cString: path)
            if let why = nd_dlopen_error(path) {
                print("cs_probe: dlopen \(name): refused: \(String(cString: why))")
            } else {
                print("cs_probe: dlopen \(name): loaded")
            }
        }
        let error = nd_iopol_as_nobody()
        print("cs_probe: case-sensitivity policy as uid -2: \(error == 0 ? "granted by the kernel" : "denied (errno \(error))")")
    }
}
