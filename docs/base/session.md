<!-- SPDX-License-Identifier: BSD-2-Clause -->
# An interactive session over serial (P1-08)

**Goal.** launchd-842 is PID 1 on the HFS+ root. It starts getty on the console from a LaunchDaemon plist, and getty, login and a shell give an interactive session over serial. Everything is built from Apple's source, on the userland base of P1-08b (`libsystem.md`), except the parts Apple doesn't publish.

## 1. Decisions

- **PID 1 is launchd-842** (user, 2026-09-29). It is the last open-source launchd, ported to xnu-12377 with its libxpc-300 XPC-domain code compiled out; that code's only clients are closed. Its client library (liblaunch: `bootstrap_*`, `vproc_*`, `launch_msg`) goes into NeoDarwin's libxpc stand-in. That way asl, notify, Libc and libdyld talk to a real bootstrap server, over launchd-842's own MIG interface (`job.defs`).
- **launchctl is a small first-party Swift tool** (user, 2026-09-29). launchctl-842 parses job plists with CoreFoundation, including private CF headers, while launchd itself needs no CF. NeoDarwin's launchctl parses XML plists itself, builds `launch_data` and calls `launch_msg`, as launchctl does for `bootstrap` and `load`. No CoreFoundation or ICU enters the base.
- **/bin/sh is FreeBSD ash from shell_cmds.** Apple's `/bin/sh` (dash-16) isn't published, and shell_cmds builds ash as `/usr/local/bin/ash`, linked with libedit. zsh (root's shell) and bash 3.2 came after P1-08, on ncurses (§3).
- **The console is `/dev/console`.** xnu's serial keyboard thread polls the PL011 every 16 ms when `serial=` includes input (bit 0x2; neoboot passes `serial=3`), and feeds it to the console tty. That tty has the standard line discipline, a controlling terminal and job control. The harness types into it (`tools/efi/qemu_efi_test.sh --send-after`).

## 2. Checkpoints

| # | Deliverable | Check | Status |
|---|---|---|---|
| 1 | Commands and `/etc` from Apple source; `//base:system_root`; the image's macOS layout; a test-only PID 1 that runs getty | `//kernel:sbsa_getty_boot_test`: log in as root and run commands | done |
| 2 | liblaunch in the libxpc stand-in | NeoDarwin's libraries' bootstrap and vproc imports resolve to launchd-842's client code | done |
| 3 | launchd-842 as PID 1 | it boots and serves `job.defs` | done |
| 4 | The Swift launchctl; `com.apple.getty.plist` loaded by `launchctl bootstrap` | `//kernel:sbsa_session_boot_test`: launchd as PID 1, login over serial, `launchctl list` | done |

## 3. Findings

### Checkpoint 1: the commands, /etc and a shell

| Project | Tag | Built |
|---|---|---|
| system_cmds | 1039 | `getty`, `login` |
| shell_cmds | 326 | `sh` (as `/bin/sh`), `echo`, `test`/`[`, `pwd`, `kill`, `sleep`, `date`, `hostname`, `env`, `id`, `groups`, `whoami`, `printf`, `uname` |
| file_cmds | 475 | `ls`, `cp`, `mv`, `rm`, `mkdir`, `ln`, `chmod` |
| text_cmds | 197 | `cat`, `head`, `wc`, `sed` |
| adv_cmds | 237 | `ps`, `stty`, `tty` |
| libutil | 73 | `libutil.dylib` (21 of Apple's 21 exports) |
| libxo | FreeBSD `contrib/libxo` 1.6.0 | `libxo.dylib` (127 of Apple's 128) |
| files | 968 | `/etc`: `gettytab`, `ttys`, `shells` and the rest, unmodified |

Commands link against the runtime root with `-syslibroot` (`base/commands.sh`), as the P1-08b hello world does. `//base:system_root` adds them and `/etc` to `//base:root`. `//images:shell_root` gives the image macOS's layout:
- `/etc`, `/tmp` and `/var` are links into `/private`;
- `/var/root` and `/var/run` are directories;
- `master.passwd` is 0600, `/tmp` 1777, and `login` setuid.

`tests/qemu/getty_init` stood in for launchd until checkpoint 3, and still boots as `//images:getty_root`, a check of the session that doesn't depend on launchd. It's a test-only Embedded Swift PID 1 that runs `getty std.9600 console` and restarts it whenever it exits, as `com.apple.getty.plist` does under launchd.

| Finding | Resolution |
|---|---|
| Apple's `/bin/sh` is closed (dash-16); shell_cmds' `sh` is FreeBSD ash with libedit, installed as `/usr/local/bin/ash` | ash installed as `/bin/sh`, with `mknodes` and `mksyntax` built for the host. At first it was built without libedit (`-DNO_HISTORY`); since libedit-65 it is built as `sh.xcconfig` builds it (§3, the shells) |
| login builds with PAM and BSM audit. PAM's macOS configuration needs `pam_opendirectory` (OpenDirectory and CoreFoundation, both closed) | at first login's own non-PAM path: `crypt()` against `pw_passwd`. Since OpenPAM (§3, PAM) it builds with `USE_PAM`, and since OpenBSM (§3, BSM audit) with `USE_BSM_AUDIT` |
| ls builds only with `COLORLS` (libcurses): `unix2003_compat` is declared inside the `COLORLS` block | file_cmds patch 0001 moves the declaration; `ls -G` is accepted and ignored |
| wc and others link libxo, which Apple doesn't publish | FreeBSD's `contrib/libxo`, at the commit libm uses, with Apple's install name. Patch 0001 configures it for Darwin |
| libutil's `tzlink` calls tzlinkd over XPC | libutil patch 0001 takes the simulator's `ENOTSUP` path |
| The sysroot's private `<sys/ioctl.h>` reaches `<sys/param.h>`, whose `BSD` macro breaks stty | command projects search `usr/local/include` and `usr/include` only, as a project outside libSystem does against Apple's internal SDK |
| Libinfo's file module reads `/etc/master.passwd` for root and `/etc/passwd` otherwise; there's no `pwd.db` | NeoDarwin writes `master.passwd` and `group` (`base/etc`); `passwd` is generated from them. root has an empty password, so login asks for none |
| launchd-842 doesn't read `/etc/ttys`; login only checks its console entry's `secure` flag | `ttys` is installed unmodified; getty runs from `com.apple.getty.plist` (system_cmds), installed enabled |

Still to check on the kernel: `ps` (`KERN_PROC`, `proc_pidinfo`, `task_read_for_pid`) and `/usr/bin/false` (the shell of `daemon` and `nobody`), which isn't built yet.

### Checkpoints 2 and 3: launchd-842 and liblaunch

`//base:launchd_daemon` builds `/sbin/launchd` from launchd-842.92.1, and `//base:libxpc` builds `/usr/lib/system/libxpc.dylib` from 842's liblaunch (`liblaunch.c`, `libvproc.c`, `libbootstrap.c`) plus the stand-in's XPC objects. Apple has shipped liblaunch inside libxpc since 10.8. So `launch_msg`, `bootstrap_*` and `vproc_*` now reach a real launchd over `job.defs`, for every library that calls them. The sysroot stages 842's private headers (`bootstrap_priv.h`, `vproc_priv.h`, `launch_priv.h`, `reboot2.h`) in place of base/sdk's shims.

| Finding | Resolution |
|---|---|
| base/sdk's `bootstrap_priv.h` claimed 842's flag values but had `BOOTSTRAP_PRIVILEGED_SERVER` wrong (1<<1 is 842's `ALLOW_LOOKUP`) | 842's own headers are staged and the shims deleted |
| libSystem-1356 calls `_libxpc_initializer`, not liblaunch's `bootstrap_init`, and libsystem_kernel never sets `bootstrap_port` | the libxpc stand-in's initializer and child fork hook call `bootstrap_init()` |
| launchd receives through libxpc-300's `xpc_pipe_try_receive`, falling back to `xpc_domain_server`; the XPC domain `.defs` and `<xpc/launchd.h>` aren't published | launchd patch 0002: a `mach_msg_server_once()` loop with the audit trailer, and core.c's XPC domain, event and process code compiled out (its only clients, xpcproxy and xpcd, are closed). LaunchEvents are logged and not imported |
| The SDK's `<sandbox.h>` turns `HAVE_SANDBOX` on; libauditd, quarantine and systemstats are closed | patch 0001 lets the build set every `HAVE_*` switch, all 0 |
| libbsm is linked only for `audit_token_to_au32` | `base/launchd/src/nd_audit_token.c` reads xnu's token layout |
| 842 knows only 32-bit arm; `TASK_SEATBELT_PORT` is gone; current clang rejects `__typeof__` on bit-fields | patches 0003 and 0004 |
| launchd as PID 1 calls `setaudit_addr` and takes children's task ports | nothing to change: `config/MASTER.arm64.MacOSX` has `config_audit`, no MAC policy makes control ports immovable, and developer mode is on under xnu's code-signing monitor. A future AMFI/MAC policy must keep both working |

### Checkpoint 4: launchctl and the session

`base/launchctl` is NeoDarwin's launchctl: Embedded Swift with a Foundation-free XML plist parser (`Plist.swift`). It builds `launch_data` and speaks `launch_msg`. It implements `bootstrap -S System`, `load [-w]`, `unload`, `start`, `stop` and `list`. On the host it parses 424 of macOS's 425 LaunchDaemon plists (as XML); the other is a properties file with no Label. `//images:session_root` puts it at `/bin/launchctl`, with launchd at `/sbin/launchd`. `//kernel:sbsa_session_boot_test` boots it:
- launchd-842 starts as PID 1 and runs `launchctl bootstrap -S System`, which loads `com.apple.getty.plist`;
- launchd spawns getty on the console;
- the test logs in as root, runs commands, and `launchctl list` shows getty as a launchd job.

| Finding | Resolution |
|---|---|
| The kernel mounts the root read-only. PID 1 launchd creates its socket under `/var/tmp/launchd` only when launchctl first asks for it, and `EROFS` there fails silently, so `launch_msg` answered `ENOTCONN` | `launchctl bootstrap` remounts `/` read-write first, as launchctl-842 does with `mount -uw /` (since diskdev_cmds, with fsck first, as 842 does: below). The image has `/private/var/tmp` (1777), as macOS does |
| The kernel names the root's device `root_device` (`vfs_rootmountalloc`), which no path reaches, and an update mount looks its device path up | at first launchctl found the `/dev` block device whose device number is the root's and called mount(2) itself; since diskdev_cmds it runs `mount -uw /`, which finds it the same way through Libinfo (below) |
| macOS's `<launch.h>` marks launch_msg deprecated, and it's part of the SDK's Darwin module, so the marking can't be undone for one import | `launch_shim.h` declares the calls as 842's own `launch.h` does |
| Embedded Swift has no `CommandLine`; String comparison needs the Unicode tables | a `@_cdecl("main")` entry; `rules/darwin_executable.bzl` links the Embedded stdlib's `libswiftUnicodeDataTables.a`, which dead-stripping trims to what's used |

Not in NeoDarwin's launchctl yet: binary plists, the overrides database (`load -w` doesn't persist), `/etc/rc.*` and `sysctl.conf`. Loopback setup and job sockets are under "Loopback and sshd", below.

### After P1-08: zsh and ncurses

Root's login shell is zsh 5.9, as on macOS, built from zsh-110.1.1 on ncurses-79 (ncurses 6.0, the 5.4 ABI):
- **libncurses** exports Apple's 942 symbols. It installs as `/usr/lib/libncurses.5.4.dylib`, with the names `libncurses`, `libncurses.5`, `libcurses` and `libtermcap` linked to it.
- **The terminfo database** has all 2,684 entries, compiled by a `tic` built from the same sources. It is byte-identical to macOS's.
- **zsh** loads its 36 modules as bundles under `/usr/lib/zsh/5.9`, as on macOS (all but `pcre`), and has the 1,203 autoloaded functions. `/etc/zshrc` and `/etc/zprofile` are Apple's.
- **The test:** `//kernel:sbsa_session_boot_test` logs in to zsh's prompt and runs commands. It checks `$ZSH_VERSION`, and loads `zsh/datetime`, which `dlopen`s its bundle through dyld.

| Finding | Resolution |
|---|---|
| Apple's `run_tic.sh` compiles terminfo with the build machine's `/usr/bin/tic` | the build uses the `tic_static` it builds from these sources |
| ncurses' generated sources run the internal SDK's `cc` | replayed with `clang -E` over NeoDarwin's headers. Apple's build doesn't run configure either: its committed `ncurses_cfg.h` is used as is |
| zsh's configure runs about 20 test programs, which would describe the host's newer libSystem | configure runs in cross-compiling mode, as Apple's embedded builds do, with `base/zsh/config.cache` (Apple's `configure.cache-embedded` plus pinned answers). Compile and link checks use NeoDarwin's sysroot and root |
| The base has no PCRE or libiconv | `--enable-pcre` is dropped and iconv is pinned absent; zsh uses its own UTF-8 code |
| A Bazel target named like its source directory hides that directory's files from tests' runfiles | targets `libncurses_dylib` and `zsh_shell`, as `libutil_dylib` |
| zle writes a character, backs up (`\b`) and redraws the line, which the harness's log showed doubled | `qemu_efi_test.sh` applies backspaces when it cleans the log, and types half a second after its prompt appears, as a person would |

Not yet: locale data (zprofile sets `LANG=C.UTF-8`; zsh falls back to the C locale) and `/usr/bin/locale` (zshrc skips it). PAM, mount and fsck are below, and `/usr/libexec/path_helper`, which zprofile runs, under "passwd and chpass".

### After P1-08: the shells

NeoDarwin has macOS's three shells:

| Shell | From | Line editing |
|---|---|---|
| `/bin/sh` | shell_cmds-326's FreeBSD ash, built as `sh.xcconfig` builds `/usr/local/bin/ash` | libedit: `set -o emacs` or `set -o vi`, history, `fc`; it reads `$ENV` |
| `/bin/zsh` | zsh-110.1.1 (zsh 5.9), root's login shell | zle |
| `/bin/bash` | bash-140 (bash 3.2.57, as macOS ships it) | its own readline, linked statically; termcap from libncurses |

- **libedit** (libedit-65, NetBSD libedit 20121213-3.0) installs as `/usr/lib/libedit.3.dylib`, with `install_misc.sh`'s names `libedit.2`, `libedit.3.0`, `libedit` and `libreadline` linked to it, as the SDK's `.tbd` files name it. It exports the SDK `libedit.3.tbd`'s 150 symbols, all of them, and links libncurses. Its headers (`histedit.h`, `editline/readline.h` and the `readline/` links) are build-only, as ncurses' are.
- **ash** links libedit as Apple's does. `mkbuiltins` now runs without `-h`, so `fc` is a builtin. Editing starts once the shell is interactive on a terminal and `set -o emacs` or `set -o vi` is given (FreeBSD's sh of 2017 enables neither by default).
- **bash** is built with the project's committed configuration: `config.h`, `pathnames.h`, `signames.h`, `syntax.c`, `version.h` and the generated `builtins/*.c`. Apple's build doesn't run configure, and neither does NeoDarwin's: it uses them as ncurses' `ncurses_cfg.h` is used. Only what the project generates is generated: `ostype.h` (`darwin25`, xnu-12377's release) and `parse.y` through the toolchain's yacc. It links its four static libraries (readline, glob, libsh, intl) and libncurses. `/etc/profile` and `/etc/bashrc` are Apple's, from bash-140's RC files phase. `profile` runs `/usr/libexec/path_helper` if it is executable, as zsh's `zprofile` does ("passwd and chpass", below).
- **The test:** `//kernel:sbsa_shells_boot_test` logs in to zsh. It starts `/bin/sh` with `$ENV` and edits a line with Backspace in emacs mode. It recalls and edits a line with the Up arrow, lists the history with `fc -l` and edits a line in vi mode. Then it runs `bash --version`, and `bash -l`, whose prompt is `/etc/bashrc`'s. There it uses arrays and recalls a line with readline. Each expected line can only appear if the editing worked: the typed text doesn't contain it. `qemu_efi_test.sh`'s `--send-after` now types `\b` (DEL, the tty's erase character), `\e` and the arrow keys (`{up}`) on serial.

