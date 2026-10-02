<!-- SPDX-License-Identifier: BSD-2-Clause -->
# Usage parity with command-line FreeBSD

## 1. Decision (user, 2026-09-29)

**NeoDarwin 1.0 is usable the way command-line FreeBSD is usable.** A FreeBSD administrator sitting at a NeoDarwin console or ssh session finds the programs, flags, configuration files and administration workflows they expect. Where Darwin has its own mechanism (launchd, IOKit, `ndpkg`), NeoDarwin keeps it and adds FreeBSD-named front ends for daily administration, instead of replacing Darwin with FreeBSD.

The bar has four parts, and 1.0 needs all of them:

| Part | What it means | Epics |
|---|---|---|
| Base userland and administration | FreeBSD's base programs are present or have a recorded equivalent; accounts, services, logging and periodic jobs are administered as on FreeBSD | P4-20–P4-23, P4-26 |
| Networking and remote access | DHCP, DNS, NTP, `pf`, and OpenSSH on real hardware | P4-24, P4-25, P3-08 |
| ZFS and boot environments | root on ZFS; upgrades into a new boot environment with rollback, as with `bectl` | P3-01–P3-03, P2-03 |
| Self-hosting | NeoDarwin builds NeoDarwin, the kernel and the ports tree included (self-hosting.md) | P5-10–P5-12 |

Parity is about **usage**, not source. A program counts if it exists under the FreeBSD name, accepts the flags FreeBSD documents, and reads the same files, whether it was built from Apple's source, from FreeBSD's, or written new. The reuse order in `docs/repository.md` §3.1 still decides where each program comes from.

## 2. Measuring it: the parity inventory

`tools/parity` (P4-20) lists every program FreeBSD's base builds in `bin/`, `sbin/`, `usr.bin/` and `usr.sbin/` and records what NeoDarwin has for each. `bazel test //tools/parity:all` keeps the record honest.

**Which FreeBSD.** The inventory pins **15.1-RELEASE**: tag `release/15.1.0`, freebsd-src `96841ea08dcf` (`tools/parity/freebsd.lock`). The files NeoDarwin builds from FreeBSD (msun, libxo, dhclient, rtsold, rtadvd, resolvconf, libcrypt) are pinned at `050683bb8e13` instead. That commit is on `main` (16-CURRENT, 25 September 2026), not on a release branch, and it suits a source drop. Parity is measured against what a FreeBSD administrator actually runs, and that is a release, so the inventory follows the newest RELEASE. Patch releases (`-pN`) don't change the program set. The pin moves when a new RELEASE ships (`bazel run //tools/parity:lock`), and the ratchet then shows every row the bump adds or removes. `../freebsd-src` is used only as a reference.

**What a row is.** The walk (`parity programs`, run by `//tools/parity:programs`) reads the pinned Makefiles with a small bmake reader. It follows `SUBDIR`, `SUBDIR.${MK_*}` and `.if` conditions, `Makefile.<arch>` (as `bsd.arch.inc.mk` includes it) and the `Makefile.inc` chain. It reads `PROG`, `PROGNAME`, `PROGS`, `SCRIPTS`, `LINKS` and `BINDIR`, and evaluates the `MK_*` options from `share/mk`'s defaults for an arm64 build. A row is one program directory: either a top-level entry, or, under an entry that only collects others (`usr.sbin/acpi`, `usr.bin/clang`, `usr.sbin/bluetooth`), each descendant that installs a program. Helpers in a program directory's subdirectories belong to its row (bsdinstall's, routed's `rtquery`). The `tests` directories are FreeBSD's test suite, not programs. Every architecture's entries and every option's entries are included, so the set doesn't depend on the build options. `programs.tsv` records each row's condition (`cond`), whether a default arm64 build includes it (`aarch64`) and its installed names. 15.1-RELEASE has **752 rows: 41 in `bin`, 90 in `sbin`, 340 in `usr.bin` and 281 in `usr.sbin`**. 71 of them aren't built for arm64 by default (the arch-specific ones, `MK_CLANG_EXTRAS`, `MK_OFED_EXTRA` and a few others).

**The inventory** is `tools/parity/inventory.tsv`, one row per program directory:

