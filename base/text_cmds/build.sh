#!/usr/bin/env bash
# SPDX-License-Identifier: BSD-2-Clause
# The session's text commands from text_cmds-197 (docs/base/libsystem.md):
# replays text_cmds.xcodeproj's cat, head, wc, sed, cut, sort, uniq, tr,
# tail and grep targets with the project's settings and
# xcconfigs/base.xcconfig (gnu99, __FBSDID=__RCSID, DEAD_CODE_STRIPPING,
# VERSION_INFO_PREFIX __; INSTALL_PATH /usr/bin, /bin for cat).
#   build.sh OUT TEXT_CMDS_SRC SYSROOT DEPROOT...   (DEPROOT: //base:root, //base:libxo, //base:libutil,
#                                                    //base:libncurses_dylib, //base:libmd_dylib,
#                                                    //base:libz_dylib, //base:bzip2_commands,
#                                                    //base:xz_commands)
# OUT receives bin/cat and usr/bin/{head,wc,sed,cut,sort,uniq,tr,tail,grep,
# egrep,fgrep,zgrep,zegrep,zfgrep,bzgrep,bzegrep,bzfgrep}, and the
# project's other targets (P4-21): bin/ed,
# sbin/md5 with md5_variant_links.sh's names, usr/bin/bintrans with the
# "Install bintrans links" names, and usr/bin/{banner,col,colrm,column,comm,
# csplit,expand,fmt,fold,join,lam,look,nl,paste,pr,rev,rs,split,ul,
# unexpand,unvis,vis}. Not jq, which macOS 26 adds to text_cmds and FreeBSD
# has as a port.
# wc links libxo (FreeBSD's; base/libxo), tail libutil (expand_number(3)).
# grep links libbz2, liblzma and libz (grep.xcconfig), for compressed
# input (-Z, -J, --xz, --lzma and the z and bz names), and
# grep_variant_links.sh's names are installed: egrep, fgrep, zgrep, zegrep,
# zfgrep, bzgrep, bzegrep, bzfgrep.
source "$(dirname "$0")/../../tools/base/common.sh"
source "$(dirname "$0")/../commands.sh"
OUT="$(abspath "$1")"; T="$(abspath "$2")"; SYSROOT="$(abspath "$3")"; shift 3
DEPS=(); for d in "$@"; do DEPS+=("$(abspath "$d")"); done
ROOT="$(find_root "${DEPS[@]}")"
XO=""; for d in "${DEPS[@]}"; do [ -f "$d/usr/lib/libxo.dylib" ] && XO="$d"; done
[ -n "$XO" ] || { echo "text_cmds: no DEPROOT holds usr/lib/libxo.dylib (pass //base:libxo)" >&2; exit 1; }
NC=""; for d in "${DEPS[@]}"; do [ -f "$d/usr/lib/libncurses.5.4.dylib" ] && NC="$d"; done
[ -n "$NC" ] || { echo "text_cmds: no DEPROOT holds usr/lib/libncurses.5.4.dylib (pass //base:libncurses_dylib)" >&2; exit 1; }
ZL=""; BZ=""; LZ=""
for d in "${DEPS[@]}"; do
	[ -f "$d/usr/lib/libz.1.dylib" ] && ZL="$d"; [ -f "$d/usr/lib/libbz2.1.0.dylib" ] && BZ="$d"
	[ -f "$d/usr/lib/liblzma.5.dylib" ] && LZ="$d"
done
[ -n "$ZL" ] && [ -n "$BZ" ] && [ -n "$LZ" ] ||
	{ echo "text_cmds: grep needs //base:libz_dylib, //base:bzip2_commands and //base:xz_commands" >&2; exit 1; }
MD=""; for d in "${DEPS[@]}"; do [ -f "$d/usr/lib/libmd.dylib" ] && MD="$d"; done
[ -n "$MD" ] || { echo "text_cmds: no DEPROOT holds usr/lib/libmd.dylib (pass //base:libmd_dylib)" >&2; exit 1; }
UTIL=""; for d in "${DEPS[@]}"; do [ -f "$d/usr/lib/libutil.dylib" ] && UTIL="$d"; done
[ -n "$UTIL" ] || { echo "text_cmds: no DEPROOT holds usr/lib/libutil.dylib (pass //base:libutil)" >&2; exit 1; }
PROJ="$(cd "$(dirname "$0")" && pwd)"
B="$(mktemp -d)"; trap 'rm -rf "$B"' EXIT
T="$(stage_src "$T" "$B/src" "$PROJ/patches")"   # patches/ applied
cd "$T"
D="$B/derived"; mkdir -p "$D"