| Finding | Resolution |
|---|---|
| Apple's bash commits `config.h`, but its `conftypes.h` takes `HOSTTYPE` from compiler macros under `MACOSX` and knows no arm64 (`__arm__` only), so it needs a `CONF_HOSTTYPE` | `-DCONF_HOSTTYPE='"arm64"'`: `$MACHTYPE` is `arm64-apple-darwin25`, as on macOS 26 |
| bash's `ostype.h` script phase writes the build machine's `uname -r` major | written for xnu-12377's release 25, not the build host's |
| The project's `USER_HEADER_SEARCH_PATHS` is `$(SRCROOT)/**` | the directories its sources include from are listed, SRCROOT first; `lib/termcap` (not built) is left out, so `<termcap.h>` is libncurses' |
| bash's GCC_PREPROCESSOR_DEFINITIONS carry quoted strings (`LIBDIR='"/usr/libdata"'`) | written to the response file in the same shell quoting, which clang's response files read |
| bash-140 prints "The default interactive shell is now zsh" when `/bin/zsh` exists (Apple's `shell.c` change) | kept, as on macOS; `BASH_SILENCE_DEPRECATION_WARNING=1` silences it |
| The finale script links `/usr/local/bin/bash` to `/bin/bash` | not installed: `usr/local` isn't part of the root |
| libedit's `unexports` list also hides the apple-generic version symbols | not generated |

### ksh

`/bin/ksh` is **ksh93u+m 1.0.10** (`github.com/ksh93/ksh`, EPL-2.0; `//base:ksh_shell`, `base/ksh/build.sh`), for the OpenZFS test suite, whose scripts start `#!/bin/ksh -p` (P3-01, `docs/architecture/filesystems.md` §7). It ships only in the ZFS test image for now, not in `//base:system_root`. FreeBSD's base has no ksh either; its ports' `shells/ksh` is this one.

- **Why not Apple's drop.** Apple's `ksh-42` is ksh93u+ 2012-08-01 (EPL-1.0). Its AST build fails under Xcode 27's clang: libast's `va_list` probe gives arm64 the wrong `va_listval` (`hashalloc.c`, `tokscan.c`), libdll's `dlopen` probe fails, implicit declarations and int conversions are errors, and its `main.c` includes `<sys/codesign.h>`. ksh93u+m is the maintained continuation of the same AT&T code, with those fixed; it is the `ksh93` FreeBSD's runs of the ZFS test suite use.
- **The build** is the project's own `bin/package make` (mamake and iffe): libast, libcmd, libdll and libsum, linked statically into `ksh`, which links only libSystem. iffe has no cross-compiling mode: many feature tests run the program they built. So, unlike zsh's configure (cross-compiling, answers from `config.cache`), every probe is compiled against NeoDarwin's sysroot and linked against the runtime root, and then run on the build machine, where `/usr/lib/libSystem.B.dylib` is macOS's: a probe sees only what NeoDarwin declares and exports, and its run-time answers (type sizes, signal numbers, stdio internals) come from the same Libc and xnu lineage. `$CC` is a wrapper with those flags; links are ad hoc signed, as `link_tool`'s. About two minutes.
- Nothing else is installed: no `fun/` files, man page or tests. ksh's builtins (libcmd's) are compiled in.

### After P1-08: the ZFS test suite's commands

The OpenZFS test suite (P3-01, `docs/architecture/filesystems.md` §7) and its `libtest.shlib` call about forty commands the first session lacked. They are in `//base:system_root`, each built as its Xcode target builds it, installed where macOS has it:

| Project | Tag | Added |
|---|---|---|
| text_cmds | 197 | `cut`, `sort`, `uniq`, `tr`, `tail`, `grep` (with `egrep`, `fgrep`) |
| shell_cmds | 326 | `basename`, `dirname`, `expr` (`/bin`), `find`, `xargs`, `mktemp`, `seq`, `which`, `true`, `false`, `tee`, `script`, `hexdump` and `od` |
| file_cmds | 475 | `dd`, `rmdir` (`/bin`), `du`, `touch`, `stat` and `readlink`, `truncate`, `cksum` and `sum`, `mkfifo`, `chown` (`/usr/sbin`) and `chgrp`, `compress` and `uncompress` |
| adv_cmds | 237 | `pkill` and `pgrep` |
| system_cmds | 1039 | `sync` (`/bin`), `getconf` |
| awk | 40 | `awk` (`//base:awk_command`) |
| patch_cmds | 72 | `cmp`, `diff` (`//base:patch_commands`) |
| FreeBSD `bin/timeout` | freebsd-src `050683bb8e13` | `/bin/timeout` (`//base:timeout_command`; macOS 26 has none) |

| Problem | Resolution |
|---|---|
| sort `-R` hashes with CommonCrypto's `CC_SHA256_*` (libcommonCrypto, closed, not reexported) | `base/text_cmds/compat/nd_cc_sha256.c`, SHA-256 per FIPS 180-4, linked into sort |
| pkill reads the process table through libsysmon (closed; asks sysmond over XPC) | `base/adv_cmds/compat`: the `sysmon.h` and `xpc/xpc.h` subset pkill uses, answered from `sysctl(3)` (`KERN_PROC_ALL`, `KERN_PROCARGS2`) by `nd_sysmon.c`; pkill.c is unmodified |
| FreeBSD's timeout uses `procctl(2)`'s reaper to signal and wait for the command's descendants | timeout patch 0001: the command runs in its own process group, which `killpg(2)` signals; `pipe2` and `str2sig` are local |
| getconf's tables are gperf sources turned into C by `fake-gperf.awk` | the build machine's awk runs it, as Xcode's script phase does |