| Column | Content |
|---|---|
| `path` | the FreeBSD directory, e.g. `usr.bin/grep` |
| `status` | one of the statuses below |
| `source` | where NeoDarwin's program comes from: the Apple project and version (`text_cmds-197`), the FreeBSD pin, the port's origin, or the equivalent and its project |
| `provides` | for an `equivalent`, or a program installed under another name: the image paths that count as this row being built (`usr/sbin/ioreg`). Empty means the row's own name |
| `roadmap` | the backlog item that builds it (P4-21 by default) |
| `built` | `yes` if the image installs it. This is derived from the image, never claimed by hand: `bazel run //tools/parity:update` sets it, and `built_test` fails if it is wrong |
| `note` | a short rationale; required for `n/a` |

| Status | Meaning |
|---|---|
| `apple` | built from Apple's open source (the `*_cmds` projects and others in the macOS 26 release set) |
| `freebsd` | built from FreeBSD's source, where Apple doesn't publish the program |
| `new` | written for NeoDarwin (for example `service`, `sysrc`, `pciconf` over the IOKit registry) |
| `equivalent` | a different program does the job; `source` names it (for example `bectl` → `ndpkg system`, `devinfo` → `ioreg`, `ktrace` → `dtruss`) |
| `port` | not in the base, available from the ports tree (for example `ntpd` → `net/ntp`, sendmail) |
| `n/a` | tied to a FreeBSD subsystem or file system that NeoDarwin doesn't have (§5: jails, bhyve, GEOM, ipfw, UFS, CAM, netgraph, OFED, NIS), or not built for FreeBSD/arm64; `note` gives the reason |
| `todo` | not yet decided |

A program's key name is the installed name that matches its directory (`vi`, not `nvi`), or else its first name. A row counts as built when the image's `bin`, `sbin`, `usr/bin`, `usr/sbin` or `usr/libexec` has that name, or when every path in `provides` exists. The image is `//images:session_root`, read from its file list (`//images:session_root_contents`, which every `hfs_ramdisk` now has).

**Checks** (in `bazel test //...`):

| Test | Fails when |
|---|---|
| `inventory_test` (a), (b) | a program directory has no row, a row isn't a program directory of the pinned release, or rows are duplicated or unsorted; a status is unknown; an `apple`, `freebsd`, `new` or `equivalent` row has no source, or is unbuilt with no roadmap item; an `n/a` row has no reason; a roadmap item isn't in `roadmap/backlog.yaml` |
| `ratchet_test` (c) | against `tools/parity/baseline.tsv`, a row loses its status (back to `todo`), stops being built, or disappears; or the inventory moved ahead and the baseline wasn't updated (`bazel run //tools/parity:accept`). `accept` refuses to record a regression without `-- --regress`, so losing ground is always a deliberate, reviewed change to `baseline.tsv` |
| `built_test` (d) | a row's `built` disagrees with the image |
| `selftest` | the walk or a check gives the wrong answer on `tools/parity/testdata` |

**Coverage.** `bazel build //tools/parity:coverage` writes `coverage.md`: counts by status and directory, how much of the base is built, and the unbuilt rows grouped by roadmap item and source. That last part is P4-21's work list. `ci/parity.sh` runs the checks, builds the report and copies it to `$PARITY_OUT` (and to the job summary on GitHub and Forgejo Actions). The workflow that publishes it as an artifact waits for the CI runners (P0-05). Coverage is every status except `todo`, as a share of all rows. The 1.0 gate is **no `todo` rows**, and every `apple`, `freebsd`, `new` and `equivalent` row built, installed and passing the FreeBSD test suite's tests for that program where they exist (`/usr/tests`, Kyua).

Today (P4-20 seeding, 2026-10-02), on 15.1-RELEASE's 752 rows:

| Status | Rows | Built |
|---|---:|---:|
| `apple` | 250 | 52 |
| `freebsd` | 120 | 5 |
| `new` | 15 | 0 |
| `equivalent` | 44 | 9 |
| `port` | 28 | – |
| `n/a` | 295 | – |
| `todo` | 0 | – |

Coverage is 100% (no `todo`). Of the 429 base rows, 66 (15.4%) are built: 52.6% of `bin`, 33.3% of `sbin`, 5.6% of `usr.bin` and 16.5% of `usr.sbin`. P4-21 holds 277 of the 363 unbuilt base rows. The largest groups are 82 FreeBSD programs (`fetch`, `ee`, `mandoc`, `bmake`, `xz`, `zstd`, `kyua`, `timeout`, `certctl`, `makefs`, ...), shell_cmds (38), text_cmds (31), file_cmds (19), system_cmds (14), adv_cmds (8) and Apple's NFS (7). The toolchain (31 rows, P5-10), accounts (13, P4-22), services (7, P4-23) and the ndpkg-backed equivalents (P2-02 to P2-04) make up the rest.

