#!/usr/bin/env bash
# SPDX-License-Identifier: BSD-2-Clause
# The session's text commands from text_cmds-197 (docs/base/libsystem.md):
# replays text_cmds.xcodeproj's cat, head, wc, sed, cut, sort, uniq, tr,
# tail and grep targets with the project's settings and
# xcconfigs/base.xcconfig (gnu99, __FBSDID=__RCSID, DEAD_CODE_STRIPPING,
# VERSION_INFO_PREFIX __; INSTALL_PATH /usr/bin, /bin for cat).
#   build.sh OUT TEXT_CMDS_SRC SYSROOT DEPROOT...   (DEPROOT: //base:root, //base:libxo, //base:libutil)
# OUT receives bin/cat and usr/bin/{head,wc,sed,cut,sort,uniq,tr,tail,grep,
# egrep,fgrep}.
# wc links libxo (FreeBSD's; base/libxo), tail libutil (expand_number(3)).
# grep links libbz2, liblzma and libz on macOS (grep.xcconfig), for
# compressed input; the base has none of them yet (zlib is P3-02), so patch
# 0001 builds it without them (GREP_NO_DECOMPRESSION), and of
# grep_variant_links.sh's names only egrep and fgrep are installed (the z
# and bz ones read compressed input).
source "$(dirname "$0")/../../tools/base/common.sh"
source "$(dirname "$0")/../commands.sh"
OUT="$(abspath "$1")"; T="$(abspath "$2")"; SYSROOT="$(abspath "$3")"; shift 3
DEPS=(); for d in "$@"; do DEPS+=("$(abspath "$d")"); done
ROOT="$(find_root "${DEPS[@]}")"
XO=""; for d in "${DEPS[@]}"; do [ -f "$d/usr/lib/libxo.dylib" ] && XO="$d"; done
[ -n "$XO" ] || { echo "text_cmds: no DEPROOT holds usr/lib/libxo.dylib (pass //base:libxo)" >&2; exit 1; }
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
# grep: grep.xcconfig less its OTHER_LDFLAGS (patch 0001), and
# grep_variant_links.sh's egrep and fgrep (hard links: copies here).
write_rsp "$B/grep.rsp" "${base[@]}" -DGREP_NO_DECOMPRESSION $(cmd_sysroot_flags "$SYSROOT")
tool "$B" "$ROOT" "$OUT/usr/bin/grep" "$B/grep.rsp" grep/file.c grep/grep.c grep/queue.c grep/util.c "$(vers grep)"
cp "$OUT/usr/bin/grep" "$OUT/usr/bin/egrep"; cp "$OUT/usr/bin/grep" "$OUT/usr/bin/fgrep"
