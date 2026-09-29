#!/usr/bin/env bash
# SPDX-License-Identifier: BSD-2-Clause
# The first session's text commands from text_cmds-197 (docs/base/libsystem.md):
# replays text_cmds.xcodeproj's cat, head, wc and sed targets with the
# project's settings and xcconfigs/base.xcconfig (gnu99, __FBSDID=__RCSID,
# DEAD_CODE_STRIPPING, VERSION_INFO_PREFIX __; INSTALL_PATH /usr/bin, /bin
# for cat).
#   build.sh OUT TEXT_CMDS_SRC SYSROOT DEPROOT...   (DEPROOT: //base:root, //base:libxo)
# OUT receives bin/cat and usr/bin/{head,wc,sed}.
# wc links libxo (FreeBSD's; base/libxo). grep is left for later: it links
# libbz2, liblzma and libz.
source "$(dirname "$0")/../../tools/base/common.sh"
source "$(dirname "$0")/../commands.sh"
OUT="$(abspath "$1")"; T="$(abspath "$2")"; SYSROOT="$(abspath "$3")"; shift 3
DEPS=(); for d in "$@"; do DEPS+=("$(abspath "$d")"); done
ROOT="$(find_root "${DEPS[@]}")"
XO=""; for d in "${DEPS[@]}"; do [ -f "$d/usr/lib/libxo.dylib" ] && XO="$d"; done
[ -n "$XO" ] || { echo "text_cmds: no DEPROOT holds usr/lib/libxo.dylib (pass //base:libxo)" >&2; exit 1; }
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
