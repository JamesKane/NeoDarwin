<!-- SPDX-License-Identifier: BSD-2-Clause -->
# Third-party notices

NeoDarwin's own code is under the BSD 2-Clause licence (`LICENSE`). NeoDarwin also builds third-party code, pinned by version and hash in `MODULE.bazel` and the `upstream.lock` files (`base/upstream.lock`, `kernel/upstream.lock`, `third_party/*/upstream.lock`) and never vendored into this repository. Each component stays under its own licence. Changes to upstream files are the numbered patches beside each component, under the component's licence. Binaries NeoDarwin distributes carry the notices below.

## Components

| Component | Pin | Licence | Where NeoDarwin uses it |
|---|---|---|---|
| xnu | xnu-12377.1.9 | APSL-2.0 (BSD-derived parts keep their BSD notices) | the kernel |
| hfs | hfs-704.0.3.0.2 | APSL-2.0 | HFS+, built into the kernel; its userland tools `mount_hfs`, `newfs_hfs` and `fsck_hfs` |
| libpthread `kern/` | libpthread-539 | APSL-2.0 | built into the kernel |
| IOPCIFamily | IOPCIFamily-726.0.5 | APSL-2.0 (the archive's `APPLE_LICENSE`; some file headers name APSL 1.1, whose versions clause lets it be used under any later version) | PCI enumeration, built into the kernel (`kernel/neodarwin/pci`) |
| IOStorageFamily | IOStorageFamily-331 | APSL-2.0 | block storage, IOMedia, partition schemes and `/dev/disk` nodes, built into the kernel (`kernel/neodarwin/storage`) |
| AppleFileSystemDriver | AppleFileSystemDriver-31 | APSL-2.0 | the root by boot-uuid, built into the kernel (`kernel/neodarwin/storage`) |
| IONetworkingFamily | IONetworkingFamily-186 | APSL-2.0 (the archive's `APPLE_LICENSE`; the file headers name APSL 1.1, whose versions clause lets it be used under any later version) | Ethernet controllers and interfaces, built into the kernel (`kernel/neodarwin/network`) |
| ACPICA | 20260408 | Intel dual licence; **used under BSD-3-Clause** (notice below) | ACPI in the kernel (`kernel/neodarwin/acpi`) |
| FreeBSD crypto and `msun`, `libxo` | FreeBSD `freebsd-src` at the commits in the locks | BSD-2-Clause / BSD-3-Clause, per file | the kernel's crypto provider, `libsystem_m`, `libxo` |
| FreeBSD Qualcomm GENI UART driver (`sys/dev/uart/uart_dev_qcom_geni.c`, `sys/dev/qcom_geni/qcom_geni_reg.h`) | `freebsd-src` commit 124c151cbc (branch `radxa-dragon-q8b`), ported into `kernel/neodarwin/serial/nd_geni_uart.h` with its notice (`PROVENANCE.md` there) | BSD-2-Clause (notice below) | the kernel's serial console on the Radxa Dragon Q8B |
| FreeBSD ACPI xHCI attachment and DWC3 registers (`sys/dev/usb/controller/generic_xhci_acpi.c`, `sys/dev/usb/controller/dwc3/dwc3.h`) | `freebsd-src` commit cbbcf73a5d (branch `radxa-dragon-q8b`), the DWC3 role-switch set-up ported into `kernel/neodarwin/usb/nd_dwc3.h` with its notices (`PROVENANCE.md` there) | BSD-2-Clause (notice below) | the console USB keyboard's USB-C controllers on the Radxa Dragon Q8B |
| dyld, Libc, libplatform, libpthread, libmalloc, Libinfo, libclosure, Libnotify, syslog, copyfile, removefile, Libsystem, objc4, cctools (libmacho), AvailabilityVersions | per `base/upstream.lock` | APSL-2.0 (BSD-derived parts keep their BSD notices) | the userland base (libSystem and the libraries under it) |
| libdispatch | libdispatch-1542.0.4 | Apache-2.0 | `libdispatch.dylib` |
| mDNSResponder (client library) | mDNSResponder-2881.0.25 | Apache-2.0 | `libsystem_dnssd` |
| launchd | launchd-842.92.1 | Apache-2.0 | `/sbin/launchd`, liblaunch in `libxpc`, `/usr/libexec/launchproxy` |
| LLVM runtimes (libc++, libc++abi, libunwind, compiler-rt builtins) | swiftlang/llvm-project swift-6.2-RELEASE | Apache-2.0 WITH LLVM-exception | the C++ runtime and `libcompiler_rt` |
| system_cmds, shell_cmds, file_cmds, text_cmds, adv_cmds, diskdev_cmds, libutil, files | per `base/upstream.lock` | APSL-2.0 and BSD (most commands are BSD-derived; each file keeps its notice) | commands and `/etc` |
| OpenPAM | OpenPAM-35 (OpenPAM 20071221) | BSD-3-Clause | `libpam.2.dylib`, the `libpam.1.dylib` shim, and the modules `pam_deny`, `pam_permit` and `pam_unix` in `/usr/lib/pam` |
| pam_modules | pam_modules-217.0.1 | per file: BSD-3-Clause (`pam_group`, from FreeBSD), Linux-PAM's BSD-style licence (`pam_env`, `pam_nologin`, `pam_rootok`), Apple's BSD-style licence (`pam_self`, `pam_uwtmp`), APSL-2.0 (`pam_launchd`, `pam_sacl`) | the other modules in `/usr/lib/pam` |
| OpenBSM | OpenBSM-21 (OpenBSM 1.1; the last OpenBSM Apple published, Mac OS X 10.6.8) | BSD-2-Clause and BSD-3-Clause (per file; `LICENSE`) | `libbsm.0.dylib`, `libauditd.0.dylib`, `auditd`, `audit`, `auditreduce`, `praudit`, `/etc/security` and `com.apple.auditd.plist` |
| ncurses | ncurses-79 (ncurses 6.0) | MIT/X11 | `libncurses` and the terminfo database |
| zsh | zsh-110.1.1 (zsh 5.9) | the zsh licence (MIT-style) | `/bin/zsh` |
| libedit | libedit-65 (NetBSD libedit 20121213-3.0) | BSD-3-Clause | `libedit.3.dylib`, which `/bin/sh` (ash) links for line editing and history |
| bash | bash-140 (bash 3.2.57) | **GPL-2.0-or-later** (copyleft; the `lib/readline` and `lib/intl` it links statically are GPL and LGPL-2.0) | `/bin/bash`, a separate program: nothing else links it or its readline. Its complete corresponding source is the pinned archive (URL and hash in `MODULE.bazel` and `base/upstream.lock`) with NeoDarwin's build script (`base/bash`), which must be offered with any binary distribution |
| network_cmds | network_cmds-726 | APSL-2.0 and BSD (the commands are BSD-derived; each file keeps its notice) | `ifconfig`, `ping`, `netstat`, `route` |
| LibreSSL | libressl-3.3.6 (upstream, ftp.openbsd.org; the version macOS 26 ships) | the OpenSSL and original SSLeay licences (BSD-style, with advertising clauses) for code from OpenSSL, ISC for LibreSSL's own; per file (`COPYING`; acknowledgements below) | `libcrypto.46.dylib` |
| OpenSSH | OpenSSH-354.0.3 (OpenSSH 10.0p2) | BSD-style: the `LICENCE` file's terms (BSD-2-Clause and BSD-3-Clause, ISC, and public-domain parts, per file) | `ssh`, `sshd`, `sshd-session`, `sshd-auth`, `ssh-keygen`, `ssh-add`, `ssh-agent`, `ssh-keyscan`, `scp`, `sftp`, `sftp-server`, `/etc/ssh`, `/etc/pam.d/sshd` and `ssh.plist` |
| Swift Embedded standard library | swift.org toolchain 6.3.2 | Apache-2.0 WITH Swift runtime library exception | linked into neoboot and NeoDarwin's Swift programs |
| TF-A, EDK2, edk2-platforms | TF-A v2.15.0 (with upstream fix 5c33fafc for `qemu_sbsa`), edk2-stable202608, edk2-platforms 061beb4c (SbsaQemu) | BSD-3-Clause, BSD-2-Clause-Patent | test firmware for QEMU `virt,secure=on` and `sbsa-ref` only (`third_party/qemu_firmware`); not part of NeoDarwin images |

The authoritative licence of a component is the one in its pinned archive. This table summarises it; where they disagree, the archive wins.

## ACPICA

NeoDarwin uses ACPICA under the BSD-3-Clause option of its licence. As that option requires:

```
Copyright © 2000 – 2026 Intel Corp.
All rights reserved.

Alternatively, you may choose to be licensed under the terms of the
following license:

Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions
are met:
1. Redistributions of source code must retain the above copyright
   notice, this list of conditions, and the following disclaimer,
   without modification.
2. Redistributions in binary form must reproduce at minimum a disclaimer
   substantially similar to the "NO WARRANTY" disclaimer below
   ("Disclaimer") and any redistribution must be conditioned upon
   including a substantially similar Disclaimer requirement for further
   binary redistribution.
3. Neither the names of the above-listed copyright holders nor the names
   of any contributors may be used to endorse or promote products derived
   from this software without specific prior written permission.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
"AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
(INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
```

## FreeBSD Qualcomm GENI UART driver

The kernel's GENI UART console (`kernel/neodarwin/serial/nd_geni_uart.h`) is ported from FreeBSD's `uart_dev_qcom_geni.c` and `qcom_geni_reg.h`:

```
Copyright (c) 2026 James Kane

Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions
are met:
1. Redistributions of source code must retain the above copyright
   notice, this list of conditions and the following disclaimer.
2. Redistributions in binary form must reproduce the above copyright
   notice, this list of conditions and the following disclaimer in the
   documentation and/or other materials provided with the distribution.

THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
SUCH DAMAGE.
```

## FreeBSD ACPI xHCI attachment and DWC3 registers

The console USB keyboard's DWC3 role-switch set-up (`kernel/neodarwin/usb/nd_dwc3.h`) is ported from FreeBSD's `generic_xhci_acpi.c` and `dwc3.h`:

```
Copyright (c) 2019 Val Packett <val@packett.cool>
Copyright (c) 2019 Emmanuel Vadot <manu@FreeBSD.Org>

Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions
are met:
1. Redistributions of source code must retain the above copyright
   notice, this list of conditions and the following disclaimer.
2. Redistributions in binary form must reproduce the above copyright
   notice, this list of conditions and the following disclaimer in the
   documentation and/or other materials provided with the distribution.

THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
SUCH DAMAGE.
```

## LibreSSL

`libcrypto.46.dylib` is LibreSSL's libcrypto, which descends from OpenSSL and SSLeay. As their licences require:

This product includes software developed by the OpenSSL Project for use in the OpenSSL Toolkit (http://www.openssl.org/).

This product includes cryptographic software written by Eric Young (eay@cryptsoft.com).

The full licence texts are in the pinned archive's `COPYING`.
