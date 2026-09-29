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

`tools/parity` (P4-20) walks `bin/`, `sbin/`, `usr.bin/` and `usr.sbin/` of a pinned FreeBSD release (the stable 15.x line; `../freebsd-src` is 16-CURRENT and is used only as a reference). It writes `docs/base/parity.tsv` with one row per program. At the time of writing there are 633 program directories: 42 in `bin`, 85 in `sbin`, 274 in `usr.bin` and 232 in `usr.sbin`.

| Status | Meaning |
|---|---|
| `apple` | built from Apple's open source (the `*_cmds` projects and others) |
| `freebsd` | built from FreeBSD's source, where Apple doesn't publish the program |
| `new` | written for NeoDarwin (for example `launchctl`, `service`, `sysrc`) |
| `equivalent` | a different program does the job; the row names it and the differences (for example `bectl` → `ndpkg system`) |
| `port` | not in the base, available from the ports tree |
| `n/a` | tied to a FreeBSD subsystem that NeoDarwin does not have (§5), with the reason |
| `todo` | not yet decided |

CI publishes the coverage (every status except `todo`, as a share of all rows) and fails if a row that had a status loses it. The 1.0 gate is **no `todo` rows**, and every `apple`, `freebsd` and `new` row built, installed and passing the FreeBSD test suite's tests for that program where they exist (`/usr/tests`, Kyua).

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
| Firewall | `pf`, `pfctl`, `/etc/pf.conf` | `pf` (in xnu) and `pfctl` | FreeBSD's `ipfw` has no counterpart; recorded as `n/a` |
| Tracing | `dtrace` | `dtrace` (xnu's kernel side and Apple's open userland) | |
| Documentation | `man`, `apropos` (mandoc) | mandoc, and every base program's page installed | |

Every administration command that reports state takes `--libxo json` (the FreeBSD convention, and libxo is already in the base), so scripts and agents parse data, not text (overview principle 2).

## 4. The workflow suite (exit test of P4-27)

These tasks are adapted from the FreeBSD Handbook. They run as scripts against a QEMU image in CI, and on the CD8180 before a release:

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
| DHCP client | Apple's open `bootp` and `IPConfiguration` source if it builds without closed dependencies, otherwise FreeBSD's `dhclient`, otherwise `dhcpcd` (BSD-2) | P4-24 |
| NTP | Apple's open `ntp` drop, or FreeBSD's `ntpd` | P4-24 |
| Default interactive shell | `sh` (FreeBSD's default for users), `tcsh` (FreeBSD's root shell until 14), or `zsh` (Darwin's) | P4-21 |
