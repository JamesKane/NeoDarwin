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
| FreeBSD `libcrypt` (`lib/libcrypt`: `crypt-md5.c`, `crypt-sha256.c`, `crypt-sha512.c`, `misc.c`, `crypt.h`; `secure/lib/libcrypt`: `crypt-blowfish.c`, `blowfish.c`, `blowfish.h`) and its digests (`sys/crypto/md5c.c`, `sys/sys/md5.h`, `sys/crypto/sha2`) | `freebsd-src` at the commit in `base/libc/freebsd.lock` | per file: BSD-2-Clause (`crypt-md5.c`, `crypt-sha256.c` and `crypt-sha512.c`, the last two based on Ulrich Drepper's public-domain SHA-crypt; `crypt.h`; `md5c.c`; the SHA-2 files), BSD-3-Clause (`misc.c`), BSD-4-Clause (bcrypt: Niels Provos, notice below), RSA-MD (`md5.h`, notice below) | `crypt(3)`'s MD5, bcrypt, SHA-256 and SHA-512 schemes in `libsystem_c` (Libc patch 0001) |
| FreeBSD Qualcomm GENI UART driver (`sys/dev/uart/uart_dev_qcom_geni.c`, `sys/dev/qcom_geni/qcom_geni_reg.h`) | `freebsd-src` commit 124c151cbc (branch `radxa-dragon-q8b`), ported into `kernel/neodarwin/serial/nd_geni_uart.h` with its notice (`PROVENANCE.md` there) | BSD-2-Clause (notice below) | the kernel's serial console on the Radxa Dragon Q8B |
| FreeBSD ACPI xHCI attachment and DWC3 registers (`sys/dev/usb/controller/generic_xhci_acpi.c`, `sys/dev/usb/controller/dwc3/dwc3.h`) | `freebsd-src` commit cbbcf73a5d (branch `radxa-dragon-q8b`), the DWC3 role-switch set-up ported into `kernel/neodarwin/usb/nd_dwc3.h` with its notices (`PROVENANCE.md` there) | BSD-2-Clause (notice below) | the console USB keyboard's USB-C controllers on the Radxa Dragon Q8B |
| FreeBSD Toshiba TC956x Ethernet driver (`sys/dev/tcx/if_tcx.c`, `sys/dev/tcx/if_tcxreg.h`) | `freebsd-src` branch `radxa-dragon-q8b` at `4c5da483b9`, ported into `kernel/neodarwin/network/nd_tc956x.h` with its notice (`PROVENANCE.md` there) | BSD-2-Clause (notice below) | the kernel's Ethernet driver for the Radxa Dragon Q8B |
| dyld, Libc, libplatform, libpthread, libmalloc, Libinfo, libclosure, Libnotify, syslog, copyfile, removefile, Libsystem, objc4, cctools (libmacho), AvailabilityVersions | per `base/upstream.lock` | APSL-2.0 (BSD-derived parts keep their BSD notices) | the userland base (libSystem and the libraries under it) |
| libdispatch | libdispatch-1542.0.4 | Apache-2.0 | `libdispatch.dylib` |
| mDNSResponder | mDNSResponder-2881.0.25 | Apache-2.0 | `libsystem_dnssd` (the client library) and `/usr/sbin/mDNSResponder` (the published POSIX daemon, mDNSPosix) |
| libresolv | libresolv-93 | APSL-2.0, with the ISC and BSD notices of its BIND-derived files (per file) | `libresolv.9.dylib`. Its HMAC-MD5 uses FreeBSD's `sys/crypto/md5c.c` (RSA Data Security's MD5, notice below) |
| configd's `dnsinfo.h` | configd-1385.0.7 (one header, `base/libresolv/configd.lock`) | APSL-2.0 | declarations libresolv builds against; configd isn't built |
| FreeBSD `dhclient` | `freebsd-src` at the commit in `base/dhclient/freebsd.lock` | BSD-3-Clause (the ISC DHCP client's, OpenBSD's and FreeBSD's notices, per file); ISC (`dhclient-script`); BSD-2-Clause (`inet6.c`) | `/sbin/dhclient`, `/sbin/dhclient-script`, `/etc/dhclient.conf` |
| FreeBSD `rtadvd` | `freebsd-src` at the commit in `base/rtadvd/freebsd.lock` (`usr.sbin/rtadvd`, KAME's) | per file: BSD-3-Clause (the WIDE Project's KAME notice, with Hiroki Sato's for FreeBSD's rewrite; `advcap.c`, the Regents of the University of California); BSD-2-Clause (`advcap.h`, Andrey A. Chernov; `control*.c`, Hiroki Sato); `rtadvd.conf`, the WIDE Project's notice | `/usr/sbin/rtadvd`, `/etc/rtadvd.conf` |
| FreeBSD `rtsold` | `freebsd-src` at the commit in `base/rtsold/freebsd.lock` (`usr.sbin/rtsold`, KAME's) | per file: BSD-3-Clause (the WIDE Project's KAME notice: `rtsold.c`, `rtsol.c`, `if.c`, `dump.c`, `rtsock.c`, `cap_sendmsg.c`, `rtsold.h`), BSD-2-Clause (`cap_llflags.c`, `cap_script.c`, the FreeBSD Foundation) | `/usr/sbin/rtsold`, `/sbin/rtsol` |
| FreeBSD `resolvconf` (openresolv 3.17.4) | `freebsd-src` at the commit in `base/resolvconf/freebsd.lock` (`contrib/openresolv`) | BSD-2-Clause (Roy Marples; `LICENSE`) | `/sbin/resolvconf`, `/usr/libexec/resolvconf/libc` |
| WIDE-DHCPv6 (`dhcp6c`) | `hrs-allbsd/wide-dhcpv6` at the commit in `base/dhcp6c/wide.lock` (tag v20080615.2, FreeBSD's `net/dhcp6` port) | per file: BSD-3-Clause (the WIDE Project's notice, `COPYRIGHT`), with ISC (`base64.c` and `auth.c` also carry the Internet Systems/Software Consortium's ISC notice, for code from BIND) | `/usr/sbin/dhcp6c` |
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
| network_cmds | network_cmds-726 | APSL-2.0 and BSD (the commands are BSD-derived; each file keeps its notice) | `ifconfig`, `ping`, `netstat`, `route`, `arp`, `ndp`, `ping6`, `traceroute`, `traceroute6`, `rarpd`, `spray`, `kdumpd` (its `rtsol` is replaced by FreeBSD's) |
| libpcap | libpcap-144 (libpcap 1.10.1) | BSD-3-Clause (`libpcap/LICENSE`, tcpdump.org's) and, per file, the Regents' LBL notice (BSD-style, with an advertising acknowledgement), with APSL-2.0 for Apple's own files (`pcap-darwin.c`, `pcapng.c`, `pcap-util.c` and their headers) | `libpcap.A.dylib` |
| tcpdump | tcpdump-153 (tcpdump 4.99.1) | BSD-3-Clause (`tcpdump/LICENSE`) and, per file, the Regents' LBL notice (BSD-style, with an advertising acknowledgement) and others' BSD-style notices, with APSL-2.0 for Apple's own files (`print_pktap.c`, `pktaputil.c`, `pktmetadatafilter.c`, `print-kev_msg.c`) | `/usr/sbin/tcpdump` |
| ipsec (libipsec) | ipsec-1125 (KAME's libipsec, as ipsec-tools 0.7 ships it) | BSD-3-Clause (the WIDE Project's notice, per file; `ipsec.txt`) | `libipsec.A.dylib` |
| OpenBSD `pfctl` | OpenBSD 4.3's `sbin/pfctl` and `sys/net/pf_ruleset.c`, openbsd/src at the commit in `base/pfctl/openbsd.lock` | per file: BSD-2-Clause (`pfctl.c`, `parse.y`, `pfctl_parser.c`, `pf_print_state.c`, `pfctl_radix.c`, `pfctl_table.c`, `pf_ruleset.c`), ISC (`pfctl_optimize.c`, `pfctl_osfp.c`, and `etc/pf.os`, adapted from p0f); its MD5 is FreeBSD's `md5c.c` (RSA Data Security's MD5, notice below) | `/sbin/pfctl`, `/etc/pf.os` |
| ntp | ntp-139 (ntp 4.2.8p10; the last ntp Apple published, macOS 10.15) | the NTP licence (University of Delaware, notice below); the files it includes keep their notices: libisc (ISC), libevent (BSD-3-Clause), libopts (AutoOpts, offered under LGPL-3.0-or-later or a modified BSD licence: **used under the modified BSD licence**, `sntp/libopts/COPYING.mbsd`; its LGPL-only `parse-duration.c` isn't compiled, as in Apple's build) | `/usr/bin/sntp` |
| OpenSSL | openssl-3.5.9 (upstream release; FreeBSD's base library, the 3.5 LTS line) | Apache-2.0 (`LICENSE.txt`; the archive has no `NOTICE` file) | `libcrypto.3.dylib`, `libssl.3.dylib`, the legacy provider (`/usr/lib/ossl-modules`), the `capi` and `loader_attic` engines (`/usr/lib/engines-3`), `/usr/bin/openssl` and `/etc/ssl/openssl.cnf` |
| OpenSSH | OpenSSH-354.0.3 (OpenSSH 10.0p2) | BSD-style: the `LICENCE` file's terms (BSD-2-Clause and BSD-3-Clause, ISC, and public-domain parts, per file) | `ssh`, `sshd`, `sshd-session`, `sshd-auth`, `ssh-keygen`, `ssh-add`, `ssh-agent`, `ssh-keyscan`, `scp`, `sftp`, `sftp-server`, `/etc/ssh`, `/etc/pam.d/sshd` and `ssh.plist` |
| Swift Embedded standard library | swift.org toolchain 6.3.2 | Apache-2.0 WITH Swift runtime library exception | linked into neoboot and NeoDarwin's Swift programs |
| OpenZFS (with the OpenZFS on OS X macOS OS layer) | zfs-macOS-2.4.1p1, openzfsonosx/openzfs-fork commit a4c1b11ab900 (`kexts/zfs/upstream.lock`; OpenZFS 2.4.1) | **CDDL-1.0** (file-level copyleft; notice below), with the permissive parts its files name: BSD-2-Clause (`lz4.c`, `lz4_zfs.c`, `spl-debug.c`), BSD-2-Clause OR GPL-2.0-only (`zfs_fletcher_superscalar*.c`, **used under BSD-2-Clause**), BSD-3-Clause OR GPL-2.0-only (zstd under `module/zstd`, **used under BSD-3-Clause**), MIT (Lua 5.2 in `module/lua`, `cityhash.c`, `spl-qsort.c`, `sysctl_os.c`), public domain (Skein, `vdev_draid_rand.c`), the OpenSSL licence (`aesv8-armx.S`, `ghashv8-armx.S`; acknowledgement below), Apache-2.0 (`sha256-armv8.S`, `sha512-armv8.S`), BSD (`setjmp.S`, NeXT and the Regents) | `zfs.kext`, a separate kext in the boot kernel collection (never linked into the kernel image; `//kernel:sbsa_zfs_kc`), and `/sbin/zpool` and `/sbin/zfs` with libzfs, libzfs_core, libnvpair, libzutil, libspl and libefi linked in. The Linux SPL (`module/os/linux/spl`, GPL-2.0) and the rest of the Linux layer aren't built. NeoDarwin's own files beside it (`kexts/zfs/compat`, the build scripts) are BSD-2-Clause; its changes to OpenZFS files are `kexts/zfs/patches`, under CDDL-1.0 |
| TF-A, EDK2, edk2-platforms | TF-A v2.15.0 (with upstream fix 5c33fafc for `qemu_sbsa`), edk2-stable202608, edk2-platforms 061beb4c (SbsaQemu) | BSD-3-Clause, BSD-2-Clause-Patent | test firmware for QEMU `virt,secure=on` and `sbsa-ref` only (`third_party/qemu_firmware`); not part of NeoDarwin images |

The authoritative licence of a component is the one in its pinned archive. This table summarises it; where they disagree, the archive wins.

## OpenZFS

OpenZFS is under the Common Development and Distribution License, version 1.0 (`LICENSE` and `COPYRIGHT` in the pinned archive), except where a file says otherwise. The CDDL's copyleft is per file: the source of every CDDL file NeoDarwin distributes in executable form, including NeoDarwin's modifications to it, must be available under the CDDL. For NeoDarwin that source is the pinned archive (URL and hash in `MODULE.bazel` and `kexts/zfs/upstream.lock`) with the patches in `kexts/zfs/patches`, which `kexts/zfs/common.sh` applies after copying the macOS OS layer to `os/neodarwin`. Files that aren't CDDL keep their own notices in their headers; the archive's `COPYRIGHT` lists the third-party licence files.

`zfs.kext` contains, in `aesv8-armx.S` and `ghashv8-armx.S`, software under the OpenSSL licence, which asks binary distributions to carry this acknowledgement:

> This product includes software developed by the OpenSSL Project for use in the OpenSSL Toolkit (http://www.openssl.org/)

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

## FreeBSD Toshiba TC956x Ethernet driver

The kernel's TC956x Ethernet driver core (`kernel/neodarwin/network/nd_tc956x.h`) is ported from FreeBSD's `tcx` driver, `if_tcx.c` and `if_tcxreg.h`:

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

## RSA Data Security MD5

`libresolv.9.dylib`, `libsystem_c.dylib` and `/sbin/pfctl` (and the kernel's and dyld's digest code) include FreeBSD's `sys/crypto/md5c.c`, which is "derived from the RSA Data Security, Inc. MD5 Message-Digest Algorithm". As its licence requires:

> Copyright (C) 1991-2, RSA Data Security, Inc. Created 1991. All rights reserved.
>
> License to copy and use this software is granted provided that it is identified as the "RSA Data Security, Inc. MD5 Message-Digest Algorithm" in all material mentioning or referencing this software or this function.
>
> License is also granted to make and use derivative works provided that such works are identified as "derived from the RSA Data Security, Inc. MD5 Message-Digest Algorithm" in all material mentioning or referencing the derived work.
>
> RSA Data Security, Inc. makes no representations concerning either the merchantability of this software or the suitability of this software for any particular purpose. It is provided "as is" without express or implied warranty of any kind.
>
> These notices must be retained in any copies of any part of this documentation and/or software.

## FreeBSD libcrypt's bcrypt (Niels Provos)

`libsystem_c.dylib`'s bcrypt scheme (`crypt(3)` with a `$2a$`, `$2b$` or `$2y$` setting) is built from FreeBSD's `secure/lib/libcrypt/crypt-blowfish.c`, `blowfish.c` and `blowfish.h`, OpenBSD's bcrypt, under a four-clause BSD licence. This product includes software developed by Niels Provos.

```
Copyright 1997 Niels Provos <provos@physnet.uni-hamburg.de>
All rights reserved.

Implementation advice by David Mazieres <dm@lcs.mit.edu>.

Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions
are met:
1. Redistributions of source code must retain the above copyright
   notice, this list of conditions and the following disclaimer.
2. Redistributions in binary form must reproduce the above copyright
   notice, this list of conditions and the following disclaimer in the
   documentation and/or other materials provided with the distribution.
3. All advertising materials mentioning features or use of this software
   must display the following acknowledgement:
     This product includes software developed by Niels Provos.
4. The name of the author may not be used to endorse or promote products
   derived from this software without specific prior written permission.

THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
(INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
```

## The NTP distribution (University of Delaware)

`/usr/bin/sntp` is built from Apple's ntp-139, the Network Time Protocol Version 4 distribution (ntp 4.2.8p10). Its `COPYRIGHT` file applies to all of it unless a file says otherwise:

```
Copyright (c) University of Delaware 1992-2015

Permission to use, copy, modify, and distribute this software and
its documentation for any purpose with or without fee is hereby
granted, provided that the above copyright notice appears in all
copies and that both the copyright notice and this permission
notice appear in supporting documentation, and that the name
University of Delaware not be used in advertising or publicity
pertaining to distribution of the software without specific,
written prior permission. The University of Delaware makes no
representations about the suitability this software for any
purpose. It is provided "as is" without express or implied
warranty.
```