# Warning flags change no interface and are left out.
base=("${TARGET_FLAGS[@]}" -Os -std=gnu99 -fno-common -D__FBSDID=__RCSID)
write_rsp "$B/cflags" "${base[@]}" $(cmd_sysroot_flags "$SYSROOT")
vers() { write_vers "$D/${1}_vers.c" "$1" text_cmds 197 __; printf '%s' "$D/${1}_vers.c"; }

tool "$B" "$ROOT" "$OUT/bin/cat" "$B/cflags" cat/cat.c "$(vers cat)"
tool "$B" "$ROOT" "$OUT/usr/bin/head" "$B/cflags" head/head.c "$(vers head)"
tool "$B" "$ROOT" "$OUT/usr/bin/sed" "$B/cflags" sed/compile.c sed/main.c sed/misc.c sed/process.c "$(vers sed)"
# wc: libxo's headers and dylib from its install tree (the SDK's libxo.tbd).
write_rsp "$B/wc.rsp" "${base[@]}" -I"$XO/usr/local/include" $(cmd_sysroot_flags "$SYSROOT")
tool "$B" "$ROOT" "$OUT/usr/bin/wc" "$B/wc.rsp" wc/wc.c "$(vers wc)" -- -L"$XO/usr/lib" -lxo

# cut, uniq, tr: one target each, no frameworks.
tool "$B" "$ROOT" "$OUT/usr/bin/cut" "$B/cflags" cut/cut.c "$(vers cut)"
tool "$B" "$ROOT" "$OUT/usr/bin/uniq" "$B/cflags" uniq/uniq.c "$(vers uniq)"
tool "$B" "$ROOT" "$OUT/usr/bin/tr" "$B/cflags" tr/cmap.c tr/cset.c tr/str.c tr/tr.c "$(vers tr)"
# sort: sort.xcconfig's definitions (SORT_VERSION is the project version).
# sort -R hashes with CommonCrypto's SHA-256 (libcommonCrypto, closed, which
# libSystem doesn't reexport here): compat/nd_cc_sha256.c is linked in.
write_rsp "$B/sort.rsp" "${base[@]}" "-DSORT_VERSION='\"197\"'" -DWITHOUT_NLS -DSORT_THREADS $(cmd_sysroot_flags "$SYSROOT")
tool "$B" "$ROOT" "$OUT/usr/bin/sort" "$B/sort.rsp" sort/bwstring.c sort/coll.c sort/file.c sort/mem.c \
	sort/radixsort.c sort/sort.c sort/vsort.c "$PROJ/compat/nd_cc_sha256.c" "$(vers sort)"
# tail: libutil.tbd in its Frameworks phase.
write_rsp "$B/tail.rsp" "${base[@]}" -I"$UTIL/usr/local/include" $(cmd_sysroot_flags "$SYSROOT")
tool "$B" "$ROOT" "$OUT/usr/bin/tail" "$B/tail.rsp" tail/forward.c tail/misc.c tail/read.c tail/reverse.c \
	tail/tail.c "$(vers tail)" -- -L"$UTIL/usr/lib" -lutil
# grep: grep.xcconfig (OTHER_LDFLAGS -lbz2 -llzma -lz), and
# grep_variant_links.sh's names (hard links: copies here).
write_rsp "$B/grep.rsp" "${base[@]}" -I"$ZL/usr/local/include" -I"$BZ/usr/local/include" \
	-I"$LZ/usr/local/include" $(cmd_sysroot_flags "$SYSROOT")
tool "$B" "$ROOT" "$OUT/usr/bin/grep" "$B/grep.rsp" grep/file.c grep/grep.c grep/queue.c grep/util.c "$(vers grep)" \
	-- -L"$BZ/usr/lib" -lbz2 -L"$LZ/usr/lib" -llzma -L"$ZL/usr/lib" -lz
