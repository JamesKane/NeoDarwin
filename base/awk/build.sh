#!/usr/bin/env bash
# SPDX-License-Identifier: BSD-2-Clause
# awk from awk-40 (the one true awk, onetrue-awk; docs/base/libsystem.md):
# replays awk.xcodeproj's awk target (INSTALL_PATH /usr/bin; project
# settings: apple-generic versioning, no preprocessor definitions). The
# project commits the generated parser and table (src/awkgram.tab.c,
# src/proctab.c), which the target compiles as they are.
#   build.sh OUT AWK_SRC SYSROOT DEPROOT...   (DEPROOT: //base:root)
# OUT receives usr/bin/awk. The script phase's man page and open-source
# plists aren't installed.
source "$(dirname "$0")/../../tools/base/common.sh"
source "$(dirname "$0")/../commands.sh"
OUT="$(abspath "$1")"; A="$(abspath "$2")"; SYSROOT="$(abspath "$3")"; shift 3
DEPS=(); for d in "$@"; do DEPS+=("$(abspath "$d")"); done
ROOT="$(find_root "${DEPS[@]}")"
B="$(mktemp -d)"; trap 'rm -rf "$B"' EXIT
cd "$A"
D="$B/derived"; mkdir -p "$D"

# Warning flags change no interface and are left out.
write_rsp "$B/cflags" "${TARGET_FLAGS[@]}" -Os -iquote src $(cmd_sysroot_flags "$SYSROOT")
write_vers "$D/awk_vers.c" awk awk 40
tool "$B" "$ROOT" "$OUT/usr/bin/awk" "$B/cflags" src/b.c src/lex.c src/lib.c src/awkgram.tab.c src/main.c \
	src/parse.c src/proctab.c src/run.c src/tran.c "$D/awk_vers.c"
