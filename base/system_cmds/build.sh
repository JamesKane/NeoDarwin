#!/usr/bin/env bash
# SPDX-License-Identifier: BSD-2-Clause
# getty, login, sysctl, passwd, chpass and pwd_mkdb from system_cmds-1039
# (docs/base/libsystem.md): replays system_cmds.xcodeproj's targets with the
# project's base.xcconfig (gnu99, GCC_SYMBOLS_PRIVATE_EXTERN, its OTHER_CFLAGS).
#   build.sh OUT SYSTEM_CMDS_SRC SYSROOT DEPROOT...   (DEPROOT: //base:root, //base:libpam_dylib, //base:bsm_audit,
#                                                      //base:libutil_dylib)
# OUT receives usr/libexec/getty, usr/bin/login and its PAM policies
# private/etc/pam.d/{login,login.term}, usr/sbin/sysctl (the
# target's one source, no frameworks; the SMP boot test reads hw.ncpu with
# it, P1-06) and getty's launchd job,
# System/Library/LaunchDaemons/com.apple.getty.plist (getty std.9600 on the
# console). generate_plist.sh adds Disabled = true on macOS, where
# loginwindow owns the console; NeoDarwin installs it as the embedded
# platforms do, enabled. Also usr/bin/{passwd,chpass,chfn,chsh} and
# usr/sbin/pwd_mkdb (below), bin/sync and usr/bin/getconf (the ZFS test
# suite's), and P4-21's sbin/{dmesg,reboot,halt,shutdown},
# usr/bin/{pagesize,gcore} and usr/sbin/{ac,accton,sa,zic,zdump} (at the
# end). Not iostat: it needs IOKit.framework and CoreFoundation, which the
# base doesn't have yet (docs/architecture/freebsd-parity.md §2.1).
# On macOS, login authenticates through PAM and records BSM audit and
# EndpointSecurity events (USE_PAM, USE_BSM_AUDIT; -lpam -lbsm, weak
# libEndpointSecuritySystem). NeoDarwin builds it with USE_PAM against
# OpenPAM (//base:libpam_dylib; docs/base/session.md, "PAM") and with
# USE_BSM_AUDIT against OpenBSM's libbsm (//base:bsm_audit; "BSM audit"):
# login_audit.c records AUE_login and AUE_logout. compat/ has the private
# headers it includes for its EndpointSecurity events, whose library is
# closed: the weak symbols are null, as when the library is absent. Patch
# 0001 keeps the root-terminal check on the PAM path; 0002 makes the target's
# pam.d policies use pam_unix in place of pam_opendirectory (closed). Apple
# installs login setuid root (INSTALL_MODE_FLAG u+s); the image rule sets
# modes.
source "$(dirname "$0")/../../tools/base/common.sh"
source "$(dirname "$0")/../commands.sh"
OUT="$(abspath "$1")"; S="$(abspath "$2")"; SYSROOT="$(abspath "$3")"; shift 3
DEPS=(); for d in "$@"; do DEPS+=("$(abspath "$d")"); done
ROOT="$(find_root "${DEPS[@]}")"
PAM=""; for d in "${DEPS[@]}"; do [ -f "$d/usr/lib/libpam.2.dylib" ] && PAM="$d"; done
[ -n "$PAM" ] || { echo "system_cmds: no DEPROOT holds usr/lib/libpam.2.dylib (pass //base:libpam_dylib)" >&2; exit 1; }
BSM=""; for d in "${DEPS[@]}"; do [ -f "$d/usr/lib/libbsm.0.dylib" ] && BSM="$d"; done
[ -n "$BSM" ] || { echo "system_cmds: no DEPROOT holds usr/lib/libbsm.0.dylib (pass //base:bsm_audit)" >&2; exit 1; }
UTIL=""; for d in "${DEPS[@]}"; do [ -f "$d/usr/lib/libutil.dylib" ] && UTIL="$d"; done
[ -n "$UTIL" ] || { echo "system_cmds: no DEPROOT holds usr/lib/libutil.dylib (pass //base:libutil_dylib)" >&2; exit 1; }
PROJ="$(cd "$(dirname "$0")" && pwd)"
B="$(mktemp -d)"; trap 'rm -rf "$B"' EXIT
S="$(stage_src "$S" "$B/src" "$PROJ/patches")"   # patches/ applied
cd "$S"

