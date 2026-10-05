#!/usr/bin/env bash
# SPDX-License-Identifier: BSD-2-Clause
# logger and wall from remote_cmds-306 (P4-21,
# docs/architecture/freebsd-parity.md §2.1): replays remote_cmds.xcodeproj's
# logger and wall targets with the project's Release settings
# (VERSIONING_SYSTEM apple-generic, VERSION_INFO_PREFIX __,
# GCC_NO_COMMON_BLOCKS, DEAD_CODE_STRIPPING, signed ad hoc; INSTALL_PATH
# /usr/bin). The project's talk, telnet and tftp (and their daemons) are
# ports, not base (§6).
#   build.sh OUT REMOTE_CMDS_SRC SYSROOT DEPROOT...   (DEPROOT: //base:root)
# OUT receives usr/bin/{logger,wall}. logger sends to syslogd through
# syslog(3) (with -h, over UDP).
source "$(dirname "$0")/../../tools/base/common.sh"
source "$(dirname "$0")/../commands.sh"
OUT="$(abspath "$1")"; R="$(abspath "$2")"; SYSROOT="$(abspath "$3")"; shift 3
DEPS=(); for d in "$@"; do DEPS+=("$(abspath "$d")"); done
ROOT="$(find_root "${DEPS[@]}")"
PROJ="$(cd "$(dirname "$0")" && pwd)"
B="$(mktemp -d)"; trap 'rm -rf "$B"' EXIT
R="$(stage_src "$R" "$B/src" "$PROJ/patches")"   # patches/ applied
cd "$R"
D="$B/derived"; mkdir -p "$D"

# Warning flags change no interface and are left out; wall adds
# GCC_PREPROCESSOR_DEFINITIONS __FBSDID=__RCSID.
base=("${TARGET_FLAGS[@]}" -Os -fno-common $(cmd_sysroot_flags "$SYSROOT"))
vers() { write_vers "$D/${1}_vers.c" "$1" remote_cmds 306 __; printf '%s' "$D/${1}_vers.c"; }
write_rsp "$B/logger.rsp" "${base[@]}"
write_rsp "$B/wall.rsp" "${base[@]}" -D__FBSDID=__RCSID
tool "$B" "$ROOT" "$OUT/usr/bin/logger" "$B/logger.rsp" logger/logger.c "$(vers logger)"
tool "$B" "$ROOT" "$OUT/usr/bin/wall" "$B/wall.rsp" wall/ttymsg.c wall/wall.c "$(vers wall)"
