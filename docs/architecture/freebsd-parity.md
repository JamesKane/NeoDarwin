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

**Which FreeBSD.** The inventory pins **15.1-RELEASE**: tag `release/15.1.0`, freebsd-src `96841ea08dcf` (`tools/parity/freebsd.lock`). The files NeoDarwin builds from FreeBSD (msun, libxo, dhclient, rtsold, rtadvd, resolvconf, libcrypt, the leaf utilities of `base/freebsd_cmds`) are pinned at `050683bb8e13` instead. That commit is on `main` (16-CURRENT, 25 September 2026), not on a release branch, and it suits a source drop. Parity is measured against what a FreeBSD administrator actually runs, and that is a release, so the inventory follows the newest RELEASE. Patch releases (`-pN`) don't change the program set. The pin moves when a new RELEASE ships (`bazel run //tools/parity:lock`), and the ratchet then shows every row the bump adds or removes. `../freebsd-src` is used only as a reference.

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

Today (2026-10-05, after P4-21 checkpoint 4's leaf utilities), on 15.1-RELEASE's 752 rows:

| Status | Rows | Built |
|---|---:|---:|
| `apple` | 245 | 193 |
| `freebsd` | 107 | 38 |
| `new` | 16 | 0 |
| `equivalent` | 44 | 10 |
| `port` | 45 | – |
| `n/a` | 295 | – |
| `todo` | 0 | – |

Coverage is 100% (no `todo`). Of the 412 base rows, 241 (58.5%) are built: 92.1% of `bin`, 46.2% of `sbin`, 71.4% of `usr.bin` and 28.0% of `usr.sbin`. P4-21 holds 54 of the 171 unbuilt base rows (80 before checkpoint 4, 81 before vis, 96 before checkpoint 3, 107 before checkpoint 2, 193 before checkpoint 1; 241 of 327 until 2026-10-04, when NFS, tracing, quotas, printing and `at` moved to their own items and the obsolete network programs became ports; §2.1). The largest group is 31 FreeBSD programs (`fetch`, `bmake`, `kyua`, `certctl`, `makefs`, ...); the rest are cp6's equivalents and new programs, and `iostat` and `top` (IOKit). The toolchain (31 rows, P5-10), NFS (15, P4-28), accounts (13, P4-22), services (8, P4-23), printing (7, P4-31), quotas (5, P4-30), tracing (3, P4-29) and the ndpkg-backed equivalents (P2-02 to P2-04) make up the rest.

**Workflow.** Reclassify a row by editing `inventory.tsv`, then run `bazel run //tools/parity:accept`. After a base change installs or removes programs, run `bazel run //tools/parity:update` then `accept`. For a new FreeBSD release, run `bazel run //tools/parity:lock -- CHECKOUT "15.2-RELEASE (tag release/15.2.0)"` on a checkout of the tag (a sparse checkout of `bin sbin usr.bin usr.sbin share/mk` is enough), then `update` (new directories arrive as `todo`), classify them, and `accept`.

### 2.1 P4-21's checkpoints

P4-21 is planned as seven checkpoints, cheapest first. Each one is about one agent's work (around 300K tokens). It ends with `bazel run //tools/parity:update` and `accept`, and with the new programs smoke-tested on QEMU. Two items in the backlog title are already met: `/bin/sh` is shell_cmds' `sh` linked with libedit, and `ed` is part of text_cmds.

| cp | Contents | Rows | Notes |
|---|---|---:|---|
| 1 (done, but `iostat`) | **The Apple projects already in `base/`.** text_cmds (25, including `md5` and `ed`), shell_cmds (25), patch_cmds (3), file_cmds (8; `gzip` is in cp2), adv_cmds (7), system_cmds' leaf tools (`dmesg`, `reboot`, `shutdown`, `iostat`, `pagesize`, `gcore`, `zic`, `zdump`, `ac`, `accton`, `sa`), misc_cmds (5), basic_cmds (2), and remote_cmds' `logger` and `wall` | ~85 | Each one extends an existing `build.sh`. It can be split into 1a (text, shell, patch) and 1b (the rest) |
| 2 (done) | **Compression and archives.** libz (moving Apple's zlib-100.120.1 out of `kexts/zfs` into the base), libbz2, liblzma and libzstd; `gzip`, `bzip2`, `xz`, `xzdec`, `lzmainfo`, `zstd`; libarchive's `tar`, `cpio` and `bsdcat`; `unzip` | ~14 | Turns grep's decompression back on (dropping text_cmds patch 0001) and fixes the ZFS suite's `bzcat` failures |
| 3 (done, but `top`) | **Interactive essentials.** `less` (3), vis as `vi` (§6), `man` with `mandoc` and `soelim`, `file`, `bc`, `top`, `nc`, `csh` (tcsh), `mail`, `iconv` (3), the ncurses tools, `locale` and `localedef` with locale data | ~20 | `zsh` is the default interactive shell (§6) |
| 4 (leaf rows done; §2.1's cp4 notes) | **FreeBSD leaf utilities** on the FreeBSD source drop at `050683bb8e13` (as for `timeout` and libxo): `nmtree`, `nproc`, `uuidgen`, `pwait`, `m4`, `ident`, `ministat`, `xo`, `perror`, `getaddrinfo`, `ts`, `daemon`, `fsync`, `lock`, `asa`, `ee`, `bsdiff`, `bspatch`, `resizewin`, `domainname`, ... | ~30 | Sets up the shared build pattern for FreeBSD programs. The obsolete network programs are ports, not base (§6) |
| 5 | **Heavier FreeBSD programs.** `bmake`, `dtc`, `mkimg`, `makefs`, `iasl` and `acpidb` (from `third_party/acpica`), `fetch` and `certctl` (with OpenSSL), `drill`, `tzsetup`, and the IPv6 and multicast tools (`route6d`, `rtadvctl`, `ip6addrctl`, `mld6query`, `rrenumd`, `mtest`, ...) | ~25 | `diskinfo`, `trim` and `recoverdisk` use GEOM and CAM ioctls and need rewriting over IOKit |
| 6 | **`equivalent` and `new` rows.** Equivalents: `lsof`, `vm_stat`, `ioreg`, `xattr`, `pppd`. New programs: `pciconf` and `acpidump` over the IOKit registry, `mdconfig`, `nvmecontrol`, `efivar` and `efibootmgr` | ~18 | `efivar` and `efibootmgr` need a kernel interface to UEFI runtime services |
| 7 | **Tests.** `kyua` and ATF, and FreeBSD's `/usr/tests` for every built row, run on QEMU. This is P4-21's exit | – | Needs Lua and SQLite. It may become its own item |

**cp1 progress (2026-10-04).** Part 1a is done: text_cmds' 25 rows (with `md5` and its `sha*` names, `ed`, `bintrans` and its `base64`/`uuencode` names), shell_cmds' 25 (with `locate`'s helpers and updatedb scripts, `alias` and its builtin names, `w` and `uptime`, `chroot`), and patch_cmds' `patch`, `diff3` and `sdiff`. Two libraries macOS ships but Apple doesn't publish came in from FreeBSD at `050683bb8e13`, as libxo did: `base/libmd` (`/usr/lib/libmd.dylib`, MD5 and SHA-1/2 with their End/File/Data helpers, for `md5` and `install`) and `base/libsbuf` (`/usr/lib/libsbuf.dylib`, FreeBSD's sbuf under macOS's `usbuf_` names, for `apply` and `w`). Of part 1b, file_cmds (`chflags`, `pax`, `mknod`, `ipcrm`, `ipcs`, `pathchk`, `install`) and adv_cmds (`finger`, `gencat`, `last`, `lsvfs`, `whois`, `locale`, `localedef`) are built. `locale` and `localedef` are built without locale data: `/usr/share/locale` stays with cp3. `//kernel:sbsa_base_commands_test` (manual, qemu) runs a sample on `session_root`.

Part 1b is done too (2026-10-04): system_cmds' `dmesg`, `reboot` (with `halt`), `shutdown`, `pagesize`, `gcore`, `zic`, `zdump`, `ac`, `accton` and `sa`; misc_cmds-45's `calendar`, `leave`, `ncal` (with `cal`), `tsort` and `units` (with `/usr/share/misc/units.lib`); basic_cmds-70's `mesg` and `write`; and remote_cmds-306's `logger` and `wall`. The three projects are new pins (`base/misc_cmds`, `base/basic_cmds`, `base/remote_cmds`). The smoke test runs `cal`, `tsort`, `units`, `ncal -e`, `calendar`, `pagesize`, `dmesg`, `zdump`, `zic`, `shutdown -s`, `mesg`, `write`, `logger` and `wall`. Patches:
- system_cmds 0005 and 0006: NeoDarwin has no kextd, so `reboot` and `shutdown` skip the kext manager's reboot lock (`kextmanager.defs`, MIG), as the embedded platforms' builds do; they call launchd-842's `reboot2()` where newer launchd has `reboot3()`. `shutdown -s` (sleep, through IOKit's IOPMLib) fails at once with "sleep is not supported". Neither needs IOKit or CoreFoundation now. Neither has been run on QEMU, since it would end the test.
- system_cmds 0007: `gcore` drops its `os_log_set_hook()` hook. The libsystem_trace stand-in has no hooks and already writes every message to standard error; it gains `_os_log_impl()`, which plain `os_log()` calls. `compat/responsibility.h` stands in for libquarantine's responsibility SPI: each process is its own responsible process. gcore is built with C11 for its `static_assert`. It doesn't dump yet: `dyld_process_create_for_task()` fails with `0xeb000005` (the row's `note`).
- misc_cmds 0001: `leave.c` includes `<sys/types.h>` for `u_int`, which macOS 26's headers no longer reach through its other includes.

`cal` and `ncal` warned `setlocale: Bad file descriptor` until the locale data arrived (cp3). The image rule sets modes but not groups, so `write` isn't setgid `tty` as on macOS: it reaches only terminals open to everyone.

`lsvfs` doesn't hang on QEMU, it is slow: it asks for every type number below `vfs.generic.maxtypenum`. The kernel's mockfs (MOCKFS, kernel patch 0011) registers the FourCC type number `'mock'` (0x6D6F636B), so the maximum is 1,836,016,492, and after hfs `lsvfs` makes that many `sysctl` calls, each failing with ENOTSUP. macOS release kernels don't build mockfs. Kernel patch 0044 (2026-10-05) gives mockfs type number 24 on SBSA, and the smoke test now runs `lsvfs`.

`nmtree` is `freebsd` (user, 2026-10-04), built in cp4: Apple's `mtree` needs CoreFoundation, CommonCrypto and APFS's private `<apfs/apfs_fsctl.h>`.

*Left for cp1:* `iostat`. It reads drive statistics from the IOKit registry (IOBlockStorageDriver) with IOKit.framework and CoreFoundation. NeoDarwin's base has neither (the sysroot's IOKit.framework is xnu's headers alone), so it waits for an IOKit userland (IOKitUser and CoreFoundation, with cp6's `ioreg`). Its row stays `apple`, unbuilt, with a `note`.

cp1, cp2 and cp4 touch separate directories, so they can run in parallel. They still share one Bazel server.

**cp2 (done, 2026-10-05).** 11 rows: `gzip`, `bzip2`, `bzip2recover`, `xz`, `xzdec`, `lzmainfo`, `zstd`, `tar`, `cpio`, `bsdcat` and `unzip`, with four libraries in `/usr/lib` (headers build-only in each tree's `usr/local/include`, as for the base's other libraries) and an exports test each:
- `base/zlib`: `libz.1.dylib` from Apple's zlib-100.120.1 (zlib 1.2.12; the release set lists zlib-100, and 100.120.1 is its later update), with Apple's arm64 adler32 and crc32 assembly and optimised `inflate_fast` (`VEC_OPTIMIZE`, `INFFAST_OPT`). The published sources don't include AddOn's `zopt_defs.h`, so the C files force-include it. Its 81 exports are the SDK's `libz.tbd` less `zSetAllocInfo`, which isn't in the published source. The ZFS userland links it now; its private static copy (`//kexts/zfs:zlib`, `zlib.sh`) is gone.
- `base/bzip2`: `libbz2.1.0.dylib` (bzip2-47; exports match `libbz2.tbd`), `bzip2` with `bunzip2` and `bzcat`, `bzip2recover`, `bzdiff`/`bzcmp`, `bzmore`/`bzless`.
- `base/xz`: `liblzma.5.dylib` and the commands from XZ Utils 5.4.7, the upstream release pinned by hash. macOS ships liblzma without publishing it; its `liblzma.tbd` exports are 5.4's, and the 107 built match them exactly. `xz` with FreeBSD's names (`unxz`, `lzma`, `unlzma`, `xzcat`, `lzcat`), `xzdec` with `lzdec`, `lzmainfo`. The three rows stay `freebsd` (FreeBSD's `usr.bin/xz` builds contrib/xz 5.8.4), with source `xz-5.4.7 (upstream)`. `config.h` is committed (a configure run, CoreFoundation answers removed).
- `base/zstd`: `libzstd.1.dylib` and `zstd` (`unzstd`, `zstdcat`, `zstdmt`) from Zstandard 1.5.7's release tarball, the version FreeBSD's `sys/contrib/zstd` has at `050683bb8e13`, built as FreeBSD's `lib/libzstd` and `usr.bin/zstd` Makefiles do. macOS has no zstd, so the exports list (208) is the ratchet alone. libarchive doesn't link it, as on macOS.
- `base/libarchive`: `libarchive.2.dylib` (libarchive-158, 3.7.4), `bsdtar` with the `tar` link, `cpio`, and `bsdcat` (built as FreeBSD's `usr.bin/bsdcat`; Apple's project has no cat target). `compat/nd_libarchive_config.h` wraps Apple's `config.h`: no libiconv (cp3), no libxml2 (the xar format reports itself unsupported), digests from libmd instead of CommonCrypto (no SHA-384). `compat/nd_archive_check_entitlement.c` replaces the Security/CoreFoundation entitlement check with its answer for a process without the entitlements (every format allowed). Exports are `libarchive.tbd`'s less the two quarantine calls.
- `base/zip`: `unzip` (Info-ZIP UnZip 6.0 from Apple's zip-29) with `zipinfo`, `funzip`, `unzipsfx` and `zipgrep`. The row stays `apple`; FreeBSD's `unzip` is libarchive's `bsdunzip`, which libarchive-158 also carries if the row is ever reclassified.
- `gzip` (file_cmds-475) with `gunzip`, `gzcat`, `zcat`, `zcmp`, `zless` and the `gzexe`, `zdiff`, `zforce`, `zmore`, `znew` scripts, linking libz, libbz2 and liblzma.
- grep links libz, libbz2 and liblzma (grep.xcconfig): text_cmds patch 0001 (`GREP_NO_DECOMPRESSION`) is dropped, and grep_variant_links.sh's `zgrep`, `zegrep`, `zfgrep`, `bzgrep`, `bzegrep`, `bzfgrep` are installed.

Patches: libarchive 0001 (no libquarantine: `HAVE_MAC_QUARANTINE` off, as on Apple's embedded platforms; no CommonCrypto for the zip/7-Zip cryptor and HMAC, so encrypted entries are refused), libarchive 0002 (the xar stub, never compiled on Apple's side, calls the entitlement check by its declared name), zip 0001 (unzip without libquarantine; `mkdir_qtn()` returns 0 after a successful `mkdir()` instead of falling off its end). `//kernel:sbsa_base_commands_test` adds gzip, bzip2, xz, xzdec and zstd round trips, `lzmainfo`, `tar cf`/`tf`/`czf`/`xzf`, `bsdcat`, `cpio -o`/`-it`/`-id`, `zgrep`, `bzgrep`, `grep --xz`, and `unzip -l`/`-p` on a zip made on the host. The ZFS suite's three `bzcat` tests (`zpool_import_013_neg`, `zpool_import_errata3`, `zpool_import_errata4`) should now pass; `kexts/zfs/tests/expected.tsv` still lists them as failures until the suite is rerun.

**cp3 (2026-10-05).** 15 rows, and the locale data. Part 3a is done but `top`; part 3b, the locale data and `vi`, is done (vis, below). Eight new Apple pins, each checked against macOS 26's release.json (`base/less`, `base/man`, `base/file`, `base/bc`, `base/netcat`, `base/libiconv`, `base/tcsh`, `base/mail_cmds`), and two FreeBSD file pins at `050683bb8e13` (`base/mandoc`, `base/locales`):
- less-50: `less` (with `more`, a hard link, as Apple's script phase makes it), `lessecho`, `lesskey`.
- man-62: Apple's `man` is FreeBSD's `man.sh`; installed with `manpath`, `apropos` and `whatis`, and `/etc/man.conf`. It formats with `/usr/bin/mandoc` and pages through `/usr/bin/less -sR`.
- `mandoc` (with `makewhatis`) and `soelim` from FreeBSD (contrib/mandoc and usr.bin/soelim, as usr.bin/mandoc's Makefile builds them; macOS's mandoc is unpublished). `config.h` is committed (a configure run): NeoDarwin's Libc has no `reallocarray`, `recallocarray` or ohash, so mandoc's compat files supply them. `apropos` and `whatis` stay man-62's, as on macOS.
- file-104 (file 5.41) with `/usr/share/file/magic.mgc`, compiled by a `file` built for the build machine (the project's `build_magichost.sh`).
- bc-35: Gavin Howard's bc, which is FreeBSD's `gh-bc`, so the row stays `apple`; `dc` is a hard link.
- netcat-56: `nc`. Its target links libnetwork (closed) for `copyconninfo()` and `freeconninfo()` alone (`-v`'s report after `connectx`); `base/netcat/compat/network/conninfo.h` implements them over xnu's `SIOCGCONNINFO`.
- libiconv-113: `libiconv.2.dylib` (95 exports, exactly the SDK's `libiconv.2.tbd`) reexporting `libcharset.1.dylib`, the 25 Citrus modules in `/usr/lib/i18n`, the prebuilt csmapper and esdb tables in `/usr/share/i18n` (44 MB), `iconv`, and `mkcsmapper` and `mkesdb` in `/usr/bin` (Apple installs those two in `/usr/local/bin`, which the base root drops; FreeBSD's are in `/usr/bin`). Nothing links libiconv yet (zsh, libarchive and tcsh were built without it).
- ncurses-79 (already pinned): `clear`, `infocmp`, `tic` (with `captoinfo`, `infotocap`), `toe`, `tput`, `tset` (with `reset`), and `tabs`, which has no Xcode target and is built the same way.
- tcsh-75: tcsh 6.21 as `/bin/tcsh` and `/bin/csh` (a hard link) with `/etc/csh.*`; configure cross-compiles with a `config.cache`, as zsh's does. `zsh` stays the default shell (§6).
- mail_cmds-41: `mail` and `mailx`. Sending needs a `sendmail`, which the base doesn't have.
- **Locale data** (`base/locales`): macOS 26's release set publishes no locale data (`/usr/share/locale` is unpublished; adv_cmds has `localedef` but no sources), so it is compiled from FreeBSD's CLDR-derived sources (share/ctypedef, colldef_unicode, timedef and the *def_unicode directories, with tools/tools/locale's `map.UTF-8` and `widths.txt`) as FreeBSD's Makefiles do, by adv_cmds-237's `localedef`, built for the build machine from the base's own pinned source. Apple's localedef writes Apple's formats (`RuneMagB` LC_CTYPE, "DARWIN 1" LC_COLLATE). The layout is macOS's: `C.UTF-8` has only LC_CTYPE (Libc gives every other category of a `C.` locale the C one); `en_US.UTF-8` has LC_COLLATE, LC_TIME, LC_NUMERIC, LC_MONETARY and LC_MESSAGES/LC_MESSAGES, and LC_CTYPE links to C.UTF-8's; `UTF-8/LC_CTYPE` too. `locale -a` lists them, and `cal` no longer warns "setlocale: Bad file descriptor" (the smoke test fails if it does).
- Man pages are installed for the cp3 projects (less, man, mandoc, file, bc, nc, iconv, tcsh, mail); `man less` works. tcsh installs its own message catalogs in `/usr/share/locale/*/LC_MESSAGES`.

`//kernel:sbsa_base_commands_test` adds `bc`, `dc`, `file`, `less -F`, `lessecho`, `man -w`, `man` through mandoc and less, `soelim`, `tput` and `infocmp`, `iconv`, `nc -z` to an `nc -l` on loopback, `tcsh -c` and `csh -c`, `mail -f`, `locale -a`, `locale charmap` in en_US.UTF-8 and C.UTF-8, and `cal` in en_US.UTF-8.

*Left for cp3:*
- `top`: its libtop reads disk statistics with IOKitLib and keeps its tables in CoreFoundation dictionaries, and it links libpanel. It waits for an IOKit userland with `iostat` (cp6). (`systat`, its equivalent, waits with it.)

**vi is vis (2026-10-05, §6).** `base/vis` builds vis 0.9 (github.com/martanne/vis, ISC) from its release tag's tarball, with libtermkey 0.22, Lua 5.4.9 and LPeg 1.1.0 from their release tarballs, all four pinned by sha256 in `MODULE.bazel` and `base/upstream.lock`:
- The build is vis's Makefile with the `config.mk` its configure would write, spelled out in `build.sh` (configure's checks link test programs): curses (the base's libncurses), Lua, LPeg built in (`CONFIG_LPEG`), no TRE, no ACL or SELinux. Lua, LPeg and libtermkey are linked into the binary statically; nothing else in the base uses Lua, so there is no shared liblua. Lua is built with `LUA_COMPAT_5_3` (vis-lua.c uses its integer casts) and `LUA_USE_POSIX` without `LUA_USE_DLOPEN` (no C modules). TRE isn't built: `text-regex.c` uses Libc's `regcomp`/`regexec`; TRE would only add matching across the gaps of vis's piece table.
- The editor is installed as `/usr/bin/vi` only. `/usr/bin/vis` is BSD's `vis(1)` (text_cmds, FreeBSD's `usr.bin/vis`), which keeps its name; vis never runs itself by name. vis's page is installed as `vi.1`, titled `vi`. `vis-menu`, `vis-digraph`, `vis-clipboard`, `vis-complete` and `vis-open` are installed with their pages, and vis's Lua runtime (`lua/` less `lua/doc`: `vis.lua`, `visrc.lua`, the plugins, lexers and themes) in `/usr/share/vis`, the compiled-in `VIS_PATH`.
- vis has no ex mode and no read-only mode, so there is no `ex` and no `view`. The `usr.bin/vi` row is `equivalent`: vis isn't POSIX vi (sam-style structural regular expressions), so FreeBSD's vi tests don't apply.
- The smoke test runs `vi -v` (`vis 0.9 +curses +lua +lpeg`) and edits a file with no one at the keyboard, on the console's terminal: `vi "+x/old/ c/new/" F +w F +qall F` (each `+` command runs after the next file opens; the second and third windows share the first's text, and `q` isn't allowed in a command group). A `visrc.lua` in `XDG_CONFIG_HOME`, which requires `/usr/share/vis/vis.lua`, checks that LPeg and the C lexer load.

**Man pages for every project (2026-10-05).** `base_library`'s `man_pages` (rules/base.bzl) runs the build script through `tools/base/pages.sh`, which then installs the source tree's `NAME.N` page for every program the tree installs in `bin`, `sbin`, `usr/bin`, `usr/sbin` and `usr/libexec`, unless the script installed one: the page in a directory named after the program wins, then the shortest path, outside `tests` directories. A program with no page of its own that is a link to, or an identical copy of, one with a page gets a copy of that page under its own name (gunzip, sha256, unxz, egrep, pgrep, the shell builtins); `page_aliases` names the rest (`cpio=bsdcpio`, `cal=ncal`, `unexpand=expand`). It is on for the 46 base libraries that install programs (not the ones in `//base:root`, whose change would rebuild everything). text_cmds makes `sort.1` from `sort.1.in` as FreeBSD's Makefile does; ncurses installs its commands' pages and `terminfo.5` itself, with `fix_man.sed` and the substitutions `edit_man.sh` would make. `stage_root.sh` builds `/usr/share/man/whatis`, which man-62's `apropos` and `whatis` grep: the root's own mandoc `makewhatis` indexes the pages and its `apropos` lists every entry, both run on the build machine (they link only libSystem and libz). It indexes a tree where a section's identical pages are hard links to one file, so a page copied under several names is one entry with all of them, as FreeBSD's MLINKS are; `mandoc.db` isn't kept. The image has 341 pages (5.1 MB) and 267 whatis entries. Programs still without a page: those built from FreeBSD file pins that don't pin the page (`timeout`, `dhclient`, `dhclient-script`, `pfctl`, `resolvconf`, `rtsol`, `rtsold`, `rtadvd`, `dhcp6c` and its scripts), `openssl` (its pages are POD), `bzip2recover`, the locate helpers, and `sshd-auth`/`sshd-session`. The smoke test runs `man -w ls`, `man ls`, `apropos gzip` and `whatis ls`.

**cp4 (2026-10-05).** 26 rows from FreeBSD at `050683bb8e13`, in one project, `base/freebsd_cmds` (one `freebsd.lock` of 130 files, `@freebsd_cmds`, `//base:freebsd_commands`), built as each program's Makefile builds it: one `prog` line in `build.sh` per `PROG` with its `SRCS`, `CFLAGS` and `LIBADD`, installed at FreeBSD's `BINDIR`, with its page through `man_pages`. Built: `nproc`, `uuidgen`, `pwait`, `domainname` (`/bin`); `asa`, `bsdiff`, `bspatch`, `ee` (with `ree` and `edit`), `etdump`, `fsync`, `getaddrinfo`, `ident`, `lock` (setuid root, `images/BUILD.bazel`), `m4`, `ministat`, `perror`, `resizewin`, `revoke`, `ts`, `xo` (`/usr/bin`); `daemon`, `fifolog_create`, `fifolog_reader`, `fifolog_writer`, `wake`, and nmtree as `mtree` with the `nmtree` link (`/usr/sbin`, with `mtree.5`). None of them clashes with a program already in the image; macOS's own `uuidgen`, `m4` and `mtree` are absent from it (unpublished, an Xcode tool, and CoreFoundation-bound).

The shared pattern, so that a FreeBSD program usually costs one line:
- `compat/nd_freebsd.h`, force-included: FreeBSD's `<sys/cdefs.h>` names (`__unreachable`, `__DECONST`, `__packed`, `__va_list`, `__uintptr_t`, `nitems`, ...), `timespecadd`/`timespecsub`, `<sys/time.h>` (which FreeBSD's `<sys/stat.h>` reaches), and declarations of what `compat/nd_freebsd.c` supplies: `reallocarray`, `memrchr`, `setproctitle` (a no-op: the command line stays as `ps` shows it), `cpuset_getaffinity` (every active CPU: xnu has no user affinity masks) and `setusercontext` (no login classes: group, groups, login name and user only). Every program links `nd_freebsd.c`; `-dead_strip` keeps what it calls.
- `compat/` headers FreeBSD has and Darwin lacks: `<sys/capsicum.h>` and `<capsicum_helpers.h>` (`cap_*` and `caph_*` succeed and limit nothing, as in a `WITHOUT_CAPSICUM` build; §5), `<libcasper.h>` and `<casper/cap_fileargs.h>` (`WITHOUT_CASPER`'s fallbacks), `<sys/cpuset.h>`, `<sys/endian.h>`, `<sys/stdint.h>`, `<sys/consio.h>` (vt ioctl numbers; a terminal answers ENOTTY, so `lock -v` fails as on a FreeBSD pty), `<sys/sbuf.h>` (libsbuf's, under macOS's `usbuf_` names), `<login_cap.h>`, FreeBSD's `<uuid.h>` and `<sys/uuid.h>` (DCE `struct uuid`; `nd_uuid.c` implements `uuidgen()` and `uuid_to_string()` over `uuid_generate_time(3)`), and `include_next` wrappers adding `kqueuex()` to `<sys/event.h>` and `fparseln()` and `pidfile_fileno()` to `<libutil.h>`.
- FreeBSD's internal libraries are compiled into the programs that `LIBADD` them, from the pin: libopenbsd's ohash (m4), libnetbsd (getaddrinfo, nmtree), libfifolog with `getdate.y`, libutil's `fparseln.c`, and contrib/libc-pwcache (nmtree's `uid_from_user()` and `pwcache_userdb()`; `compat/nd_pwcache.h` renames its `user_from_uid()` and `group_from_gid()`, whose types differ from Libc's). `<sys/tree.h>` comes from the pin. m4's `parser.y` and `tokenizer.l` and fifolog's `getdate.y` go through the build machine's yacc and lex.

Patches (`base/freebsd_cmds/patches`): 0001, `pwait -v` asks xnu for the exit status (`NOTE_EXITSTATUS`, which xnu gives only to a process allowed to signal the target; otherwise it says "terminated"); 0002, fifolog sizes a disk with xnu's `DKIOCGETBLOCKSIZE` and `DKIOCGETBLOCKCOUNT`; 0003, `daemon` drops `madvise(MADV_PROTECT)` (xnu's equivalent is the jetsam priority launchd sets). nmtree is built with `NO_RMD160`: libmd, as macOS's, has no RIPEMD-160, so `rmd160digest` is refused. `ee`'s message catalogs aren't installed (its built-in English strings are the C locale's). `wake`, `revoke`, `resizewin`, `lock`, `ee` and `etdump` are built but not exercised by the smoke test (they need BPF and a network peer, a terminal, or an ISO 9660 image).

`//kernel:sbsa_base_commands_test` adds `nproc` and `nproc --all`, `uuidgen` and `uuidgen -r`, `m4` with `eval`, `perror`, `ident`, `ministat`, `xo --text`, `getaddrinfo localhost`, `daemon -f -p` with `pwait -v` on its child, an `nmtree -c` spec verified and then a change caught, `bsdiff` and `bspatch`, `fifolog_create`/`_writer`/`_reader`, `fsync`, `ts`, `domainname`, and `man -w m4`. The ISA audit covers 394 files with no new findings.

*Left for cp4* (resume here): `getfacl` and `setfacl` (over Darwin's `ACL_TYPE_EXTENDED` acl(3), only if they work honestly; the rows' notes), `setaudit` (OpenBSM is in the base), `manctl` (a shell script), `uefisign` (OpenSSL is in the base), and `usr.sbin/mtest` (a multicast test tool, in no checkpoint yet). Each should be one `prog` line in `base/freebsd_cmds/build.sh` plus its files in `freebsd.lock` (hashes from `git show 050683bb8e13:PATH | shasum -a 256`; `../freebsd-src` has local changes, so `tools/pinned/lock.sh` can't re-pin from it).

**Split out of P4-21 (2026-10-04).** These rows need kernel work or daemons, not just command ports, so they belong to their own items:
- NFS: 15 rows, including `rpcbind`, `rpcinfo`, `autofs` and `gssd` (P4-28).
- `ktrace`, `kdump` and `truss` → `dtruss`, which needs DTrace (P4-29).
- Quotas (P4-30).
- Printing: lpr and `lpd` → CUPS (P4-31).
- `at`, which goes with cron's `atrun` (P4-23).

**Became ports (2026-10-04).** 17 obsolete network programs (§6). `ftp` is `ftp/tnftp` and `tftp` is `ftp/tftp-hpa`. FreeBSD has no port for the others (`rwho`, `rwhod`, `rusers`, `rup`, `ruptime`, `rwall`, `bootparamd`, `callbootd`, `enigma`, `msgs`, `biff`, `from`, `tip`, `talk`, `telnet`), so they are one NeoDarwin recipe, `net/freebsd-legacy`, built from FreeBSD's and remote_cmds' source once the ports tree exists (P2-05).

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
| Partitioning tool | **decided (user, 2026-10-04): `gpart`**, a NeoDarwin subset of FreeBSD's (`create`, `add`, `delete`, `show`, `resize`, `bootcode`) over IOStorageFamily, so administrators type the command they know. Built in P3-11 checkpoint 3 for the ZFS tests' partitioning; the installer (P3-03, P7-03) uses it | P3-11 |
| DHCP client | Apple's open `bootp` and `IPConfiguration` source if it builds without closed dependencies, otherwise FreeBSD's `dhclient`, otherwise `dhcpcd` (BSD-2). IPv6 **decided (P4-24)**: IPConfiguration's DHCPv6 and RDNSS code is bound to its service threads, CoreFoundation and SystemConfiguration's private interfaces, so FreeBSD's `rtsold` (RDNSS, DNSSL) and `resolvconf`, and `net/dhcp6`'s `dhcp6c`, which leaves SLAAC to the kernel as `dhcpcd` wouldn't (`docs/kernel/network.md`, "IPv6 DNS: RDNSS and DHCPv6") | P4-24 |
| NTP | **decided (P4-24)**: Apple's open `ntp` drop, ntp-139, for its `sntp`; its `ntpd` needs closed libraries (`docs/base/pf-ntp.md`). FreeBSD's `ntpd` if NeoDarwin is to serve time | P4-24 |
| Default interactive shell | **decided (user, 2026-10-04): `zsh`**, Darwin's default. Root's shell is already `/bin/zsh` (`base/etc/master.passwd`); P4-22's `pw` and `adduser` default new users to it. `/bin/sh` stays shell_cmds' `sh` for scripts, and `tcsh` is built as `csh` (checkpoint 3) | P4-21 checkpoint 3 |
| vi: vis, not nvi or vim | **decided (user, 2026-10-05): vis** (Marc André Tanner's, ISC) as `/usr/bin/vi`, in place of vim-163 (macOS's) or nvi (FreeBSD's): a small, modern modal editor with sam's structural regular expressions, Lua configuration and LPeg syntax highlighting. It isn't POSIX vi (no ex mode, no `ex` or `view`), so the `usr.bin/vi` row is `equivalent` and FreeBSD's vi tests don't apply. `/usr/bin/vis` stays BSD's `vis(1)`. vim and nvi can be ports (§2.1) | P4-21 checkpoint 3 |
| Obsolete network programs | **decided (user, 2026-10-04): ports.** FreeBSD 15.1 still ships `rwho`, `rusers`, `talk`, `telnet`, `ftp` and others, but nobody administers a system with them; they are `port` rows (§2.1) | P4-21 checkpoint 4 |
