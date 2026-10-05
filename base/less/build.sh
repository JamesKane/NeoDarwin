#!/usr/bin/env bash
# SPDX-License-Identifier: BSD-2-Clause
# less, lessecho and lesskey from less-50 (less 661; P4-21 checkpoint 3,
# docs/architecture/freebsd-parity.md §2.1): replays less.xcodeproj's three
# targets with BSD.xcconfig's settings (VERSIONING_SYSTEM apple-generic,
# VERSION_INFO_PREFIX __, DEAD_CODE_STRIPPING), the project's gnu99 and
# HEADER_SEARCH_PATHS $(SRCROOT) (Apple's configured defines.h) and less's
# SYSDIR and BINDIR definitions, and the "default" target's script phase:
# more is a hard link to less.
#   build.sh OUT LESS_SRC SYSROOT DEPROOT...   (DEPROOT: //base:root, //base:libncurses_dylib)
# OUT receives usr/bin/{less,more,lessecho,lesskey} and their pages in
# usr/share/man/man1 (uncompressed: man(1) reads either).
source "$(dirname "$0")/../../tools/base/common.sh"
source "$(dirname "$0")/../commands.sh"
OUT="$(abspath "$1")"; L="$(abspath "$2")"; SYSROOT="$(abspath "$3")"; shift 3
DEPS=(); for d in "$@"; do DEPS+=("$(abspath "$d")"); done
ROOT="$(find_root "${DEPS[@]}")"
NC=""; for d in "${DEPS[@]}"; do [ -f "$d/usr/lib/libncurses.5.4.dylib" ] && NC="$d"; done
[ -n "$NC" ] || { echo "less: no DEPROOT holds usr/lib/libncurses.5.4.dylib (pass //base:libncurses_dylib)" >&2; exit 1; }
PROJ="$(cd "$(dirname "$0")" && pwd)"
B="$(mktemp -d)"; trap 'rm -rf "$B"' EXIT
L="$(stage_src "$L" "$B/src" "$PROJ/patches")"   # patches/ (none) applied
cd "$L/less"
D="$B/derived"; mkdir -p "$D"

vers() { write_vers "$D/${1}_vers.c" "$1" less 50 __; printf '%s' "$D/${1}_vers.c"; }
base=("${TARGET_FLAGS[@]}" -Os -std=gnu99 -I"$L" $(cmd_sysroot_flags "$SYSROOT"))
write_rsp "$B/cflags" "${base[@]}"
write_rsp "$B/less.rsp" "${base[@]}" '-DSYSDIR=\"/etc\"' '-DBINDIR=\"/usr/bin\"'

tool "$B" "$ROOT" "$OUT/usr/bin/less" "$B/less.rsp" pattern.c cvt.c brac.c ch.c xbuf.c charset.c cmdbuf.c \
	command.c decode.c edit.c filename.c lesskey_parse.c forwback.c help.c ifile.c input.c jump.c line.c \
	linenum.c lsystem.c main.c mark.c optfunc.c option.c opttbl.c os.c output.c position.c prompt.c \
	screen.c search.c signal.c tags.c evar.c ttyin.c version.c "$(vers less)" -- -L"$NC/usr/lib" -lncurses
tool "$B" "$ROOT" "$OUT/usr/bin/lesskey" "$B/cflags" xbuf.c version.c lesskey.c lesskey_parse.c "$(vers lesskey)"
tool "$B" "$ROOT" "$OUT/usr/bin/lessecho" "$B/cflags" lessecho.c "$(vers lessecho)"
ln "$OUT/usr/bin/less" "$OUT/usr/bin/more"
mkdir -p "$OUT/usr/share/man/man1"
install -m 0444 "$L/less.1" "$L/lessecho.1" "$L/lesskey.1" "$OUT/usr/share/man/man1/"
ln "$OUT/usr/share/man/man1/less.1" "$OUT/usr/share/man/man1/more.1"
