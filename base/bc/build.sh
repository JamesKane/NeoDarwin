#!/usr/bin/env bash
# SPDX-License-Identifier: BSD-2-Clause
# bc and dc from bc-35 (Gavin Howard's bc, as FreeBSD's usr.bin/gh-bc; P4-21
# checkpoint 3, docs/architecture/freebsd-parity.md §2.1): replays
# bc.xcodeproj's bc target (gnu11, the target's OTHER_CFLAGS: BUILD_TYPE=A,
# MAINEXEC=bc, NDEBUG, editline history, extra math, bc and dc), its
# generate_sources.sh and bc_man.sh phases, and the All target's bc_link.sh
# (dc is a hard link to bc). Links libedit.
#   build.sh OUT BC_SRC SYSROOT DEPROOT...   (DEPROOT: //base:root, //base:libedit_dylib)
# OUT receives usr/bin/{bc,dc} and bc.1 and dc.1.
source "$(dirname "$0")/../../tools/base/common.sh"
source "$(dirname "$0")/../commands.sh"
OUT="$(abspath "$1")"; S="$(abspath "$2")"; SYSROOT="$(abspath "$3")"; shift 3
DEPS=(); for d in "$@"; do DEPS+=("$(abspath "$d")"); done
ROOT="$(find_root "${DEPS[@]}")"
LE=""; for d in "${DEPS[@]}"; do [ -f "$d/usr/lib/libedit.3.dylib" ] && LE="$d"; done
[ -n "$LE" ] || { echo "bc: no DEPROOT holds usr/lib/libedit.3.dylib (pass //base:libedit_dylib)" >&2; exit 1; }
PROJ="$(cd "$(dirname "$0")" && pwd)"
B="$(mktemp -d)"; trap 'rm -rf "$B"' EXIT
S="$(stage_src "$S" "$B/src" "$PROJ/patches")"   # patches/ (none) applied
cd "$S/bc"
D="$B/derived"; mkdir -p "$D"   # BUILT_PRODUCTS_DIR

# generate_sources.sh
sh gen/strgen.sh gen/lib.bc "$D/lib.c" 0 bc_lib bc_lib_name 1 1
sh gen/strgen.sh gen/lib2.bc "$D/lib2.c" 0 bc_lib2 bc_lib2_name 1 1
sh gen/strgen.sh gen/bc_help.txt "$D/bc_help.c" 0 bc_help
sh gen/strgen.sh gen/dc_help.txt "$D/dc_help.c" 0 dc_help

write_vers "$D/bc_vers.c" bc bc 35 __
write_rsp "$B/bc.rsp" "${TARGET_FLAGS[@]}" -Os -std=gnu11 -fno-common $(cmd_sysroot_flags "$SYSROOT") \
	-isystem "$LE/usr/local/include" -I"$S/bc/include" -DBUILD_TYPE=A -DMAINEXEC=bc -DNDEBUG \
	-DBC_ENABLE_EDITLINE -DBC_ENABLE_HISTORY -DBC_ENABLE_EXTRA_MATH -DBC_ENABLED -DDC_ENABLED
tool "$B" "$ROOT" "$OUT/usr/bin/bc" "$B/bc.rsp" src/opt.c src/data.c src/parse.c src/read.c src/vector.c \
	src/main.c src/rand.c "$D/bc_help.c" src/bc_lex.c src/bc.c src/dc_lex.c src/lex.c src/history.c \
	"$D/dc_help.c" src/lang.c src/vm.c src/bc_parse.c src/program.c src/args.c src/num.c src/dc.c \
	src/dc_parse.c src/file.c "$D/lib.c" src/library.c "$D/lib2.c" "$D/bc_vers.c" -- -L"$LE/usr/lib" -ledit
ln -f "$OUT/usr/bin/bc" "$OUT/usr/bin/dc"
mkdir -p "$OUT/usr/share/man/man1"
install -m 0444 manuals/bc/A.1 "$OUT/usr/share/man/man1/bc.1"
install -m 0444 manuals/dc/A.1 "$OUT/usr/share/man/man1/dc.1"
