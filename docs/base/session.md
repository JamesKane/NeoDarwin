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
| login builds with PAM and BSM audit. PAM's macOS configuration needs `pam_opendirectory` (OpenDirectory and CoreFoundation, both closed) | at first login's own non-PAM path: `crypt()` against `pw_passwd`. Since OpenPAM (§3, PAM) it builds with `USE_PAM`. `login_audit.c` still compiles to nothing (no libbsm) |
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

Not in NeoDarwin's launchctl yet: binary plists, the overrides database (`load -w` doesn't persist), `/etc/rc.*`, loopback setup and `sysctl.conf`.

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

Not yet: locale data (zprofile sets `LANG=C.UTF-8`; zsh falls back to the C locale), `/usr/libexec/path_helper` and `/usr/bin/locale` (zprofile and zshrc skip them). PAM, mount and fsck are below.

### After P1-08: the shells

NeoDarwin has macOS's three shells:

| Shell | From | Line editing |
|---|---|---|
| `/bin/sh` | shell_cmds-326's FreeBSD ash, built as `sh.xcconfig` builds `/usr/local/bin/ash` | libedit: `set -o emacs` or `set -o vi`, history, `fc`; it reads `$ENV` |
| `/bin/zsh` | zsh-110.1.1 (zsh 5.9), root's login shell | zle |
| `/bin/bash` | bash-140 (bash 3.2.57, as macOS ships it) | its own readline, linked statically; termcap from libncurses |

- **libedit** (libedit-65, NetBSD libedit 20121213-3.0) installs as `/usr/lib/libedit.3.dylib`, with `install_misc.sh`'s names `libedit.2`, `libedit.3.0`, `libedit` and `libreadline` linked to it, as the SDK's `.tbd` files name it. It exports the SDK `libedit.3.tbd`'s 150 symbols, all of them, and links libncurses. Its headers (`histedit.h`, `editline/readline.h` and the `readline/` links) are build-only, as ncurses' are.
- **ash** links libedit as Apple's does. `mkbuiltins` now runs without `-h`, so `fc` is a builtin. Editing starts once the shell is interactive on a terminal and `set -o emacs` or `set -o vi` is given (FreeBSD's sh of 2017 enables neither by default).
- **bash** is built with the project's committed configuration: `config.h`, `pathnames.h`, `signames.h`, `syntax.c`, `version.h` and the generated `builtins/*.c`. Apple's build doesn't run configure, and neither does NeoDarwin's: it uses them as ncurses' `ncurses_cfg.h` is used. Only what the project generates is generated: `ostype.h` (`darwin25`, xnu-12377's release) and `parse.y` through the toolchain's yacc. It links its four static libraries (readline, glob, libsh, intl) and libncurses. `/etc/profile` and `/etc/bashrc` are Apple's, from bash-140's RC files phase. `profile` runs `/usr/libexec/path_helper` only if it is executable, so it's skipped while it doesn't exist.
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
| login | system_cmds-1039 | built with `USE_PAM`, still without `USE_BSM_AUDIT` |
| su | shell_cmds-326 (`//base:su_command`) | `/usr/bin/su`, setuid root |

Modules are `MH_DYLIB`s named `pam_NAME.so.2` in `/usr/lib/pam`. A policy names `pam_NAME.so`, and OpenPAM's loader tries the `.so.2` first. Not built, for their closed dependencies: `pam_opendirectory`, `pam_krb5`, `pam_ntlm` and `pam_mount` (OpenDirectory, CoreFoundation, Heimdal, GSS, NetFS), `pam_smartcard` (CryptoTokenKit), `pam_localauthentication`, `pam_tid` and `pam_aks` (LocalAuthentication, AppleKeyStore), and `pam_basesystem` (CoreFoundation).

**The password module is OpenPAM's `pam_unix`**, in place of macOS's `pam_opendirectory`. It compares `crypt(3)` of the typed password with `pw_passwd` from `getpwnam(3)`. login and su run as root, so Libinfo's file module answers from `/etc/master.passwd`. libc's `crypt` computes traditional DES hashes. `pam_unix` can't change a password (its `chauthtok` refuses), and its account check accepts everyone: the `change` and `expire` fields aren't enforced.

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
- Giving root a password (or `*`, as macOS does) needs `passwd(1)`, which isn't built.