grep links the base's libbz2, liblzma and libz for `-Z`, `-J`, `--xz` and `--lzma`, with the `z*` and `bz*` variants installed. The compression and archive programs (`gzip`, `bzip2`, `xz`, `zstd`, `tar`, `cpio`, `bsdcat`, `unzip`) and their libraries are P4-21's second checkpoint (`docs/architecture/freebsd-parity.md` §2.1).

Not built: `strings` (cctools), `bc` and `jq`.

### After P1-08: mount and fsck

`/sbin/mount`, `/sbin/umount` and `/sbin/fsck` come from diskdev_cmds-751, and HFS+'s `mount_hfs`, `newfs_hfs` and `fsck_hfs` from hfs-704.0.3.0.2, the kernel's HFS pin (`//base:diskdev_commands`, `//base:hfs_commands`, both in `//base:system_root`). The HFS tools install where macOS has them, in `/System/Library/Filesystems/hfs.fs/Contents/Resources`, with `/sbin/mount_hfs`, `/sbin/fsck_hfs` and `/sbin/newfs_hfs` linked to them. There is no `/etc/fstab`, as on macOS.

**launchctl's bootstrap** now does what launchctl-842's `do_potential_fsck()` does. If `/` is mounted read-only, it runs `/sbin/fsck -q` (skipped when `kern.safeboot` is set), then `/sbin/fsck -fy` if that fails. If both fail it halts, as 842 does on macOS. Then it runs `/sbin/mount -uw /`. It starts each tool with `posix_spawn` and waits for it, and logs 842's messages ("Running fsck on the boot volume..."). launchctl no longer calls mount(2) itself, so it no longer needs HFS's mount arguments.

**The root's device.** The kernel still names the root `root_device`. This needs no kernel change, because mount does what it does on macOS. `fsck` and `mount -uw /` get the root's device from `getfsent(3)`. With no `/etc/fstab`, Libinfo makes up an entry for `/`: the `/dev` block device whose number is the root's (`devname_r`, then a scan of `/dev`), with type `hfs` and pass number 1. So fsck checks `/dev/rdisk0s2` (`/dev/rmd0` on a ramdisk root), and `mount -uw /` runs `mount_hfs -o update /dev/disk0s2 /`. After that update mount the kernel records the name it was given, so `mount` and `df` show `/dev/disk0s2 on / (hfs, local, journaled)`.

**Tests:**
- `//kernel:sbsa_disk_boot_test` (and the NVMe one) check launchctl's fsck line on both boots. The second boot comes after QEMU was stopped with the volume mounted: HFS replays the journal when the kernel mounts the root, and `fsck -q` finds the volume clean (`--absent 'Running safe fsck'`). The disk test also runs `mount`.
- `//kernel:sbsa_disk_mount_boot_test` adds a blank 64 MiB virtio-blk disk. It checks the root in `mount`'s output, then runs `newfs_hfs -J -v Scratch` on the blank disk and `mount -t hfs` (mount runs `/sbin/mount_hfs`). It writes a file and runs `umount`, then `fsck_hfs -fn` on the raw disk ("The volume Scratch appears to be OK."). It mounts the disk again, reads the file back and unmounts it. The root isn't checked while it is mounted read-write: `fsck_hfs` refuses that unless it can freeze the volume (`-l`).

| Finding | Resolution |
|---|---|
| The targets link CoreFoundation, IOKit and APFS.framework, and mount also links FSKit, LiveFS and UserManagementLayout, all of them closed. On macOS nearly all of that code is in iOS-only blocks (the EDT fstab, tmpfs ramdisks, media keys). | diskdev_cmds patch 0001: mount.c includes APFS's headers only under the condition of the code that uses them. Nothing links beyond libSystem. |
| `fskit_support.m` (mount) and `fsck_hfs`'s progress reports take their FSKit path when `<FSKit/FSKit.h>` exists. The public SDK has FSKit, but the path also needs FSKit's private headers. | diskdev_cmds patch 0002 and hfs patch 0001 test for the private header instead. mount's `invoke_tool_from_fskit()` returns `ENOTSUP`, upstream's own fallback, so mount runs `mount_<type>` from `/sbin` |
| mount_hfs uses IOKit to ask whether the device is a sparse DiskImages2 image or writable optical media. | hfs patch 0002, under `HFS_NO_FRAMEWORKS`, NeoDarwin's switch (without it upstream is unchanged): both checks compile out, and `optical.c` is left out, as on iOS |
| newfs_hfs needs IOKit for an external journal device's UUID (`-J` with `-D`), and CoreFoundation's `_CFStringGetFileSystemRepresentation` for the canonical decomposition of the volume name. | hfs patch 0003: an external journal device fails, as when IOKit finds no UUID. An ASCII name, which is its own canonical form, is used as given; any other name gets newfs_hfs's existing fallback, "untitled", with its warning |
| fsck_hfs finds an external journal's device by its IOMedia UUID through IOKit. | hfs patch 0004: `OpenDeviceByUUID()` finds none. Journals inside the volume, newfs_hfs's default, are unaffected |
| `fsck_hfs -n` on a journaled volume checks nothing ("Volume is journaled. No checking performed."). | the test uses `-fn`, which forces the check |
| macOS's `/sbin/*_hfs` links are absolute. `stage_root.sh` copies whatever an absolute link names, which here would be the build machine's own `/System/Library/Filesystems/hfs.fs`. | the links are relative (`../System/...`), as macOS's apfs links are |
| With a second disk the root can be `disk1s2`: the two virtio-blk disks register in either order. | the mount test doesn't depend on the root's number. Its scratch disk is the whole disk with no `s1` |

Not built: `quotacheck`, `fstyp`, `fdisk`, `vsdbutil`, `mount_devfs` (the kernel mounts devfs), `hfs.util` (Disk Arbitration's helper), and fsck's FSKit modules.

### After P1-08: PAM

`login` and `su` authenticate through OpenPAM, as on macOS. OpenPAM-35 and pam_modules-217.0.1 are the macOS 26.0 release set's.

| Piece | From | Installed |
|---|---|---|
| libpam | OpenPAM-35 (`//base:libpam_dylib`) | `/usr/lib/libpam.2.dylib`, with `libpam.dylib` linked to it. It exports the SDK `libpam.2.tbd`'s 56 symbols, all of them. `/usr/lib/libpam.1.dylib` is Apple's shim for old binaries (`-allowable_client !`), with `libpam.1.tbd`'s 28. The public headers are build-only, in `usr/local/include/security`, as ncurses' are |
| OpenPAM's modules | OpenPAM-35 | `pam_deny` and `pam_permit`, as Apple builds them, and `pam_unix`, which OpenPAM ships and Apple doesn't build |
| pam_modules | pam_modules-217.0.1 (`//base:pam_modules`) | the eight that need only libSystem and libpam: `pam_env`, `pam_group`, `pam_launchd`, `pam_nologin`, `pam_rootok`, `pam_sacl`, `pam_self` and `pam_uwtmp` |
| Policies | each project's own (`/etc/pam.d`) | `other` (OpenPAM: deny everything), `login` and `login.term` (system_cmds), `su` (shell_cmds) |
| login | system_cmds-1039 | built with `USE_PAM` (and, since BSM audit, `USE_BSM_AUDIT`: below) |
| su | shell_cmds-326 (`//base:su_command`) | `/usr/bin/su`, setuid root |

Modules are `MH_DYLIB`s named `pam_NAME.so.2` in `/usr/lib/pam`. A policy names `pam_NAME.so`, and OpenPAM's loader tries the `.so.2` first. Not built, for their closed dependencies: `pam_opendirectory`, `pam_krb5`, `pam_ntlm` and `pam_mount` (OpenDirectory, CoreFoundation, Heimdal, GSS, NetFS), `pam_smartcard` (CryptoTokenKit), `pam_localauthentication`, `pam_tid` and `pam_aks` (LocalAuthentication, AppleKeyStore), and `pam_basesystem` (CoreFoundation).

**The password module is OpenPAM's `pam_unix`**, in place of macOS's `pam_opendirectory`. It compares `crypt(3)` of the typed password with `pw_passwd` from `getpwnam(3)`. login and su run as root, so Libinfo's file module answers from `/etc/master.passwd`. libc's `crypt` takes SHA-512, SHA-256, bcrypt, MD5 and DES hashes ("passwd and chpass", Hashes). `pam_unix` can't change a password (its `chauthtok` refuses), and its account check accepts everyone: the `change` and `expire` fields aren't enforced.

**Policies.** Each project's pam.d file is patched where it names a closed module. OpenPAM fails a whole policy when one module doesn't load, even an `optional` one.

| Policy | Apple's | NeoDarwin's |
|---|---|---|
| `login` | `pam_krb5`, `pam_ntlm`, `pam_mount` (optional) and `pam_opendirectory` (auth); `pam_nologin` and `pam_opendirectory` (account); `pam_opendirectory` (password); `pam_launchd`, `pam_uwtmp`, `pam_mount` (session) | `pam_unix nullok`; `pam_nologin`, `pam_unix`; `pam_unix`; `pam_uwtmp`, optional (system_cmds patch 0002) |
| `login.term` (`login -f`) | `pam_nologin`, `pam_opendirectory`; `pam_uwtmp` | `pam_nologin`, `pam_unix`; `pam_uwtmp`, optional |
| `su` | `pam_rootok` (sufficient), `pam_opendirectory`; `pam_group group=admin,wheel ruser root_only`, `pam_opendirectory`; `pam_opendirectory`; `pam_launchd` | `pam_rootok`, `pam_unix nullok`; `pam_group` (unchanged), `pam_unix`; `pam_unix`; `pam_permit` (shell_cmds patch 0001) |

`pam_launchd` is built but in no policy. It moves the session into its user's per-user launchd, and NeoDarwin's launchd runs none (below).

**Empty passwords.** root has an empty password (`base/etc/master.passwd`), and logs in on the console without one, as before PAM:
- `pam_unix`'s `nullok` (OpenPAM patch 0001, FreeBSD's option) lets an account with an empty password authenticate without a prompt. Without `nullok` it can't authenticate at all.
- login refuses root on a terminal that `/etc/ttys` doesn't mark `secure`. Only the console is. Apple's login makes that check on its non-PAM path only; system_cmds patch 0001 keeps it with PAM.
- su takes root's empty password too, but `pam_group` lets only members of `admin` or `wheel` become root (`ruser root_only`). That is FreeBSD's default with an empty root password: `nullok`, and su to root for wheel only. Becoming any other user takes that user's password, unless it's empty.
- root can be given a password (or `*`, as macOS does) with `passwd(1)` (below).

