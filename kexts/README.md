<!-- SPDX-License-Identifier: BSD-2-Clause -->
# kexts

Kernel extensions: `ndacpi`, `zfs`, `hfs`, `ndfuse`, `msdosfs`, `ndfb`, `virtio`, `nvme`, `ahci`, `ndusb`. Design: `docs/architecture/drivers.md`, `docs/architecture/filesystems.md`. Downstream kexts (for example Magi's `nd9p` and `ndsandbox`) live in their own repositories (`docs/architecture/downstream.md` §4).

`swift_trial` is not a product kext. It is P0-10's `kext_swift` trial: Embedded Swift logic behind a C++ IOService, booted only by `//kernel:sbsa_swift_trial_test`. The verdict is in `docs/architecture/language-policy.md` §3.1.
