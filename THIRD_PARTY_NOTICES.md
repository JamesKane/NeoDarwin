<!-- SPDX-License-Identifier: BSD-2-Clause -->
# Third-party notices

NeoDarwin's own code is under the BSD 2-Clause licence (`LICENSE`). NeoDarwin also builds third-party code, pinned by version and hash in `MODULE.bazel` and the `upstream.lock` files (`base/upstream.lock`, `kernel/upstream.lock`, `third_party/*/upstream.lock`) and never vendored into this repository. Each component stays under its own licence. Changes to upstream files are the numbered patches beside each component, under the component's licence. Binaries NeoDarwin distributes carry the notices below.

## Components

| Component | Pin | Licence | Where NeoDarwin uses it |
|---|---|---|---|
| xnu | xnu-12377.1.9 | APSL-2.0 (BSD-derived parts keep their BSD notices) | the kernel |
| hfs | hfs-704.0.3.0.2 | APSL-2.0 | HFS+, built into the kernel |
| libpthread `kern/` | libpthread-539 | APSL-2.0 | built into the kernel |
| IOPCIFamily | IOPCIFamily-726.0.5 | APSL-2.0 (the archive's `APPLE_LICENSE`; some file headers name APSL 1.1, whose versions clause lets it be used under any later version) | PCI enumeration, built into the kernel (`kernel/neodarwin/pci`) |
| IOStorageFamily | IOStorageFamily-331 | APSL-2.0 | block storage, IOMedia, partition schemes and `/dev/disk` nodes, built into the kernel (`kernel/neodarwin/storage`) |
| AppleFileSystemDriver | AppleFileSystemDriver-31 | APSL-2.0 | the root by boot-uuid, built into the kernel (`kernel/neodarwin/storage`) |
| ACPICA | 20260408 | Intel dual licence; **used under BSD-3-Clause** (notice below) | ACPI in the kernel (`kernel/neodarwin/acpi`) |
| FreeBSD crypto and `msun`, `libxo` | FreeBSD `freebsd-src` at the commits in the locks | BSD-2-Clause / BSD-3-Clause, per file | the kernel's crypto provider, `libsystem_m`, `libxo` |
| dyld, Libc, libplatform, libpthread, libmalloc, Libinfo, libclosure, Libnotify, syslog, copyfile, removefile, Libsystem, objc4, cctools (libmacho), AvailabilityVersions | per `base/upstream.lock` | APSL-2.0 (BSD-derived parts keep their BSD notices) | the userland base (libSystem and the libraries under it) |
| libdispatch | libdispatch-1542.0.4 | Apache-2.0 | `libdispatch.dylib` |
| mDNSResponder (client library) | mDNSResponder-2881.0.25 | Apache-2.0 | `libsystem_dnssd` |
| launchd | launchd-842.92.1 | Apache-2.0 | `/sbin/launchd`, liblaunch in `libxpc` |
| LLVM runtimes (libc++, libc++abi, libunwind, compiler-rt builtins) | swiftlang/llvm-project swift-6.2-RELEASE | Apache-2.0 WITH LLVM-exception | the C++ runtime and `libcompiler_rt` |
| system_cmds, shell_cmds, file_cmds, text_cmds, adv_cmds, libutil, files | per `base/upstream.lock` | APSL-2.0 and BSD (most commands are BSD-derived; each file keeps its notice) | commands and `/etc` |
| ncurses | ncurses-79 (ncurses 6.0) | MIT/X11 | `libncurses` and the terminfo database |
| zsh | zsh-110.1.1 (zsh 5.9) | the zsh licence (MIT-style) | `/bin/zsh` |
| Swift Embedded standard library | swift.org toolchain 6.3.2 | Apache-2.0 WITH Swift runtime library exception | linked into neoboot and NeoDarwin's Swift programs |
| TF-A, EDK2 | TF-A v2.15.0, edk2-stable202608 | BSD-3-Clause, BSD-2-Clause-Patent | test firmware for QEMU only (`third_party/qemu_firmware`); not part of NeoDarwin images |

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