for v in e f z ze zf bz bze bzf; do cp "$OUT/usr/bin/grep" "$OUT/usr/bin/${v}grep"; done

# The other targets (P4-21), one source each unless listed; INSTALL_PATH
# /usr/bin unless the target says otherwise. Their per-target warning
# settings change no interface.
for t in banner col colrm column comm csplit expand fmt fold join lam look nl paste rev rs; do
	tool "$B" "$ROOT" "$OUT/usr/bin/$t" "$B/cflags" "$t/$t.c" "$(vers "$t")"
done
tool "$B" "$ROOT" "$OUT/usr/bin/unexpand" "$B/cflags" unexpand/unexpand.c "$(vers unexpand)"
tool "$B" "$ROOT" "$OUT/usr/bin/pr" "$B/cflags" pr/egetopt.c pr/pr.c "$(vers pr)"
tool "$B" "$ROOT" "$OUT/usr/bin/vis" "$B/cflags" vis/foldit.c vis/vis.c "$(vers vis)"
# ed: INSTALL_PATH /bin. macOS installs no red, only its manual page.
tool "$B" "$ROOT" "$OUT/bin/ed" "$B/cflags" ed/buf.c ed/glbl.c ed/io.c ed/main.c ed/re.c ed/sub.c ed/undo.c "$(vers ed)"
# bintrans, and the "Install bintrans links" phase's hard links (copies here).
tool "$B" "$ROOT" "$OUT/usr/bin/bintrans" "$B/cflags" bintrans/apple_base64.c bintrans/bintrans.c bintrans/qp.c \
	bintrans/uudecode.c bintrans/uuencode.c "$(vers bintrans)"
for l in base64 uudecode uuencode b64decode b64encode; do cp "$OUT/usr/bin/bintrans" "$OUT/usr/bin/$l"; done
# split: libutil.tbd in its Frameworks phase (expand_number(3)).
tool "$B" "$ROOT" "$OUT/usr/bin/split" "$B/tail.rsp" split/split.c "$(vers split)" -- -L"$UTIL/usr/lib" -lutil
# unvis: libxo.tbd in its Frameworks phase.
tool "$B" "$ROOT" "$OUT/usr/bin/unvis" "$B/wc.rsp" unvis/unvis.c "$(vers unvis)" -- -L"$XO/usr/lib" -lxo
# ul: /usr/lib/libcurses.dylib (ncurses' termcap interface).
write_rsp "$B/ul.rsp" "${base[@]}" -I"$NC/usr/local/include" $(cmd_sysroot_flags "$SYSROOT")
tool "$B" "$ROOT" "$OUT/usr/bin/ul" "$B/ul.rsp" ul/ul.c "$(vers ul)" -- -L"$NC/usr/lib" -lncurses
# md5: INSTALL_PATH /sbin, OTHER_LDFLAGS -lmd (and -lCrashReporterClient on
# macOS, a closed static library md5.c doesn't call). libmd is FreeBSD's
# (base/libmd; its headers want libmd_cdefs.h first). md5_variant_links.sh's hard links (copies here).
write_rsp "$B/md5.rsp" "${base[@]}" -I"$MD/usr/local/include" -include libmd_cdefs.h $(cmd_sysroot_flags "$SYSROOT")
tool "$B" "$ROOT" "$OUT/sbin/md5" "$B/md5.rsp" md5/md5.c "$(vers md5)" -- -L"$MD/usr/lib" -lmd
for l in md5sum sha1 sha1sum sha224 sha224sum sha256 sha256sum sha384 sha384sum sha512 sha512sum; do
	cp "$OUT/sbin/md5" "$OUT/sbin/$l"
done

# sort's page is sort.1.in: FreeBSD's usr.bin/sort Makefile comments out the
# %%THREADS%% and %%NLS%% lines for a build without either, as this one is.
mkdir -p "$OUT/usr/share/man/man1"
sed -e 's/%%THREADS%%/.\\"/g' -e 's/%%NLS%%/.\\"/g' "$T/sort/sort.1.in" > "$OUT/usr/share/man/man1/sort.1"
