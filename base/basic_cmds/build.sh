#!/usr/bin/env bash
# SPDX-License-Identifier: BSD-2-Clause
# mesg and write from basic_cmds-70 (P4-21,
# docs/architecture/freebsd-parity.md §2.1): replays basic_cmds.xcodeproj's
# two targets with the project's Release settings (VERSIONING_SYSTEM
# apple-generic, VERSION_INFO_PREFIX __, DEAD_CODE_STRIPPING; INSTALL_PATH
# /usr/bin).
#   build.sh OUT BASIC_CMDS_SRC SYSROOT DEPROOT...   (DEPROOT: //base:root)
# OUT receives usr/bin/{mesg,write}. Apple installs write setgid tty
# (INSTALL_GROUP tty, INSTALL_MODE_FLAG g+s), to write to other users'
# terminals; the image rule sets modes.
source "$(dirname "$0")/../../tools/base/common.sh"
source "$(dirname "$0")/../commands.sh"
OUT="$(abspath "$1")"; A="$(abspath "$2")"; SYSROOT="$(abspath "$3")"; shift 3
DEPS=(); for d in "$@"; do DEPS+=("$(abspath "$d")"); done
ROOT="$(find_root "${DEPS[@]}")"
PROJ="$(cd "$(dirname "$0")" && pwd)"
B="$(mktemp -d)"; trap 'rm -rf "$B"' EXIT
A="$(stage_src "$A" "$B/src" "$PROJ/patches")"   # patches/ applied
cd "$A"
D="$B/derived"; mkdir -p "$D"

vers() { write_vers "$D/${1}_vers.c" "$1" basic_cmds 70 __; printf '%s' "$D/${1}_vers.c"; }
write_rsp "$B/cflags" "${TARGET_FLAGS[@]}" -Os -fno-common $(cmd_sysroot_flags "$SYSROOT")
tool "$B" "$ROOT" "$OUT/usr/bin/mesg" "$B/cflags" mesg/mesg.c "$(vers mesg)"
tool "$B" "$ROOT" "$OUT/usr/bin/write" "$B/cflags" write/write.c "$(vers write)"
