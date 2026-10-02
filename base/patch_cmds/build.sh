#!/usr/bin/env bash
# SPDX-License-Identifier: BSD-2-Clause
# cmp and diff from patch_cmds-72 (FreeBSD's cmp and diff, as macOS 26
# ships them; docs/base/libsystem.md): replays patch_cmds.xcodeproj's cmp and
# diff targets (gnu11, INSTALL_PATH /usr/bin; cmp's OTHER_LDFLAGS -lutil,
# diff's HEADER_SEARCH_PATHS $(SRCROOT)/diff).
#   build.sh OUT PATCH_CMDS_SRC SYSROOT DEPROOT...   (DEPROOT: //base:root, //base:libutil)
# OUT receives usr/bin/{cmp,diff}. patch, diff3, diffstat and sdiff are
# left for later.
source "$(dirname "$0")/../../tools/base/common.sh"
source "$(dirname "$0")/../commands.sh"
OUT="$(abspath "$1")"; P="$(abspath "$2")"; SYSROOT="$(abspath "$3")"; shift 3
DEPS=(); for d in "$@"; do DEPS+=("$(abspath "$d")"); done
ROOT="$(find_root "${DEPS[@]}")"
UTIL=""; for d in "${DEPS[@]}"; do [ -f "$d/usr/lib/libutil.dylib" ] && UTIL="$d"; done
[ -n "$UTIL" ] || { echo "patch_cmds: no DEPROOT holds usr/lib/libutil.dylib (pass //base:libutil)" >&2; exit 1; }
B="$(mktemp -d)"; trap 'rm -rf "$B"' EXIT
cd "$P"
D="$B/derived"; mkdir -p "$D"

# The project's settings: apple-generic versioning, VERSION_INFO_PREFIX __;
# warning flags change no interface and are left out.
base=("${TARGET_FLAGS[@]}" -Os -std=gnu11 $(cmd_sysroot_flags "$SYSROOT"))
vers() { write_vers "$D/${1}_vers.c" "$1" patch_cmds 72 __; printf '%s' "$D/${1}_vers.c"; }
write_rsp "$B/cmp.rsp" "${base[@]}" -I"$UTIL/usr/local/include"
tool "$B" "$ROOT" "$OUT/usr/bin/cmp" "$B/cmp.rsp" cmp/cmp.c cmp/link.c cmp/misc.c cmp/regular.c cmp/special.c \
	"$(vers cmp)" -- -L"$UTIL/usr/lib" -lutil
write_rsp "$B/diff.rsp" "${base[@]}" -I"$P/diff"
tool "$B" "$ROOT" "$OUT/usr/bin/diff" "$B/diff.rsp" diff/diff.c diff/diff_atomize_text.c diff/diff_main.c \
	diff/diff_myers.c diff/diff_output.c diff/diff_output_edscript.c diff/diff_output_plain.c \
	diff/diff_output_unidiff.c diff/diff_patience.c diff/diffdir.c diff/diffreg.c diff/diffreg_new.c diff/pr.c \
	diff/recallocarray.c diff/xmalloc.c "$(vers diff)"
