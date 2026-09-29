#!/usr/bin/env bash
# SPDX-License-Identifier: BSD-2-Clause
# The first session's file commands from file_cmds-475 (docs/base/libsystem.md):
# replays file_cmds.xcodeproj's ls, cp, mv, rm, mkdir, ln and chmod targets
# (project settings: __FBSDID=__RCSID, _DARWIN_USE_64_BIT_INODE,
# DEAD_CODE_STRIPPING; each target installs in /bin).
#   build.sh OUT FILE_CMDS_SRC SYSROOT DEPROOT...   (DEPROOT: //base:root, //base:libutil)
# OUT receives bin/{ls,cp,mv,rm,mkdir,ln,chmod}.
# ls links libutil for humanize_number(3). Apple builds it with COLORLS,
# which links libcurses for termcap; NeoDarwin has no ncurses yet, so ls
# builds without colour (ls -G is accepted and ignored).
source "$(dirname "$0")/../../tools/base/common.sh"
source "$(dirname "$0")/../commands.sh"
OUT="$(abspath "$1")"; F="$(abspath "$2")"; SYSROOT="$(abspath "$3")"; shift 3
DEPS=(); for d in "$@"; do DEPS+=("$(abspath "$d")"); done
ROOT="$(find_root "${DEPS[@]}")"
UTIL=""; for d in "${DEPS[@]}"; do [ -f "$d/usr/lib/libutil.dylib" ] && UTIL="$d"; done
[ -n "$UTIL" ] || { echo "file_cmds: no DEPROOT holds usr/lib/libutil.dylib (pass //base:libutil)" >&2; exit 1; }
PROJ="$(cd "$(dirname "$0")" && pwd)"
B="$(mktemp -d)"; trap 'rm -rf "$B"' EXIT
F="$(stage_src "$F" "$B/src" "$PROJ/patches")"   # patches/ applied
cd "$F"
D="$B/derived"; mkdir -p "$D"

# The project's Release settings; WARNING_CFLAGS (-Wall -Werror ...) change
# no interface and are left out. VERSIONING_SYSTEM = apple-generic, 475.
base=("${TARGET_FLAGS[@]}" -Os -fno-common -D__FBSDID=__RCSID -D_DARWIN_USE_64_BIT_INODE)
write_rsp "$B/cflags" "${base[@]}" $(cmd_sysroot_flags "$SYSROOT")
vers() { write_vers "$D/${1}_vers.c" "$1" file_cmds 475; printf '%s' "$D/${1}_vers.c"; }

# ls: libutil's private header from its install tree; libutil linked from
# there (-L resolves under -syslibroot's root first, then as given).
write_rsp "$B/ls.rsp" "${base[@]}" -I"$UTIL/usr/local/include" $(cmd_sysroot_flags "$SYSROOT")
tool "$B" "$ROOT" "$OUT/bin/ls" "$B/ls.rsp" ls/cmp.c ls/ls.c ls/print.c ls/util.c "$(vers ls)" \
	-- -L"$UTIL/usr/lib" -lutil
for t in cp:cp/utils.c,cp/cp.c mv:mv/mv.c rm:rm/rm.c mkdir:mkdir/mkdir.c ln:ln/ln.c chmod:chmod/chmod_acl.c,chmod/chmod.c; do
	IFS=, read -r -a srcs <<< "${t#*:}"
	tool "$B" "$ROOT" "$OUT/bin/${t%%:*}" "$B/cflags" "${srcs[@]}" "$(vers "${t%%:*}")"
done