**Code signing.** libpam loads every module with `dlopen`, and under enforcement the kernel refuses a library that the image's trust cache doesn't list (`docs/kernel/amfi-provider.md` §4). `//tools/trustcache` lists every signed Mach-O staged in an image, whatever its name, so libpam, the shim, the eleven modules, login and su are all in it. The test runs under enforcement and fails if `ndamfi` refuses anything.

**The test:** `//kernel:sbsa_pam_session_test` boots `//images:pam_session_root`, which is `session_root` plus two test accounts from `tests/qemu/pam`. `test` (uid 501, group `admin`) and `guest` (uid 502) have the password `neodarwin` and home directories under `/Users` that they own. The system's own root has no such accounts. The test checks, in order:
- A wrong password is refused ("Login incorrect"). `test` logs in with its password.
- `su - root -c id` makes `test` root without a password (`uid=0(root)`, status 0).
- As root it writes `/etc/nologin`. `pam_nologin` shows the file and refuses `guest`. root still logs in, with no password, after seeing it, and removes it.
- `guest` logs in, and su refuses it (`su: Sorry`, status 1): it is in neither `admin` nor `wheel`.

`//kernel:sbsa_session_boot_test` still logs in as root, now through PAM.

| Finding | Resolution |
|---|---|
| The first non-root login panicked the kernel: launchd, PID 1, was killed by `EXC_GUARD` (`kGUARD_EXC_DESCRIPTOR_VIOLATION`). launchd-842 answers a non-root client's lookup with `VPROC_ERR_TRY_PER_USER`, and starts a per-user launchd for that user. PID 1 answers the new launchd, and later exchanges jobs and ports with it, in MIG messages with out-of-line port arrays (`job.defs`: `get_listener_port_rights` first, then `take_subset`, `lookup_children`; "Per-user launchd", below). xnu-12377 refuses those from a platform binary (`IPC_POLICY_ENHANCED_V2`) with a fatal guard exception. With the boot-arg `ool_port_array_enforced=0`, PID 1 survives and the per-user launchd it starts dies instead | launchd patch 0005 adds `HAVE_PER_USER_LAUNCHD`, built 0: PID 1 serves every user, as embedded launchd does |
| hdiutil records the build user (uid 501) as every file's owner and drops set-user-ID bits. su ran as uid 501 ("su: not running setuid"), and a user with uid 501 owned every system file | `//tools/hfsowners` (Swift) rewrites the image's catalog after hdiutil: every file and folder is root:wheel, every `files`/`tree_modes` mode is set again, and `hfs_ramdisk`'s new `owners` gives single paths (home directories) to their users. This applies to every image |
| `pam_unix` compares `crypt()` of the typed password with `pw_passwd`, so an empty password never matches | OpenPAM patch 0001: `nullok` |
| With `USE_PAM`, login leaves the root-terminal check to the policy (FreeBSD's `pam_securetty`), which macOS doesn't have: its root has no password | system_cmds patch 0001: the check runs with PAM too |
| `pam_group` asks `mbr_check_membership(3)`, which answers `EIO` without Open Directory (Libinfo's `DS_AVAILABLE` is off), so no one was a member and su refused everyone | pam_modules patch 0001: when the call fails, check `/etc/group` as the module's FreeBSD code does (primary group or member list) |
| `pam_uwtmp` can't write utmpx on a read-only root: under the test PID 1 that runs getty itself (`//images:getty_root`), `pam_open_session` failed and login exited | `pam_uwtmp` is optional, as a `pututxline` failure was to login's non-PAM path |
| A policy without a session chain falls back to `other`'s `pam_deny`, so dropping `pam_launchd` alone would make `su -` fail to open its session | su's session is `pam_permit` |
| su.c includes the private `<SoftLinking/SoftLinking.h>` (for libEndpointSecuritySystem, closed) and libsystem_sandbox's private `<rootless.h>` | `base/shell_cmds/compat`: a SoftLinking that resolves the library with `dlopen` (not found), and `rootless_restricted_environment()` answering 0 |
| libpam's public headers are in the SDK | installed from OpenPAM's tree, build-only, for login, su and the modules. They aren't in the sysroot: nothing in libSystem includes them |

Not yet: per-user launchd sessions (`pam_launchd`; "Per-user launchd", below), and account expiry. `passwd(1)` and `chpass(1)` are below, and sshd's policy under "Loopback and sshd".

### After P1-08: passwd and chpass

Accounts live in `/etc/master.passwd` alone, as on FreeBSD without `pwd.db`, and users change them with system_cmds-1039's commands (`//base:system_commands`):

| Command | From | Backend |
|---|---|---|
| `/usr/bin/passwd` | `passwd.tproj`: `passwd.c`, `file_passwd.c` | the project's own file backend (iOS's): it rewrites `master.passwd` in place, under `O_EXLOCK`, through a temporary file and `rename` |
| `/usr/bin/chpass`, `chfn`, `chsh` | `chpass.tproj` with vipw's `pw_util.c` and pwd_mkdb's `pw_scan.c` | FreeBSD's file path, which Apple's sources keep under `!__APPLE__`: `pw_copy` the changed line into a copy of `master.passwd`, then `pwd_mkdb -p` |
| `/usr/sbin/pwd_mkdb` | `pwd_mkdb.tproj` | Apple's: no `pwd.db` (its `db(3)` code is `!__APPLE__`); `-p` writes `/etc/passwd` and installs the new `master.passwd` |

- **PW_FILES**, NeoDarwin's switch (system_cmds patches 0003 and 0004; without it upstream is unchanged), compiles out Open Directory, PAM and NIS. On macOS, passwd changes passwords through `pam_opendirectory` (the `passwd` PAM policy) or Open Directory directly as root, and chpass edits the record through Open Directory and CoreFoundation (`open_directory.c`). All of that is closed. OpenPAM's `pam_unix` can't change a password (`pam_sm_chauthtok` returns `PAM_SERVICE_ERR`), so passwd has no PAM path, and NeoDarwin installs no `passwd` policy.
- **setuid.** passwd, chpass, chfn and chsh are setuid root (4555), as on FreeBSD (`images/BUILD.bazel`'s modes). A user changes their own password after giving the old one. chpass asks a user for their password too, and lets them change the full name, office, phones and shell (one of `/etc/shells`). root changes any record without a password. chpass runs `$EDITOR` (default `vi`, which the base doesn't have) as the user, on a temporary copy of the record. `chpass -s SHELL [user]` needs no editor.
- **/etc/passwd** holds `*` for every password, as `pwd_mkdb -p` writes it, and the build now writes it so too (`base/etc/build.sh`). Libinfo's file module reads `master.passwd` only for euid 0, and login, su, passwd and chpass are setuid. Until now the build copied the hashes into the world-readable file.
- **Hashes.** Libc-1725's `crypt(3)` (`gen/crypt.c`) has only DES: traditional DES (two salt characters, 25 rounds, only the first eight characters of the password count) and BSDi's extended DES (`_`, four characters of rounds, four of salt). Libc patch 0001 makes `crypt()` choose the scheme by the setting's prefix, as FreeBSD's libcrypt does:

  | Prefix | Scheme | Code |
  |---|---|---|
  | `$6$` | SHA-512 crypt (Drepper), `rounds=` 1000 to 999999999, default 5000 | FreeBSD `lib/libcrypt/crypt-sha512.c`, `sys/crypto/sha2/sha512c.c` |
  | `$5$` | SHA-256 crypt | `crypt-sha256.c`, `sha256c.c` |
  | `$2a$`, `$2b$`, `$2y$` | bcrypt, cost 4 to 31 | `secure/lib/libcrypt/crypt-blowfish.c`, `blowfish.c` |
  | `$1$` | MD5 crypt | `crypt-md5.c`, `sys/crypto/md5c.c` |
  | `_` | extended DES | Libc's own, unchanged |
  | anything else | traditional DES | Libc's own, unchanged |

  The FreeBSD files are pinned one by one (`base/libc/freebsd.lock`, the commit libm uses) and built into libsystem_c with hidden visibility, so its exports stay `crypt`, `encrypt` and `setkey`, as in macOS's `.tbd` (no `crypt_r` or `crypt_set_format`). Existing DES hashes go through the same code as before. FreeBSD's NT hash (`$3$`, MD4) is left out. A malformed bcrypt setting makes `crypt()` return `NULL`, which pam_unix treats as a wrong password.

  **passwd writes SHA-512 crypt** (system_cmds patch 0003, with `PW_FILES`): `$6$`, 16 salt characters (96 bits) from `arc4random`, and the default 5000 rounds. That is FreeBSD's default (`passwd_format=sha512` in `login.conf`); NeoDarwin has no `login.conf`, so the format is fixed. root can still install any hash `crypt(3)` takes with `chpass -p`. Before, passwd wrote extended DES (65537 rounds, a 24-bit salt): a 64-bit result from 56-bit keys. `//tests/crypt:crypt_test` checks every scheme on the host against published vectors (Drepper's SHA-crypt tests, Openwall's bcrypt ones, FreeBSD's and `openssl passwd`'s MD5) and DES against macOS's own `crypt()`; `//tests/crypt:crypt_hash PASSWORD SETTING` prints a hash as NeoDarwin's libc computes it.
- **path_helper.** `/usr/libexec/path_helper` comes from shell_cmds-326 (`//base:shell_commands`). `/etc/zprofile` (zsh) and `/etc/profile` (bash, sh) run `eval $(path_helper -s)` when it's executable. It builds `PATH` from `/etc/paths` (files-968's, unmodified: `/usr/local/bin:/System/Cryptexes/App/usr/bin:/usr/bin:/bin:/usr/sbin:/sbin`) and the files in `/etc/paths.d`, which the image doesn't have. Then it appends the inherited `PATH`'s other entries (login's `_PATH_DEFPATH`, all of them already listed). If `MANPATH` is set, it builds that from `/etc/manpaths` the same way.

**The test:** `//kernel:sbsa_accounts_test` boots `//images:pam_session_root`, with code signing enforced:
- `test` logs in to zsh, and `$PATH` is `/etc/paths`' entries in order.
- `test` runs `passwd`: the old password, then the new one twice. After logout, the old password gets "Login incorrect", and the new one logs in.
- `test` runs chpass with `EDITOR=/usr/local/bin/chpass_editor` (`tests/qemu/pam`, a sed script). chpass asks for test's password, then reports "user information updated". `id -F` (getpwuid from `/etc/passwd`, as test) answers with the new full name.
- root runs `chpass -s /bin/bash test`. `id -P test` (from `master.passwd`) shows the new shell and the SHA-512 crypt hash passwd wrote (`test:$6$`; split in zsh: `hash-6-16-86`, 16 salt characters and 86 of hash). `/etc/passwd` has `test:*:501:20:Neo Q42:/Users/test:/bin/bash`.
- root sets its own password with `passwd`. No old password is asked: it was empty. From then on, login asks root for it, and the SHA-512 hash matches.

`//kernel:sbsa_pam_session_test` logs in `test`, whose hash in `tests/qemu/pam` is SHA-512 crypt, and `guest`, whose hash is still traditional DES.

| Finding | Resolution |
|---|---|
| passwd's `file_passwd.c` reads and rewrites `master.passwd` itself, without pwd_mkdb, and doesn't touch `/etc/passwd` | nothing to do once `/etc/passwd` holds `*`: only the hash changes |
| chpass's FreeBSD path needs FreeBSD libutil's record functions (`pw_copy`, `pw_dup`, `pw_equal`, `pw_make`, `pw_scan`, `pw_tempname`). Apple's vipw `pw_util.c` has them, under `!__APPLE__`. Darwin's `struct passwd` has no `pw_fields` | patch 0004 builds them with `PW_FILES` and declares them in `pw_util.h`. The record's source is always the files |
| `pw_copy` grows its buffer with `reallocarray`, which on Darwin is libmalloc's `reallocarray$DARWIN_EXTSN`, absent from the SDK headers | patch 0004: an overflow-checked `realloc` on Darwin |
| Libc's `crypt(3)` has only DES, weak for passwords ("Hashes") | Libc patch 0001: FreeBSD's libcrypt schemes, chosen by prefix; passwd writes SHA-512 crypt |
| FreeBSD's libcrypt and SHA-2 code use `<sys/endian.h>` and `explicit_bzero`, which Darwin lacks | built with ndcrypto's prelude (`kernel/neodarwin/crypto/compat`), as libresolv's MD5 is; the files stay unmodified |
| passwd passed `crypt()`'s result for the old password straight to `strcmp`; `crypt()` can now return `NULL` (a malformed bcrypt hash) | patch 0003: `NULL` is a wrong password, and an error for the new hash |
| The host toolchain's fastbuild defines `DEBUG`, and Libc's DES code then prints its tables (host test only) | `//tests/crypt` builds it with `-UDEBUG` |
| chpass takes the record as edited only if the temporary file's modification time changed. HFS+ keeps it to the second, so an editor that finishes within the second changes nothing | the test's editor sleeps a second first. An interactive edit takes longer |
| The test image has no `grep` | the test reads `/etc/passwd` with `cat` |

### After P1-08: BSM audit

The system records BSM audit trails, as macOS does: the kernel's audit subsystem, libbsm, auditd and the tools to read trails. login and su record their events (`//base:bsm_audit`).

**Where it comes from.** macOS 26's release set has no OpenBSM (`release.json`). Apple last published it as OpenBSM-21 (OpenBSM 1.1, Mac OS X 10.6.8). Every later macOS ships libbsm, libauditd and auditd without source. NeoDarwin builds OpenBSM-21, as it builds launchd-842, the last launchd Apple published. Its `OpenBSM.xcodeproj` targets are replayed, with the project's committed `config.h`. libauditd, which launchctl-842 calls, is part of the project and isn't closed. auditd's dependencies are open too: launchd check-in (liblaunch), ASL, notify and MIG. Nothing had to be compiled out.

| Piece | Installed |
|---|---|
| libbsm | `/usr/lib/libbsm.0.dylib`, with `libbsm.dylib` linked to it. It exports 151 of the SDK `libbsm.0.tbd`'s 180 symbols, and 2 of OpenBSM 1.1's own (`audit_set_terminal_host` and `audit_set_terminal_port`). The 29 it lacks are Apple's additions after 2011 (below) |
| libauditd | `/usr/lib/libauditd.0.dylib` (+ `libauditd.dylib`): 16 of the SDK's 18. It lacks `audit_quick_start_internal` and `auditd_set_sflags_masks` |
| Commands | `/usr/sbin/auditd`, `audit`, `auditreduce` and `praudit` |
| Configuration | `/etc/security/audit_class`, `audit_control`, `audit_event`, `audit_user` and `audit_warn`, with the project's modes (`audit_control` and `audit_user` 0400, `audit_warn` 0555). The images add `/var/audit` (0700), as files' `hierarchy` does |
| Job | `/System/Library/LaunchDaemons/com.apple.auditd.plist`, unmodified. auditd runs on demand, and its MachService is the audit control port (host special port 9) |

The libbsm exports NeoDarwin lacks:
- `/dev/auditsessions` (`au_sdev_*`);
- the session-flag, control-mode and `expire-after` calls (`audit_get_sflags`, `audit_get_ctlmode` and the like);
- the `audit_token_to_*` accessors;
- the `*_ex` writers;
- the identity, certificate-hash and Kerberos-principal token constructors.

The public headers (`libbsm.h`, `audit_uevents.h`, `audit_filter.h`, `auditd_lib.h`) are build-only, in `usr/local/include/bsm`. `bsm/audit.h` and the other kernel headers are xnu's, from the sysroot, as in Apple's build. login and su also include `<bsm/audit_session.h>`, which no published project has. It comes from the SDK, as su's already did.

**The kernel.** `CONFIG_AUDIT` is in `MASTER.arm64.MacOSX`'s `SECURITY_BASE`, and the SBSA kernel has it. That gives `audit`, `auditon` and `auditctl`, the audit pipe and sessions, and `audit_send_trigger()` to the audit control port. Nothing needed patching. The control mode is normal, so root may call `auditctl(2)` and `auditon(2)` `A_SETCOND` without Apple's entitlements.

**How audit starts.** launchctl's bootstrap does what launchctl-842 does when built with `HAVE_LIBAUDITD`, as macOS's was. Unless `com.apple.auditd.plist` is Disabled, it calls libauditd's `audit_quick_start()`. The call comes after launchctl touches utmpx and before any job loads. libauditd is opened with `dlopen`, so a root without it boots unaudited. `audit_quick_start()`:
- reads `audit_control`;
- opens a new trail in `/var/audit`, links `current` to it and hands it to the kernel (`auditctl(2)`);
- writes the `audit startup` record;
- sets the kernel's event-to-class map, non-attributable mask, policy, file-size limit and host.

launchd then holds the audit control port for the auditd job. auditd starts when a message arrives there, either a kernel trigger (a full trail, low space) or one from `audit -n` and its siblings. It checks in and does what the trigger asks, such as rotating the trail. It exits after its job's `TimeOut` of 60 idle seconds.

**login and su.**
- login builds with `USE_BSM_AUDIT` and `-lbsm`, as Apple's Release settings have it. `login_audit.c` records `AUE_login` for each success and failure, and `AUE_logout`. A successful login also gets the user's auid and preselection mask (`au_user_mask`).
- `login_audit.c` also reports to EndpointSecurity through weak links to libEndpointSecuritySystem, which is closed. `base/system_cmds/compat` makes those symbols null, as they are when the library is absent.
- su builds with `USE_BSM_AUDIT` too, as FreeBSD's does. Apple builds it without and reports to EndpointSecurity instead. `audit_submit(3)` records `AUE_su` for each success and refusal.

**What a trail holds.** As `praudit -l` prints it (`//kernel:sbsa_audit_test`):
- the startup record: `audit startup`, `text,launchctl::Audit startup`;
- a failed login: `login - local`, `subject_ex,-1,root,wheel,-1,nogroup,…`, `text,Login incorrect`, `return,failure : Operation not permitted`. The record is non-attributable, because login knows no account yet. As in Apple's code, it doesn't name the user who was tried;
- a successful login: `subject_ex,test,root,wheel,test,staff,…`, `return,success`;
- su: `su(1)`, `subject,test,…`, `text,successful authentication`;
- the logout.

xnu appends an `identity` token to each record a program submits: `identity,1,login,complete,,complete,0x<cdhash>`.

**Status on macOS.** Apple has deprecated auditd and the audit APIs since macOS 11 in favour of EndpointSecurity, and the SDK's headers mark them deprecated. The kernel's audit subsystem is still there, and it is what NeoDarwin uses. NeoDarwin keeps BSM audit because it is the open, documented record of logins and privilege changes. EndpointSecurity's client library and daemon are closed.

**The test:** `//kernel:sbsa_audit_test` boots `//images:pam_session_root` under code-signing enforcement. It fails if `audit_quick_start()` fails.
1. `test` types a wrong password, logs in with the right one, runs `su - root -c id` and logs out.
2. root logs in and runs `audit -n`. launchd starts auditd on demand, and auditd closes the trail and opens a new one (`ls` shows a `.not_terminated` file).
3. `praudit -l` prints the records above.

| Finding | Resolution |
|---|---|
| macOS 26.0's `release.json` has no OpenBSM or other audit project. Apple's GitHub has one OpenBSM tag, OpenBSM-21, from the 10.6.8 release set | OpenBSM-21, pinned as launchd-842 is: the last published version, outside the release set |
| auditd didn't link. xnu's `mach/audit_triggers.defs`, which the project's `.defs` includes, has gained a second routine, `audit_analytics`. With it the kernel sends the signing ID and name of each non-platform program that submits a record | OpenBSM patch 0002: auditd serves it and notes the caller in its debug log |
| xnu appends an identity token (`AUT_IDENTITY`, 0xed) to every record a program submits, unless the program holds Apple's reserved-class entitlement. OpenBSM 1.1's `au_fetch_tok()` didn't know the token, so praudit stopped at login's first record | OpenBSM patch 0001: libbsm reads and prints the identity, certificate-hash and Kerberos-principal tokens, in plain and XML form, with the structures of macOS's `<bsm/libbsm.h>` |
| Starting auditd, NeoDarwin's first on-demand daemon, killed PID 1 (`EXC_GUARD`, `kGUARD_EXC_DESCRIPTOR_VIOLATION`) and the kernel panicked. It was `get_listener_port_rights` ("Per-user launchd", below). With `ool_port_array_enforced=0` audit worked | launchd patch 0006 adds `HAVE_IMPORTANCE_WATCH_PORTS`, built 0. The child doesn't ask, and the daemon runs without the importance boost |
| system_cmds patch 0001 keeps login's root-terminal refusal on the PAM path. That block calls `au_login_fail()` with two of its four arguments, and Apple never compiles it with `USE_BSM_AUDIT` | patch 0001 now also passes the user name and `fflag` |
| su declares `auid` both under `USE_BSM_AUDIT` and under `__APPLE__` | shell_cmds patch 0002 declares it once |
| login includes the private `<SoftLinking/WeakLinking.h>` and `<EndpointSecuritySystem/ESSubmitSPI.h>` | `base/system_cmds/compat` |

Not built: `auditfilterd` (not in Apple's targets) and OpenBSM's man pages. Not done:
- the libbsm exports Apple added later (above), and so clients of `/dev/auditsessions`;
- launchd's own `HAVE_LIBAUDITD`, its `audit_quick_stop()` at shutdown, which closes the trail cleanly. Without it, a trail still open at shutdown stays `.not_terminated`, and the next `audit_quick_start()` renames it when it opens a new one;
- `logger`, which `audit_warn` runs.

### Per-user launchd

launchd-842 on macOS runs one launchd per logged-in user, started by PID 1. NeoDarwin builds it with `HAVE_PER_USER_LAUNCHD=0` (launchd patch 0005), so PID 1 serves every user, as embedded launchd does. This section evaluates the alternatives and the decision. Citations: launchd-842.92.1 (`src/`, `liblaunch/`) and xnu-12377 (`osfmk/ipc/`).

**Where launchd-842 moves port arrays.** `job_types.defs:27-30` defines `mach_port_move_send_array_t` and `mach_port_make_send_array_t`. MIG puts an out-of-line ports descriptor in a message even when its count is 0. Error replies are not complex, so they carry no array.

| Routine (`job.defs`) | Array | Sent by | When |
|---|---|---|---|
| `get_listener_port_rights` (:288) | `out sports`, MAKE_SEND | the server, PID 1 (`core.c:8549`) | the child of every fork asks before it execs (`job_start_child`, `core.c:4653`), to pass the job's `MachServices` to `posix_spawn` as importance-watch ports. PID 1 answers with an array when the job has `upfront` services: every `MachServices` job (`core.c:6577`), and the per-user launchd job (`core.c:8817`) |
| `take_subset` (:123) | `out ports`, MOVE_SEND, plus one MOVE_RECEIVE | PID 1 (`core.c:9876`) | a per-user launchd's `move_subset` (`core.c:9599`) grabs a session's subset from PID 1 (`_vproc_grab_subset`, `libvproc.c:375`). pam_launchd starts this through `_vprocmgr_switch_to_session` for any session type but Background |
| `lookup_children` (:209) | `out childports`, MOVE_SEND | the server (`core.c:9321`) | 842's `launchctl bstree`. NeoDarwin's launchctl doesn't have it |
| `legacy_ipc_request` (:278) | `in request_fds` and `out reply_fds`, MOVE_SEND | the client (`launch_socket_service_check_in`, `libvproc.c:1033`, count 0) and the server (`core.c:11634`) | a daemon's socket check-in over MIG |

So the panic at the first non-root login comes from the first routine. `job_mig_lookup_per_user_context` (`core.c:8833`) creates `com.apple.launchd.peruser.UID` with an upfront service, and PID 1 is killed answering that job's own child. `lookup_per_user_context` itself returns a single port. The same reply also killed PID 1 when launchd first started a daemon with `MachServices` (`com.apple.auditd`, "BSM audit"). Launchd patch 0006 (`HAVE_IMPORTANCE_WATCH_PORTS=0`) leaves out the child's request, so daemons run unboosted.

**What xnu-12377 enforces.**
- `ipc_policy_for_task` (`ipc_policy.c:117-136`) gives `IPC_POLICY_ENHANCED_V2` to every `TFRO_PLATFORM` task, and every binary in NeoDarwin's trust cache is a platform binary. It gives it too to a task whose platform restrictions version is 2 or more.
- `ipc_validate_kmsg_header_from_user` (`ipc_policy.c:931-958`) checks a message from such a task that carries any OOL ports descriptor. The destination must be an `IOT_CONNECTION_PORT_WITH_PORT_ARRAY` (`ip_is_port_array_allowed`, `ipc_port.h:328`), and the message may carry at most one array.
- The descriptor's copyin (`ipc_kmsg.c:2445-2460`) also refuses any disposition but COPY_SEND.
- Both raise `kGUARD_EXC_DESCRIPTOR_VIOLATION`, which is always fatal.
- Only `mach_port_construct(MPO_CONNECTION_PORT_WITH_PORT_ARRAY)` makes such a port (`mach_port.c:2492`). It requires the entitlement `com.apple.developer.allow-connection-port-with-port-array` (`port.h:456`, `mach_port.c:2502-2513`).
- A MIG reply port is never that type, so a platform binary can't reply with a port array at all.
- The exemptions (`ipc_should_apply_policy`, `ipc_policy.c:270-294`) are for simulated, translated and opted-out tasks only. Opting out takes `IMGPF_3P_PLUGINS`, or a DEVELOPMENT kernel's AMFI configuration.
- The boot-arg `ool_port_array_enforced` (`ipc_policy.c:76-81`, default on) turns both checks off.
- Single port descriptors, with any disposition and including moved receive rights of plain ports, are not affected.

**What per-user launchds would buy.**
- LaunchAgents per user, loaded by the per-user launchd's `launchctl bootstrap -S Background` (`core.c:7044`).
- A bootstrap namespace per user and per session, which pam_launchd moves login sessions into.
- Services that die with the user's last session.

macOS hasn't worked this way since launchd 2.0 (OS X 10.10). There, one PID 1 serves the system, user (`gui/UID`, `user/UID`) and session domains, over XPC messages that carry ports one descriptor each. Apple left the per-user launchd process model behind, and xnu-12377's policy assumes it is gone.

**Options.**
- **(a) Keep the embedded model** (the current state). PID 1 serves every user's lookups and registrations. There are no per-user agents or namespaces, and no change is needed. A user's jobs could still be loaded into PID 1 with `UserName`.
- **(b) Patch launchd's routines to send single ports.** `get_listener_port_rights` would become one port per message (an index argument, called until `BOOTSTRAP_UNKNOWN_SERVICE`), or stay off (patch 0006). `take_subset` would carry the jobs' data and a count, then one `MOVE_SEND` per routine call. The subset's job manager would have to stay in PID 1 until the last port is taken, where 842 now hands it over in a single reply (`job_mig_take_subset`, `jobmgr_export2`). `lookup_children` and `legacy_ipc_request` would change the same way. liblaunch (in the libxpc stand-in) and launchd change together, which is possible because NeoDarwin builds both. A per-user session would also need more, beyond the arrays:
  - launchctl's `bootstrap -S Background/StandardIO/Aqua` and LaunchAgents directories (NeoDarwin's launchctl has `-S System` only);
  - pam_launchd in the `login` and `su` policies, with an audit session per login (`audit_session_join`, `core.c:8800`);
  - the cause of the per-user launchd's own death, still not diagnosed. With `ool_port_array_enforced=0` PID 1 survived, and the per-user launchd it started died. The policy above is all gated on that boot-arg, so something else killed it.

  That is several routines, both sides of the protocol and an unexplained failure, so (b) is neither small nor clean today.
- **(c) A kernel exemption for PID 1** (an entitlement-checked or `task_is_initproc` bypass of `IPC_POLICY_ENHANCED_V2`'s array checks) would weaken the hardening for the one task that holds every service's receive right and every job's task port. The check exists so that a compromised platform process can't spray moved rights into another's space, and launchd is where that matters most. The receiving per-user launchds are platform binaries too, so they would need the same exemption. NeoDarwin would also diverge from xnu's policy in a security-relevant path.
- **(d) The boot-arg `ool_port_array_enforced=0`** turns the policy off for every process, and doesn't make per-user launchds work.

**Decision: (a).** `HAVE_PER_USER_LAUNCHD` stays 0, and pam_launchd stays out of the policies. If NeoDarwin wants per-user agents, the direction is macOS's: user domains inside PID 1, with jobs loaded per user (`UserName`, and a `launchctl bootstrap gui/UID`-style command in NeoDarwin's launchctl). The alternative is per-user launchd processes, which xnu's policy argues against. A future (b) needs, at the least:
1. the four routines above without port arrays;
2. the per-user launchd's death diagnosed (its crash report's guard code);
3. launchctl session types and LaunchAgents;
4. pam_launchd in the policies with audit sessions;
5. a test: a non-root login gets its own bootstrap subset, a LaunchAgent loads for the user, and PID 1 survives.