**Workflow.** Reclassify a row by editing `inventory.tsv`, then run `bazel run //tools/parity:accept`. After a base change installs or removes programs, run `bazel run //tools/parity:update` then `accept`. For a new FreeBSD release, run `bazel run //tools/parity:lock -- CHECKOUT "15.2-RELEASE (tag release/15.2.0)"` on a checkout of the tag (a sparse checkout of `bin sbin usr.bin usr.sbin share/mk` is enough), then `update` (new directories arrive as `todo`), classify them, and `accept`.

## 3. Administration: FreeBSD front ends on Darwin mechanisms

| Task | FreeBSD | NeoDarwin | Notes |
|---|---|---|---|
| Services | `service`, `sysrc`, `/etc/rc.conf`, `rc.d` scripts | `service` and `sysrc` (new, P4-23) over launchd job plists. `/etc/rc.conf` holds overrides such as `sshd_enable="YES"` and `ntpd_flags=…`, which launchd applies to the matching jobs | launchd stays PID 1 (`docs/base/session.md` §1). `rc.d` scripts are not run |
| Packages | `pkg` | `ndpkg` (`packaging.md`) | the verbs match `pkg` where the meaning matches: `install`, `remove`, `upgrade`, `search`, `info`, `verify` |
| Ports | `/usr/ports`, `make install clean`, poudriere | the ports tree with `ndports`, and a `make` shim in each port directory (`ports.md`) | |
| Base upgrade | `freebsd-update`, `bectl` | `ndpkg system upgrade`, `ndpkg system list`, `activate` and `rollback` | every upgrade is a new boot environment |
| Accounts | `pw`, `adduser`, `/etc/master.passwd`, `pwd.db`, `login.conf` | the same files, read by Libinfo's file module; `pw`-compatible commands (P4-22) | there is no OpenDirectory: it is closed |
| Kernel modules | `kldload`, `kldstat`, `/boot/loader.conf` | kext packages; the auxiliary collection is rebuilt and loaded (`packaging.md` §6.4); `kextstat`-style listing | no single-kext loading on ARM64: xnu disables it when booted from a fileset |
| Devices | `devinfo`, `pciconf`, `usbconfig`, `devd` | `ioreg` (Apple's IOKitTools), `pciconf`- and `usbconfig`-compatible listings over the IOKit registry, launchd jobs triggered by IOKit matching | |
| Tuning | `sysctl`, `/etc/sysctl.conf` | `sysctl` and `/etc/sysctl.conf` (applied at boot by a launchd job) | xnu has sysctl |
| Logs | `syslogd`, `newsyslog` | `syslogd`, `newsyslog` | asl stays for Apple's libraries |
| Periodic jobs | `cron`, `periodic` | `cron`, `periodic` under launchd | |
| Firewall | `pf`, `pfctl`, `/etc/pf.conf` | `pf` (in xnu) and `pfctl` (OpenBSD 4.3's, built against xnu's `pfvar.h`, with xnu's `-E`/`-X` references and `scrub-anchor`/`dummynet-anchor`); `/etc/pf.conf` is macOS's, and `com.apple.pfctl` enables pf at boot when it isn't `Disabled` (P4-24, `docs/base/pf-ntp.md`) | FreeBSD's `ipfw` has no counterpart; recorded as `n/a`. No ALTQ: xnu has none |
| Time | `ntpd`, `ntpdate`, `/etc/ntp.conf` | Apple's `sntp` (ntp-139) run by the `com.neodarwin.sntp` job from `/etc/ntp.conf`'s `server` and `pool` lines (P4-24, `docs/base/pf-ntp.md`) | a client only: no `ntpd` (Apple's links closed libraries; FreeBSD's ntpd remains the way to serve time). Off by default, as `ntpd_enable="NO"` |
| Resolver and IPv6 autoconfiguration | `resolvconf` (openresolv) and `/etc/resolvconf.conf`; `rtsold` (RDNSS, DNSSL, `-M`/`-O` scripts); DHCPv6 from ports (`net/dhcp6`'s `dhcp6c`, or `dhcpcd`); `ifconfig_<if>_ipv6` in `/etc/rc.conf` | the same programs: FreeBSD's `resolvconf` and `rtsold`/`rtsol`, and `net/dhcp6`'s `dhcp6c` in the base, run by netconfigd, which takes `rc.conf`'s names from `/etc/netconfigd.conf` (`ifconfig_<if>_ipv6`, and NeoDarwin's `ifconfig_<if>_dhcp6` and `rtsold_flags`) (P4-24, `docs/kernel/network.md`, "IPv6 DNS: RDNSS and DHCPv6") | the merged file is `/var/run/resolv.conf` (`/etc/resolv.conf` links to it, as on macOS); mDNSResponder answers lookups and rereads it; the kernel does SLAAC, as with `accept_rtadv` |
| Tracing | `dtrace` | `dtrace` (xnu's kernel side and Apple's open userland) | |
| Documentation | `man`, `apropos` (mandoc) | mandoc, and every base program's page installed | |

Every administration command that reports state takes `--libxo json` (the FreeBSD convention, and libxo is already in the base), so scripts and agents parse data, not text (overview principle 2).

## 4. The workflow suite (exit test of P4-27)

These tasks are adapted from the FreeBSD Handbook. They run as scripts against a QEMU image in CI, and on the Radxa Dragon Q8B before a release:

1. Install a port as a binary package and from source; upgrade it; remove it.
2. Add a user and a group, set a password, add the user to `wheel`, log in on the console and over ssh, and `su` to root.
3. Configure networking with DHCP, then with a static address from `/etc/rc.conf`. Set the hostname and resolver, and check that time syncs.
4. Enable `sshd` with `sysrc` and `service`, log in with a key, and copy files with `scp` and `sftp`.
5. Load a `pf` ruleset, check with `pfctl -s rules` that a blocked port is blocked, and flush the ruleset.
6. Create a ZFS dataset, snapshot it, change it, roll it back, and `zfs send` it to a file and receive it again.
7. Upgrade the system into a new boot environment, reboot into it, roll back, and reboot into the old one.
8. Schedule a `cron` job and see it run; see `newsyslog` rotate a log; run `periodic daily`.
9. Mount a USB mass-storage device with a FAT filesystem, copy a file to it, and unmount it.
10. Rebuild the running system from source on the machine and install it as a new boot environment (the self-hosting exit, P5-12).

## 5. Not in scope for parity

| FreeBSD subsystem | Why not | Status in the inventory |
|---|---|---|
| jails | xnu has no equivalent isolation primitive. A container story is a separate design, not a 1.0 item | `n/a` |
| bhyve | Apple's Hypervisor.framework is closed; a hypervisor is a separate design | `n/a` |
| Linux binary compatibility (`linuxulator`) | out of NeoDarwin's scope | `n/a` |
| GEOM (`gpart`, `geli`, `gmirror`) | storage is IOStorageFamily. Partitioning gets its own tool (§6); encryption and mirroring are ZFS's job | `equivalent` or `n/a` per program |
| `ipfw`, `ipf` | `pf` is the firewall | `n/a` |
| Capsicum | xnu has no capability mode. Downstreams may build capability models on TrustedBSD MAC (`downstream.md`) | `n/a` |

## 6. Open questions

| Question | Options | Decide by |
|---|---|---|
| Partitioning tool | port FreeBSD's `gpt`-era tool onto IOStorageFamily, or write a GPT-only `ndpart` | P3-03, which needs a partitioning step in the installer |
| DHCP client | Apple's open `bootp` and `IPConfiguration` source if it builds without closed dependencies, otherwise FreeBSD's `dhclient`, otherwise `dhcpcd` (BSD-2). IPv6 **decided (P4-24)**: IPConfiguration's DHCPv6 and RDNSS code is bound to its service threads, CoreFoundation and SystemConfiguration's private interfaces, so FreeBSD's `rtsold` (RDNSS, DNSSL) and `resolvconf`, and `net/dhcp6`'s `dhcp6c`, which leaves SLAAC to the kernel as `dhcpcd` wouldn't (`docs/kernel/network.md`, "IPv6 DNS: RDNSS and DHCPv6") | P4-24 |
| NTP | **decided (P4-24)**: Apple's open `ntp` drop, ntp-139, for its `sntp`; its `ntpd` needs closed libraries (`docs/base/pf-ntp.md`). FreeBSD's `ntpd` if NeoDarwin is to serve time | P4-24 |
| Default interactive shell | `sh` (FreeBSD's default for users), `tcsh` (FreeBSD's root shell until 14), or `zsh` (Darwin's) | P4-21 |
