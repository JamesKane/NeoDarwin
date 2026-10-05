#!/usr/bin/env bash
# SPDX-License-Identifier: BSD-2-Clause
# ps, stty and tty from adv_cmds-237 (docs/base/libsystem.md): replays
# adv_cmds.xcodeproj's ps, stty and tty targets with the project's settings
# (GCC_NO_COMMON_BLOCKS, DEAD_CODE_STRIPPING, VERSION_INFO_PREFIX __), and
# the Desktop target's install-ps.sh, which installs ps (SKIP_INSTALL in its
# target) as /bin/ps.
#   build.sh OUT ADV_CMDS_SRC SYSROOT DEPROOT...   (DEPROOT: //base:root, //base:libxo_dylib)
# OUT receives bin/{ps,stty} and usr/bin/{tty,pkill,pgrep}, and for P4-21
# usr/bin/{finger,gencat,last,lsvfs,whois,locale,localedef}.
# pkill (and pgrep, its variant link) reads the process table through
# libsysmon, which is closed and asks sysmond over XPC; compat/ has the
# sysmon.h and xpc/xpc.h subset it uses and nd_sysmon.c, which answers from
# sysctl(3) (KERN_PROC_ALL, KERN_PROCARGS2), linked into pkill.
# Apple installs ps setuid root (mode 4755) with its entitlements
# (PS_ENTITLED); NeoDarwin has no code-signing policy until P1-15, and the
# image rule sets modes.
source "$(dirname "$0")/../../tools/base/common.sh"
source "$(dirname "$0")/../commands.sh"
OUT="$(abspath "$1")"; A="$(abspath "$2")"; SYSROOT="$(abspath "$3")"; shift 3
DEPS=(); for d in "$@"; do DEPS+=("$(abspath "$d")"); done
ROOT="$(find_root "${DEPS[@]}")"
XO=""; for d in "${DEPS[@]}"; do [ -f "$d/usr/lib/libxo.dylib" ] && XO="$d"; done
[ -n "$XO" ] || { echo "adv_cmds: no DEPROOT holds usr/lib/libxo.dylib (pass //base:libxo_dylib)" >&2; exit 1; }
PROJ="$(cd "$(dirname "$0")" && pwd)"
B="$(mktemp -d)"; trap 'rm -rf "$B"' EXIT
A="$(stage_src "$A" "$B/src" "$PROJ/patches")"   # patches/ applied
cd "$A"
D="$B/derived"; mkdir -p "$D"

# Warning flags change no interface and are left out; ps and tty add
# __FBSDID=__RCSID, ps OTHER_CFLAGS -DPS_ENTITLED.
base=("${TARGET_FLAGS[@]}" -Os -fno-common $(cmd_sysroot_flags "$SYSROOT"))
vers() { write_vers "$D/${1}_vers.c" "$1" adv_cmds 237 __; printf '%s' "$D/${1}_vers.c"; }
write_rsp "$B/ps.rsp" "${base[@]}" -D__FBSDID=__RCSID -DPS_ENTITLED
write_rsp "$B/stty.rsp" "${base[@]}"
write_rsp "$B/tty.rsp" "${base[@]}" -D__FBSDID=__RCSID

tool "$B" "$ROOT" "$OUT/bin/ps" "$B/ps.rsp" ps/fmt.c ps/keyword.c ps/nlist.c ps/print.c ps/ps.c ps/tasks.c "$(vers ps)"
tool "$B" "$ROOT" "$OUT/bin/stty" "$B/stty.rsp" stty/cchar.c stty/gfmt.c stty/key.c stty/modes.c stty/print.c \
	stty/stty.c stty/util.c "$(vers stty)"
tool "$B" "$ROOT" "$OUT/usr/bin/tty" "$B/tty.rsp" tty/tty.c "$(vers tty)"

# pkill: INSTALL_PATH /usr/bin, __FBSDID=__RCSID as ps and tty; its
# entitlements are for sysmond. The pgrep target's variant_links.sh makes
# pgrep a hard link (a copy here).
write_rsp "$B/pkill.rsp" "${base[@]}" -D__FBSDID=__RCSID -I"$PROJ/compat"
tool "$B" "$ROOT" "$OUT/usr/bin/pkill" "$B/pkill.rsp" pkill/pkill.c "$PROJ/compat/nd_sysmon.c" "$(vers pkill)"
cp "$OUT/usr/bin/pkill" "$OUT/usr/bin/pgrep"

# P4-21's targets, INSTALL_PATH /usr/bin. finger and gencat add
# __FBSDID=__RCSID; last links libxo (libxo.tbd).
tool "$B" "$ROOT" "$OUT/usr/bin/finger" "$B/tty.rsp" finger/finger.c finger/lprint.c finger/net.c finger/sprint.c \
	finger/util.c "$(vers finger)"
tool "$B" "$ROOT" "$OUT/usr/bin/gencat" "$B/tty.rsp" gencat/gencat.c gencat/genlib.c "$(vers gencat)"
tool "$B" "$ROOT" "$OUT/usr/bin/lsvfs" "$B/stty.rsp" lsvfs/lsvfs.c "$(vers lsvfs)"
tool "$B" "$ROOT" "$OUT/usr/bin/whois" "$B/stty.rsp" whois/whois.c "$(vers whois)"
write_rsp "$B/last.rsp" "${base[@]}" -I"$XO/usr/local/include"
tool "$B" "$ROOT" "$OUT/usr/bin/last" "$B/last.rsp" last/last.c "$(vers last)" -- -L"$XO/usr/lib" -lxo
# locale is C++ (locale.cc), linked with libc++ (headers from the sysroot,
# ahead of the C ones). It reads the locale data in /usr/share/locale,
# which the base doesn't install yet (P4-21's third checkpoint): until then
# it knows the C and POSIX locales.
write_rsp "$B/locale.rsp" "${TARGET_FLAGS[@]}" -Os -fno-common -nostdinc++ -isystem "$SYSROOT/usr/include/c++/v1" \
	$(cmd_sysroot_flags "$SYSROOT")
compile "$B/obj/locale_cc" "$B/locale.rsp" locale/locale.cc
tool "$B" "$ROOT" "$OUT/usr/bin/locale" "$B/stty.rsp" "$(vers locale)" -- "$B"/obj/locale_cc/*.o -lc++
# localedef: its target's settings (gnu17; OTHER_CFLAGS -I localedef/libc,
# -I localedef), and its script phase's parser.c and parser.h from
# parser.y with bison -d (the toolchain's yacc here, as find's getdate.y).
xcrun yacc -d -o "$D/parser.c" localedef/parser.y
write_rsp "$B/localedef.rsp" "${base[@]}" -std=gnu17 -I"$A/localedef/libc" -I"$A/localedef" -I"$D"
tool "$B" "$ROOT" "$OUT/usr/bin/localedef" "$B/localedef.rsp" localedef/scanner.c localedef/charmap.c "$D/parser.c" \
	localedef/collate.c localedef/ctype.c localedef/localedef.c localedef/messages.c localedef/monetary.c \
	localedef/numeric.c localedef/time.c localedef/wide.c "$(vers localedef)"