# base.xcconfig: GCC_C_LANGUAGE_STANDARD, GCC_SYMBOLS_PRIVATE_EXTERN,
# OTHER_CFLAGS (XPC_BUILD_OTHER_CFLAGS for the current major release),
# HEADER_SEARCH_PATHS (the project directory; the rest don't exist here).
# Warning flags change no interface and are left out.
write_rsp "$B/cflags" "${TARGET_FLAGS[@]}" -Os -std=gnu99 -fno-common -fvisibility=hidden \
	-DHAVE_KDEBUG_TRACE=1 -DCONFIG_EMULATE_XNU_INITPROC_SELECTION=0 -DHAVE_GALARCH_AVAILABILITY=1 \
	-D__XPC_PROJECT_BUILD__=1 -I"$S" $(sysroot_flags "$SYSROOT")

tool "$B" "$ROOT" "$OUT/usr/libexec/getty" "$B/cflags" getty/chat.c getty/init.c getty/main.c getty/subr.c
# login: its Release settings' USE_PAM, USE_BSM_AUDIT, -lpam and -lbsm, for
# the macosx SDK.
{ cat "$B/cflags"; printf '%s\n' -DUSE_PAM -DUSE_BSM_AUDIT -I"$PROJ/compat" -I"$PAM/usr/local/include" \
	-I"$BSM/usr/local/include"; } > "$B/login.rsp"
tool "$B" "$ROOT" "$OUT/usr/bin/login" "$B/login.rsp" login/login.c login/login_audit.c -- -L"$PAM/usr/lib" -lpam \
	-L"$BSM/usr/lib" -lbsm
tool "$B" "$ROOT" "$OUT/usr/sbin/sysctl" "$B/cflags" sysctl/sysctl.c
mkdir -p "$OUT/System/Library/LaunchDaemons"
install -m 0644 getty/com.apple.getty.plist "$OUT/System/Library/LaunchDaemons/"
# The login target's copy phase: pam.d/login and login.term in /etc/pam.d.
mkdir -p "$OUT/private/etc/pam.d"
install -m 0644 login/pam.d/login login/pam.d/login.term "$OUT/private/etc/pam.d/"

# Accounts (docs/base/session.md, "passwd and chpass"): passwd, chpass (with
# its chfn and chsh links) and pwd_mkdb, on /etc/master.passwd alone. With
# PW_FILES (patches 0003 and 0004), passwd builds its file backend only
# (no Open Directory, PAM or NIS sources or frameworks) and chpass its
# FreeBSD file path, with vipw's pw_util.c and pwd_mkdb's pw_scan.c, in
# place of open_directory.c (CoreFoundation, OpenDirectory). chpass runs
# pwd_mkdb -p, which installs the new master.passwd and writes /etc/passwd.
# pwd_mkdb: its target's GCC_PREPROCESSOR_DEFINITIONS (in the response
# file's shell quoting). The image makes
# passwd, chpass, chfn and chsh setuid root.
{ cat "$B/cflags"; printf '%s\n' -DPW_FILES; } > "$B/passwd.rsp"
tool "$B" "$ROOT" "$OUT/usr/bin/passwd" "$B/passwd.rsp" passwd/passwd.c passwd/file_passwd.c
{ cat "$B/cflags"; printf '%s\n' -DPW_FILES -I"$S/vipw" -I"$S/pwd_mkdb"; } > "$B/chpass.rsp"
tool "$B" "$ROOT" "$OUT/usr/bin/chpass" "$B/chpass.rsp" chpass/chpass.c chpass/edit.c chpass/field.c \
	chpass/table.c chpass/util.c vipw/pw_util.c pwd_mkdb/pw_scan.c
# The chfn and chsh targets: hard links, which the install tree holds as copies.
cp "$OUT/usr/bin/chpass" "$OUT/usr/bin/chfn"; cp "$OUT/usr/bin/chpass" "$OUT/usr/bin/chsh"
{ cat "$B/cflags"; printf '%s\n' -D_PW_NAME_LEN=MAXLOGNAME "-D_PW_YPTOKEN='\"__YP!\"'"; } > "$B/pwd_mkdb.rsp"
tool "$B" "$ROOT" "$OUT/usr/sbin/pwd_mkdb" "$B/pwd_mkdb.rsp" pwd_mkdb/pwd_mkdb.c pwd_mkdb/pw_scan.c

# sync (INSTALL_PATH /bin) and getconf (/usr/bin, APPLE_GETCONF_UNDERSCORE):
# the ZFS test suite's (docs/architecture/filesystems.md §7). getconf's
# script phase turns each *.gperf table into C with fake-gperf.awk, run by
# the build machine's awk as Xcode runs it.
tool "$B" "$ROOT" "$OUT/bin/sync" "$B/cflags" sync/sync.c
G="$B/getconf"; mkdir -p "$G"
for t in confstr limits pathconf progenv sysconf unsigned_limits; do
	LC_ALL=C /usr/bin/awk -f getconf/fake-gperf.awk "getconf/$t.gperf" > "$G/$t.c"