### After P1-08: loopback and sshd

NeoDarwin configures the loopback interface at boot and runs OpenSSH's sshd under launchd, as macOS does. Everything here is tested over 127.0.0.1 inside QEMU. Since P1-19 checkpoint 1 there is also a NIC (virtio-net) and the host logs in over it, and since checkpoint 2 the NIC takes a DHCP lease at boot and names resolve (`docs/kernel/network.md`, "Checkpoint 2"): two more LaunchDaemons that `launchctl bootstrap` loads like getty's and auditd's, `com.neodarwin.netconfigd` (netconfigd and FreeBSD's dhclient) and `com.apple.mDNSResponder` (mDNSResponder's POSIX daemon), with libresolv-93 and `/etc/resolv.conf` -> `/var/run/resolv.conf`. The rest is in later items in `roadmap/backlog.yaml`: Ethernet dexts in Phase 3, P4-24 (the rest of the networking userland, `resolv.conf`, pf) and P4-25 (sshd on the board with a key, scp and sftp). This section is the loopback part of both, and sets no status.

| Piece | From | Installed |
|---|---|---|
| loopback | launchctl's bootstrap (`base/launchctl/Sources/Network.swift`) | lo0 up with 127.0.0.1/8 and ::1/128 |
| network commands | network_cmds-726 (`//base:network_commands`) | `/sbin/ifconfig`, `/sbin/ping`, `/sbin/route`, `/usr/sbin/netstat`; since P4-24 also `/sbin/ping6`, `arp`, `ndp`, `traceroute`, `traceroute6` (both setuid), `rarpd` and `spray` in `/usr/sbin`, and `/usr/libexec/kdumpd` with its (disabled) job (726's `rtsol` gave way to FreeBSD's, below) |
| IPv6 DNS (P4-24, `docs/kernel/network.md`, "IPv6 DNS: RDNSS and DHCPv6") | FreeBSD's `rtsold` (`//base:router_solicitation`) and `resolvconf`, openresolv (`//base:resolver_config`); the WIDE DHCPv6 client from FreeBSD's `net/dhcp6` port (`//base:dhcp6_client`) | `/usr/sbin/rtsold`, `/sbin/rtsol`; `/sbin/resolvconf`, `/usr/libexec/resolvconf/libc`, `/etc/resolvconf.conf`; `/usr/sbin/dhcp6c`, `/usr/libexec/dhcp6c-managed` and `dhcp6c-other` (rtsold's M/O scripts), `/usr/libexec/dhcp6c-script`. netconfigd runs rtsold on each autoconfiguring interface; DHCP, RDNSS and DHCPv6 servers merge in `/var/run/resolv.conf` |
| rtadvd (P4-24, `docs/kernel/network.md`, "rtadvd") | FreeBSD's `usr.sbin/rtadvd`, pinned per file (`//base:router_advertiser`) | `/usr/sbin/rtadvd`, `/etc/rtadvd.conf` (FreeBSD's, every entry commented out). Nothing runs it: a router's root turns on `net.inet6.ip6.forwarding`, puts the interface in router mode and starts it |
| launchproxy | launchd-842.92.1 (`//base:launchproxy`) | `/usr/libexec/launchproxy` |
| libcrypto | OpenSSL 3.5.9, upstream (`//base:libcrypto_dylib`) | `/usr/lib/libcrypto.3.dylib` and `libssl.3.dylib`; the legacy provider in `/usr/lib/ossl-modules`, engines in `/usr/lib/engines-3`; `/usr/bin/openssl`; `/etc/ssl/openssl.cnf`. The headers are build-only, in `usr/local/openssl` |
| OpenSSH | OpenSSH-354.0.3, OpenSSH 10.0p2 (`//base:openssh`) | `ssh`, `scp`, `sftp`, `ssh-add`, `ssh-agent`, `ssh-keygen`, `ssh-keyscan` and `slogin` in `/usr/bin`; `/usr/sbin/sshd`; `sshd-session`, `sshd-auth` and `sftp-server` in `/usr/libexec`; `/etc/ssh`; `/etc/pam.d/sshd`; `/System/Library/LaunchDaemons/ssh.plist` |
| sshd-keygen-wrapper | NeoDarwin's, Embedded Swift (`//base/sshd_keygen_wrapper`) | `/usr/libexec/sshd-keygen-wrapper` |
| pf and NTP (P4-24, `docs/base/pf-ntp.md`) | OpenBSD 4.3's pfctl against xnu's `pfvar.h` (`//base:packet_filter`); Apple's sntp from ntp-139 (`//base:ntp_client`) and NeoDarwin's `sntp-wrapper` (`//base/sntp_wrapper`) | `/sbin/pfctl`, `/etc/pf.conf` (macOS's anchors), `/etc/pf.os`, `com.apple.pfctl.plist`; `/usr/bin/sntp`, `/usr/libexec/sntp-wrapper`, `/etc/ntp.conf`, `com.neodarwin.sntp.plist`. Both jobs are `Disabled`, as ssh.plist is: pf is off and the clock isn't set until root turns them on |
| `_sshd` | `base/etc` | the privilege-separation user and group, uid and gid 75 as on macOS, home `/var/empty` |

**Loopback.** The kernel attaches lo0 but gives it no address. launchctl-842's `system_specific_bootstrap()` configures it, right after the host name (`loopback_setup_ipv4`, `loopback_setup_ipv6`). NeoDarwin's launchctl now does the same, with the same ioctls:
- `SIOCGIFFLAGS`, then `SIOCSIFFLAGS` with `IFF_UP`;
- `SIOCAIFADDR` with 127.0.0.1 and the mask 255.0.0.0;
- `SIOCAIFADDR_IN6` with ::1/128 and infinite lifetimes (`EEXIST` is fine).

Swift can't call `ioctl`, which is variadic, or import the `_IOW` request macros. So `launch_shim.h` has a static inline `nd_ioctl` and the four request codes as constants. No ifconfig runs at boot. The kernel adds fe80::1%lo0 itself.

**ifconfig and the others** build from network_cmds' Release settings, with xnu's private `net/` headers: the targets' `HEADER_SEARCH_PATHS` add System.framework's PrivateHeaders. network_cmds-726 knows two netem models ("iod", "fpd") that xnu-12377's `if_var_private.h` doesn't define, and network_cmds patch 0001 leaves them out. Apple signs ping and route with network-management entitlements. NeoDarwin has no sandbox or policy that reads them, so they're signed ad hoc without, as ps is. P4-24 built the rest of network_cmds the same way (`docs/kernel/network.md`, "P4-24: the rest of network_cmds and IPv6"): rtadvd has no sources in 726, and dnctl goes with pf. Apple's libpcap, libipsec and tcpdump followed (`docs/kernel/network.md`, "libpcap and tcpdump"): `/usr/lib/libpcap.A.dylib`, which traceroute's TCP probes use, `/usr/lib/libipsec.A.dylib`, and `/usr/sbin/tcpdump`, root's to use as on macOS (`/dev/bpf*` are root's, mode 0600).

**libcrypto: OpenSSL 3.5, as FreeBSD's base has it.** macOS links OpenSSH against `/usr/lib/libcrypto.46.dylib`, LibreSSL 3.3.6's libcrypto (macOS's `openssl version` and `ssh -V` say LibreSSL 3.3.6), and Apple's LibreSSL project isn't in the macOS 26.0 release set (distribution-macOS `macos-260` has only `OpenSSL098-85`, the 0.9.8 library it keeps for old binaries). Until 2026-10-01 NeoDarwin pinned upstream LibreSSL 3.3.6 to match. That release is from 2022, with advisories fixed only in later ones. NeoDarwin now builds OpenSSL, the library FreeBSD's base ships:
- **Version.** freebsd-src `050683bb8e13` (the commit NeoDarwin pins FreeBSD files at) has OpenSSL 3.5.8 in `crypto/openssl` (`VERSION.dat`). 3.5 is OpenSSL's current LTS line (supported to April 2030). NeoDarwin pins the newest 3.5 patch release, **3.5.9** (29 Sep 2026, a security release), from the openssl/openssl GitHub release, with the hash the release publishes (`openssl-3.5.9.tar.gz.sha256`). The tarball's bundled submodules (`oqs-provider`, `pkcs11-provider`, `cloudflare-quiche`) aren't built.
- **Configure** is OpenSSL's own Perl script. It runs no compile, link or run checks, so the target (`darwin64-arm64`) and the options are its whole answer, pinned in `base/openssl/build.sh`: `--prefix=/usr --openssldir=/private/etc/ssl shared no-tests no-docs`, FreeBSD's choices (its `configuration.h`) `no-aria no-idea no-mdc2 no-sm2 no-sm3 no-sm4 enable-ec_nistp_64_gcc_128`, and `no-padlockeng` (x86 only; FreeBSD builds it only for amd64 and i386). The rest are OpenSSL's defaults, as on FreeBSD: engines, the legacy provider and the deprecated APIs on; SSLv3, MD2, RC5, zlib and KTLS off. Compile and link see NeoDarwin's sysroot and root, through links in the build directory, so the flags `openssl version -f` records name no sandbox path; `SOURCE_DATE_EPOCH` (the release date) gives `openssl version -b`.
- **Assembly is on**, as FreeBSD's is for aarch64 (perlasm's `ios64` flavour; Xcode's clang assembles it). `armcap.c`'s Apple path assumes AES, PMULL, SHA-1 and SHA-256, which every Apple arm64 core, QEMU's CPUs and the Q8B's Cortex-X1C/A78C have, and asks `hw.optional.armv8_2_sha512` and `hw.optional.armv8_2_sha3` for the rest.
- **Randomness.** On Darwin, OpenSSL seeds from CommonCrypto's `CCRandomGenerateBytes`, which the base doesn't have. `-DOPENSSL_NO_APPLE_CRYPTO_RANDOM` turns that off, and OpenSSL seeds from libSystem's `getentropy(2)`, found at run time.
- **Install names** are OpenSSL's own, `/usr/lib/libcrypto.3.dylib` and `/usr/lib/libssl.3.dylib` (compatibility and current version 3.0.0). Keeping LibreSSL's `libcrypto.46.dylib` name would have promised LibreSSL's ABI, which OpenSSL 3 doesn't have. macOS ships no `libcrypto.3`, so nothing clashes, and no unversioned `libcrypto.dylib` is installed (macOS's is a stub that aborts any program that loads it). As on macOS, the system libcrypto is for the base's own programs: third-party software should bring its own OpenSSL rather than link it (Apple's policy for its libcrypto). The headers and `-lcrypto` links are build-only, in `usr/local/openssl`.
- **What's installed** follows FreeBSD's `secure/` tree: `libssl` (nothing in the base links it but `openssl`), `/usr/bin/openssl`, the legacy provider (`/usr/lib/ossl-modules/legacy.dylib`: MD4, DES, RC4 and the other old algorithms, loaded on request), FreeBSD's aarch64 engines less `devcrypto` (no `/dev/crypto`): `capi` and `loader_attic` in `/usr/lib/engines-3`, and `openssl.cnf` in `/etc/ssl`. `c_rehash`, `CA.pl`, the static libraries, pkg-config and CMake files aren't installed.

**OpenSSH** is built as Apple's Xcode project builds it, without configure: from the project's committed `openssh/config.h`, the `openbsd-compat` and `libssh` static libraries, then each tool target's own sources. openssh.xcconfig sets the feature macros:
- Kept, because their code needs only libSystem: `clear_lv`, `display_var`, `membership` (`getgrouplist_2`), `nohostauthproxy`, `tmpdir` and `basesystem`. sshd's what string says "Apple modifications: clear_lv display_var membership nohostauthproxy tmpdir basesystem".
- Left out, for closed code: `keychain` (Security.framework; `keychain.m` isn't compiled), `endpointsecurity`, `managed_configuration` and `nw_connection`. Also left out: the two BSM audit fixes, and `launchd`, which needs `launch_activate_socket()`, an XPC-era call that launchd-842's liblaunch lacks (only `ssh-agent -l` uses it).
- openssh patch 0001 turns off in `config.h` what needs code NeoDarwin doesn't have:
  - GSSAPI and Kerberos (Heimdal, GSS);
  - BSM audit (there was no libbsm when this was built);
  - zlib (not in the base), so compression is never negotiated;
  - Seatbelt's `sandbox_init()`: libsystem_sandbox is a stand-in without a policy. The pre-authentication process, `sshd-auth`, uses OpenSSH's rlimit sandbox instead (`SANDBOX_RLIMIT`: no new files, descriptors or processes), on top of privilege separation (chroot to `/var/empty`, user `_sshd`).
- `getrrsetbyname()` (SSHFP records for `VerifyHostKeyDNS`) queries through libresolv-93 (`-lresolv`, as Apple links it). Until P1-19 checkpoint 2 built libresolv, a patch 0003 made it fail with `ERRSET_FAIL`; that patch is gone.
- openssh patch 0003 (since 2026-10-01) gives `config.h` OpenSSL 3.5's libcrypto answers where Apple's are LibreSSL's. These are the checks of OpenSSH's own configure that answer differently, run cross-compiling against OpenSSL 3.5.9: `EVP_CIPHER_CTX_get_updated_iv`, `EVP_CIPHER_CTX_iv` and `_iv_noconst` in place of LibreSSL's `EVP_CIPHER_CTX_get_iv` and `_set_iv`; no `EVP_MD_CTX_cleanup`, `EVP_MD_CTX_init` or `HMAC_CTX_init` (gone since OpenSSL 1.1); `EVP_PKEY_get_raw_private_key` and `_public_key`, and `OPENSSL_HAS_ED25519`. `ssh -V` says "OpenSSH_10.0p2, OpenSSL 3.5.9 29 Sep 2026".
- Not built: `ssh-keysign` (setuid, host-based authentication), `ssh-pkcs11-helper` and `ssh-sk-helper` (libfido2), `ssh-apple-pkcs11`, `sshd-fvunlock`, `remote-login-status`, `slapconfig-keygen` and the regression tools. Without `ssh-sk-helper`, security-key (`-sk`) keys don't work.

**Configuration** is make-config.zsh's:
- OpenSSH's `ssh_config` and `sshd_config`, each with Apple's `Include /etc/ssh/*_config.d/*`. Both are byte-identical to the build host's (macOS 27), as is `100-macos.conf`.
- `100-macos.conf` (for sshd: `UsePAM yes`, `AcceptEnv LANG LC_*`).
- `crypto.conf`, linked to `crypto/apple.conf`: AES-GCM, ECDH P-256 and HMAC-SHA-256 first.

sshd's defaults stand: `PermitRootLogin prohibit-password`, `PasswordAuthentication yes`, `KbdInteractiveAuthentication yes`, `PermitEmptyPasswords no`.

**The PAM policy** (openssh patch 0002) is login's, without `nullok`: `pam_unix` for auth, account and password, `pam_nologin`, and `pam_permit` for the session, as su's is. Apple's has `pam_krb5`, `pam_ntlm`, `pam_mount` and `pam_opendirectory`, all closed. Two more are left out:
- `pam_sacl` (the ssh service ACL) calls `mbr_check_service_membership()`, which answers `EIO` without a directory service, and that denies everyone;
- `pam_launchd`: there are no per-user launchds.

Without `nullok`, an account with an empty password (root's) can't log in over ssh. sshd refuses empty passwords anyway.

**The launch model is macOS's: socket-activated, inetd-style.** `ssh.plist` is Apple's `com.openssh.sshd.plist`, unmodified and `Disabled`. Remote Login is off until root runs `launchctl load -w /System/Library/LaunchDaemons/ssh.plist`. With launchd-842 this works as on Mac OS X 10.9:
- launchctl, not launchd, creates a job's listening sockets. NeoDarwin's launchctl now does, as 842's `sock_dict_edit_entry()` does. For each `Sockets` entry it runs `getaddrinfo` with `AI_PASSIVE`. `SockServiceName` `ssh` gives port 22 from `/etc/services`, on 0.0.0.0 and ::, with `IPV6_V6ONLY`, `SO_REUSEADDR` and `listen(-1)`. It also handles `SockPathName` (Unix sockets), `SockType`, `SockFamily`, `SockProtocol` and `SockNodeName`. It sends the descriptors in the `SubmitJob` message (`launch_data_new_fd`; liblaunch passes them with `SCM_RIGHTS`).
- Left out: `Bonjour` (launchctl doesn't register the socket with mDNSResponder; the daemon runs since P1-19 checkpoint 2), `SecureSocketWithKey` and multicast groups.
- For an `inetdCompatibility` job, launchd watches the sockets and starts `/usr/libexec/launchproxy`. launchproxy checks in, accepts each connection and runs the job's program with the connection as standard input and output (`Wait` false, up to 42 instances).
- The program, `sshd-keygen-wrapper`, generates any missing host key (ecdsa, ed25519, rsa: `ssh-keygen -q -t ALG -f /etc/ssh/ssh_host_ALG_key -N "" -C ""`, as Apple's `HostKeyManager` does). So the keys are made on the first connection after Remote Login is turned on. Then it execs `sshd -i`. sshd re-execs `sshd-session`, which runs `sshd-auth` for the pre-authentication phase.
- A KeepAlive `sshd -D` would have needed none of this. But it would listen from boot, and the socket model is what macOS ships. Every part of it is open source (launchd-842, launchproxy), and only launchctl's half was missing.

Apple's wrapper is Swift on Foundation, System and AppleKeyStore. NeoDarwin's is about 70 lines of Embedded Swift on libSystem. It leaves out the Recovery (base system) keys and banner, the preboot copy of the keys and `sshd-fvunlock`'s plist. It adds `-e -E /var/log/sshd.log`. Until NeoDarwin has a log store, `syslog(3)` writes to standard error (the libsystem_trace stand-in). `sshd -i` points standard error at `/dev/null`, and so does the plist. With `-e -E`, sshd logs to `/var/log/sshd.log` instead.

**Code signing.** Every new Mach-O is in the image's trust cache: launchctl, the network commands, launchproxy, libcrypto and libssl, OpenSSL's provider, engines and command, the OpenSSH tools and the wrapper (`//tools/trustcache` lists everything signed in the image). The test runs under enforcement with `--absent 'ndamfi: refused'`. Nothing is setuid.

**The test:** `//kernel:sbsa_ssh_session_test` boots `//images:pam_session_root` (accounts `test` and `guest`, password `neodarwin`) with code signing enforced, and logs in as root:
- `ifconfig lo0` shows `inet 127.0.0.1 netmask 0xff000000` and `inet6 ::1 prefixlen 128`. `ping -c 1 127.0.0.1` gets its reply.
- `launchctl load -w .../ssh.plist`; `launchctl list` shows `com.openssh.sshd`.
- `ssh -o StrictHostKeyChecking=no test@127.0.0.1 id`: the first connection generates the host keys. ssh adds the ED25519 host key to root's `known_hosts`. PAM asks for test's password ("(test@127.0.0.1) Password:", keyboard-interactive), and the harness types it. It prints `uid=501(test) gid=20(staff)`.
- The same with a wrong password and one prompt: "Permission denied", status 255.
- root makes an ed25519 key with `ssh-keygen`, and copies the public key to `/Users/test/.ssh/authorized_keys`. `ssh -o BatchMode=yes` runs `echo key-$UID` as test (`key-501`), without a password.
- The crypto library: `openssl version` says "OpenSSL 3.5.9 29 Sep 2026 (Library: OpenSSL 3.5.9 29 Sep 2026)", and `ssh -V` names it. Known answers: `openssl dgst -sha256` of "abc" (FIPS 180-2's), the SHA-256 of four AES-128-ECB blocks of zeros under the key 00..0f (through the arm64 AES instructions), and `openssl dgst -provider legacy -md4` of "abc" (RFC 1320's), which dlopens the legacy provider under enforcement. root makes RSA and ECDSA keys too (`ssh-keygen -l` lists RSA, ECDSA and ED25519), adds them to test's `authorized_keys`, and logs in with each (`rsa-501`, `ecdsa-501`).
- `/var/log/sshd.log` has "Accepted keyboard-interactive/pam for test from 127.0.0.1", "PAM: authentication error for test from 127.0.0.1" and "Accepted publickey for test from 127.0.0.1".
- launchctl reports no loopback or socket errors (`--absent`).

It passes in about 25 seconds.

| Finding | Resolution |
|---|---|
| launchd-842 runs an `inetdCompatibility` job through `/usr/libexec/launchproxy` (`core.c`, `file2exec`), which wasn't built. launchctl, not launchd, creates the job's sockets | `//base:launchproxy` from 842's `support/launchproxy.c`, with launchd's patches. launchctl creates the `Sockets` descriptors (Network.swift) |
| sshd in inetd mode points standard error at `/dev/null` unless it logs to stderr (`-e`), and NeoDarwin's `syslog(3)` is standard error | the wrapper passes `-e -E /var/log/sshd.log` |
| network_cmds-726 names netem models that xnu-12377 lacks | network_cmds patch 0001 |
| Apple's OpenSSH links libresolv (SSHFP lookups through `res_9_query`), which the base didn't have | at first openssh patch 0003 (the lookups failed); since P1-19 checkpoint 2 libresolv-93 is built and OpenSSH links it (`docs/kernel/network.md`) |
| Seatbelt (`sandbox_init`) is closed | `SANDBOX_RLIMIT` for sshd-auth |
| `pam_sacl` denies everyone without a directory service (`mbr_check_service_membership` answers `EIO`) | left out of `pam.d/sshd` |
| Libinfo's `getaddrinfo` logs "si_destination_compare: send failed: Invalid argument" for each passive address (`::`, `0.0.0.0`) when launchctl creates the sockets. Its RFC 6724 sort asks the kernel's netsrc control for a route to the unspecified address | expected, as Libinfo's own comment says ("no route to host"). The order of passive sockets doesn't matter |
| `echo key-$((6*7))`, typed for the remote side, is a glob pattern in root's zsh ("no matches found") | the test sends `echo key-\$UID` |
| OpenSSL on Darwin seeds its DRBG from CommonCrypto's `CCRandomGenerateBytes` (`include/crypto/rand.h`), and the base has no libcommonCrypto | `-DOPENSSL_NO_APPLE_CRYPTO_RANDOM`: `getentropy(2)` |
| Apple's `config.h` answers OpenSSH's libcrypto checks for LibreSSL (`EVP_CIPHER_CTX_get_iv`/`_set_iv`, `EVP_MD_CTX_init`, `HMAC_CTX_init`), which OpenSSL 3 lacks | openssh patch 0003, from OpenSSH's configure run against OpenSSL 3.5.9 |
| OpenSSL records its compiler flags (`openssl version -f`), which would name the build sandbox | the build compiles through `../root` and `../sysroot` links |

**Security notes.**
- Remote Login is off by default (`Disabled`), as on macOS. `load -w` doesn't persist yet (no overrides database), so sshd is off again after a reboot.
- Root can't log in with a password (`PermitRootLogin prohibit-password`), and nobody can with an empty one.
- Passwords are SHA-512 crypt hashes at the default 5000 rounds ("passwd and chpass"); accounts made before keep their DES hashes until the password is changed. Keys are still the better way in.
- The pre-authentication sandbox is rlimit plus chroot and `_sshd`, weaker than macOS's Seatbelt profile.
- libcrypto is OpenSSL 3.5.9, a supported LTS release (since 2026-10-01; before, LibreSSL 3.3.6 from 2022, with unfixed advisories). Updates are patch releases of the 3.5 line, as FreeBSD takes them: a new pin in `MODULE.bazel` and `base/upstream.lock`. OpenSSH uses libcrypto only for its primitives (no TLS or X.509).
- Without a log store or BSM audit, logins over ssh are recorded only in `/var/log/sshd.log` and utmpx. Now that the base has libbsm (OpenBSM, above), OpenSSH's `USE_BSM_AUDIT` can come back.
- The wrapper creates the host keys on the first connection, as root, mode 0600.
