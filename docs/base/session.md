<!-- SPDX-License-Identifier: BSD-2-Clause -->
# An interactive session over serial (P1-08)

**Goal.** launchd-842 is PID 1 on the HFS+ root. It starts getty on the console from a LaunchDaemon plist, and getty, login and a shell give an interactive session over serial. Everything is built from Apple's source, on the userland base of P1-08b (`libsystem.md`), except the parts Apple doesn't publish.

## 1. Decisions

- **PID 1 is launchd-842** (user, 2026-09-29). It is the last open-source launchd, ported to xnu-12377 with its libxpc-300 XPC-domain code compiled out; that code's only clients are closed. Its client library (liblaunch: `bootstrap_*`, `vproc_*`, `launch_msg`) goes into NeoDarwin's libxpc stand-in. That way asl, notify, Libc and libdyld talk to a real bootstrap server, over launchd-842's own MIG interface (`job.defs`).
- **launchctl is a small first-party Swift tool** (user, 2026-09-29). launchctl-842 parses job plists with CoreFoundation, including private CF headers, while launchd itself needs no CF. NeoDarwin's launchctl parses XML plists itself, builds `launch_data` and calls `launch_msg`, as launchctl does for `bootstrap` and `load`. No CoreFoundation or ICU enters the base.
- **/bin/sh is FreeBSD ash from shell_cmds.** Apple's `/bin/sh` (dash-16) isn't published, and shell_cmds builds ash as `/usr/local/bin/ash`. zsh and bash need ncurses, and come later.
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
| Apple's `/bin/sh` is closed (dash-16); shell_cmds' `sh` is FreeBSD ash with libedit, installed as `/usr/local/bin/ash` | ash installed as `/bin/sh`, built as FreeBSD builds it without libedit (`-DNO_HISTORY`, `mkbuiltins -h`), with `mknodes` and `mksyntax` built for the host. There's no line editing or history yet |
| login builds with PAM and BSM audit. PAM's macOS configuration needs `pam_opendirectory` (OpenDirectory and CoreFoundation, both closed) | login's own non-PAM path: `crypt()` against `pw_passwd`. `login_audit.c` compiles to nothing. OpenPAM with the libSystem-only modules is later work |
| ls builds only with `COLORLS` (libcurses): `unix2003_compat` is declared inside the `COLORLS` block | file_cmds patch 0001 moves the declaration; `ls -G` is accepted and ignored |
| wc and others link libxo, which Apple doesn't publish | FreeBSD's `contrib/libxo`, at the commit libm uses, with Apple's install name. Patch 0001 configures it for Darwin |
| libutil's `tzlink` calls tzlinkd over XPC | libutil patch 0001 takes the simulator's `ENOTSUP` path |
| The sysroot's private `<sys/ioctl.h>` reaches `<sys/param.h>`, whose `BSD` macro breaks stty | command projects search `usr/local/include` and `usr/include` only, as a project outside libSystem does against Apple's internal SDK |
| Libinfo's file module reads `/etc/master.passwd` for root and `/etc/passwd` otherwise; there's no `pwd.db` | NeoDarwin writes `master.passwd` and `group` (`base/etc`); `passwd` is generated from them. root has an empty password, so login asks for none |
| launchd-842 doesn't read `/etc/ttys`; login only checks its console entry's `secure` flag | `ttys` is installed unmodified; getty runs from `com.apple.getty.plist` (system_cmds), installed enabled |

Still to check on the kernel: `ps` (`KERN_PROC`, `proc_pidinfo`, `task_read_for_pid`), file ownership on the image (hdiutil records the build user's uid), and `/usr/bin/false` (the shell of `daemon` and `nobody`), which isn't built yet.

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
| The kernel mounts the root read-only. PID 1 launchd creates its socket under `/var/tmp/launchd` only when launchctl first asks for it, and `EROFS` there fails silently, so `launch_msg` answered `ENOTCONN` | `launchctl bootstrap` remounts `/` read-write first, as launchctl-842 does with `mount -uw /`. The image has `/private/var/tmp` (1777), as macOS does |
| The kernel names the root's device `root_device` (`vfs_rootmountalloc`), which no path reaches, and an update mount looks its device path up | launchctl finds the `/dev` block device whose device number is the root's (`/dev/md0`) and passes HFS's mount arguments (`hfs_mount.h`, not in the SDK, declared in `launch_shim.h`) |
| macOS's `<launch.h>` marks launch_msg deprecated, and it's part of the SDK's Darwin module, so the marking can't be undone for one import | `launch_shim.h` declares the calls as 842's own `launch.h` does |
| Embedded Swift has no `CommandLine`; String comparison needs the Unicode tables | a `@_cdecl("main")` entry; `rules/darwin_executable.bzl` links the Embedded stdlib's `libswiftUnicodeDataTables.a`, which dead-stripping trims to what's used |

Not in NeoDarwin's launchctl yet: binary plists, the overrides database (`load -w` doesn't persist), fsck, `/etc/rc.*`, loopback setup and `sysctl.conf`. Of the other session pieces, zsh and bash (ncurses, libedit), line editing in `/bin/sh`, PAM and the `mount`/fsck tools (diskdev_cmds) come later.