done
{ cat "$B/cflags"; printf '%s\n' -DAPPLE_GETCONF_UNDERSCORE -iquote "$S/getconf"; } > "$B/getconf.rsp"
tool "$B" "$ROOT" "$OUT/usr/bin/getconf" "$B/getconf.rsp" getconf/getconf.c "$G/confstr.c" "$G/limits.c" \
	"$G/pathconf.c" "$G/progenv.c" "$G/sysconf.c" "$G/unsigned_limits.c"

# P4-21's leaf tools (docs/architecture/freebsd-parity.md §2.1), each its
# target's sources with base.xcconfig's flags and the target's settings.
# dmesg, ac, accton: one source each (INSTALL_PATH /sbin, /usr/sbin).
tool "$B" "$ROOT" "$OUT/sbin/dmesg" "$B/cflags" dmesg/dmesg.c
tool "$B" "$ROOT" "$OUT/usr/sbin/ac" "$B/cflags" ac/ac.c
tool "$B" "$ROOT" "$OUT/usr/sbin/accton" "$B/cflags" accton/accton.c
# sa: GCC_PREPROCESSOR_DEFINITIONS AHZV1=64.
{ cat "$B/cflags"; printf '%s\n' -DAHZV1=64; } > "$B/sa.rsp"
tool "$B" "$ROOT" "$OUT/usr/sbin/sa" "$B/sa.rsp" sa/db.c sa/main.c sa/pdb.c sa/usrdb.c
# zic and zdump: OTHER_CFLAGS -include tzconfig.h, HEADER_SEARCH_PATHS zic/.
{ cat "$B/cflags"; printf '%s\n' -I"$S/zic" -include tzconfig.h; } > "$B/tz.rsp"
tool "$B" "$ROOT" "$OUT/usr/sbin/zic" "$B/tz.rsp" zic/zic.c
tool "$B" "$ROOT" "$OUT/usr/sbin/zdump" "$B/tz.rsp" zdump/zdump.c
# pagesize: an aggregate target whose script phase installs pagesize.sh as
# /usr/bin/pagesize.
install -m 0755 pagesize/pagesize.sh "$OUT/usr/bin/pagesize"
# gcore: SYSTEM_HEADER_SEARCH_PATHS System.framework's PrivateHeaders,
# FRAMEWORK_SEARCH_PATHS Kernel.framework's PrivateHeaders; links libutil.
# C11, not base.xcconfig's gnu99: notes.c uses static_assert, which the
# headers define from C11 on. Patch 0007 drops its os_log hook;
# compat/responsibility.h stands in for libquarantine's responsibility SPI
# (closed): each process is its own responsible process.
{ cat "$B/cflags"; printf '%s\n' -iframework "$SYSROOT/System/Library/Frameworks/Kernel.framework/PrivateHeaders" \
	-isystem "$SYSROOT/System/Library/Frameworks/System.framework/PrivateHeaders" -I"$UTIL/usr/local/include" \
	-I"$PROJ/compat" -std=gnu11; } > "$B/gcore.rsp"
tool "$B" "$ROOT" "$OUT/usr/bin/gcore" "$B/gcore.rsp" gcore/notes.c gcore/vanilla.c gcore/utils.c gcore/corefile.c \
	gcore/vm.c gcore/main.c gcore/sparse.c gcore/threads.c -- -L"$UTIL/usr/lib" -lutil

# reboot and shutdown (INSTALL_PATH /sbin). Patches 0005 and 0006 drop
# kextmanager.defs (kextd's reboot lock: NeoDarwin has no kextd) and
# shutdown -s (IOPMLib), and call launchd-842's reboot2() for reboot3();
# with them neither needs IOKit or CoreFoundation. halt is reboot under
# another name (the halt target's hard link, a copy here). shutdown links
# libbsm (AUE_shutdown).
tool "$B" "$ROOT" "$OUT/sbin/reboot" "$B/cflags" reboot/reboot.c
cp "$OUT/sbin/reboot" "$OUT/sbin/halt"
{ cat "$B/cflags"; printf '%s\n' -I"$BSM/usr/local/include"; } > "$B/shutdown.rsp"
tool "$B" "$ROOT" "$OUT/sbin/shutdown" "$B/shutdown.rsp" shutdown/shutdown.c -- -L"$BSM/usr/lib" -lbsm