**Code signing.** libpam loads every module with `dlopen`, and under enforcement the kernel refuses a library that the image's trust cache doesn't list (`docs/kernel/amfi-provider.md` §4). `//tools/trustcache` lists every signed Mach-O staged in an image, whatever its name, so libpam, the shim, the eleven modules, login and su are all in it. The test runs under enforcement and fails if `ndamfi` refuses anything.

**The test:** `//kernel:sbsa_pam_session_test` boots `//images:pam_session_root`, which is `session_root` plus two test accounts from `tests/qemu/pam`. `test` (uid 501, group `admin`) and `guest` (uid 502) have the password `neodarwin` and home directories under `/Users` that they own. The system's own root has no such accounts. The test checks, in order:
- A wrong password is refused ("Login incorrect"). `test` logs in with its password.
- `su - root -c id` makes `test` root without a password (`uid=0(root)`, status 0).
- As root it writes `/etc/nologin`. `pam_nologin` shows the file and refuses `guest`. root still logs in, with no password, after seeing it, and removes it.
- `guest` logs in, and su refuses it (`su: Sorry`, status 1): it is in neither `admin` nor `wheel`.

`//kernel:sbsa_session_boot_test` still logs in as root, now through PAM.

| Finding | Resolution |
|---|---|
| The first non-root login panicked the kernel: launchd, PID 1, was killed by `EXC_GUARD` (`kGUARD_EXC_DESCRIPTOR_VIOLATION`). launchd-842 answers a non-root client's lookup with `VPROC_ERR_TRY_PER_USER`, and starts a per-user launchd for that user. The two exchange jobs and ports in MIG messages with out-of-line arrays of moved send rights (`job.defs`: `take_subset`, `lookup_children`). xnu-12377 refuses those from a platform binary (`IPC_POLICY_ENHANCED_V2`) with a fatal guard exception. With the boot-arg `ool_port_array_enforced=0`, PID 1 survives and the per-user launchd it starts dies instead | launchd patch 0005 adds `HAVE_PER_USER_LAUNCHD`, built 0: PID 1 serves every user, as embedded launchd does |
| hdiutil records the build user (uid 501) as every file's owner and drops set-user-ID bits. su ran as uid 501 ("su: not running setuid"), and a user with uid 501 owned every system file | `//tools/hfsowners` (Swift) rewrites the image's catalog after hdiutil: every file and folder is root:wheel, every `files`/`tree_modes` mode is set again, and `hfs_ramdisk`'s new `owners` gives single paths (home directories) to their users. This applies to every image |
| `pam_unix` compares `crypt()` of the typed password with `pw_passwd`, so an empty password never matches | OpenPAM patch 0001: `nullok` |
| With `USE_PAM`, login leaves the root-terminal check to the policy (FreeBSD's `pam_securetty`), which macOS doesn't have: its root has no password | system_cmds patch 0001: the check runs with PAM too |
| `pam_group` asks `mbr_check_membership(3)`, which answers `EIO` without Open Directory (Libinfo's `DS_AVAILABLE` is off), so no one was a member and su refused everyone | pam_modules patch 0001: when the call fails, check `/etc/group` as the module's FreeBSD code does (primary group or member list) |
| `pam_uwtmp` can't write utmpx on a read-only root: under the test PID 1 that runs getty itself (`//images:getty_root`), `pam_open_session` failed and login exited | `pam_uwtmp` is optional, as a `pututxline` failure was to login's non-PAM path |
| A policy without a session chain falls back to `other`'s `pam_deny`, so dropping `pam_launchd` alone would make `su -` fail to open its session | su's session is `pam_permit` |
| su.c includes the private `<SoftLinking/SoftLinking.h>` (for libEndpointSecuritySystem, closed) and libsystem_sandbox's private `<rootless.h>` | `base/shell_cmds/compat`: a SoftLinking that resolves the library with `dlopen` (not found), and `rootless_restricted_environment()` answering 0 |
| libpam's public headers are in the SDK | installed from OpenPAM's tree, build-only, for login, su and the modules. They aren't in the sysroot: nothing in libSystem includes them |

Not yet: `passwd(1)` and `chpass(1)` (password changes), BSM audit (libbsm), sshd and its policy, per-user launchd sessions (`pam_launchd`), and account expiry.
