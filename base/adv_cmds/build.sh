#!/usr/bin/env bash
# SPDX-License-Identifier: BSD-2-Clause
# ps, stty and tty from adv_cmds-237 (docs/base/libsystem.md): replays
# adv_cmds.xcodeproj's ps, stty and tty targets with the project's settings
# (GCC_NO_COMMON_BLOCKS, DEAD_CODE_STRIPPING, VERSION_INFO_PREFIX __), and
# the Desktop target's install-ps.sh, which installs ps (SKIP_INSTALL in its
# target) as /bin/ps.
#   build.sh OUT ADV_CMDS_SRC SYSROOT DEPROOT...   (DEPROOT: //base:root)
# OUT receives bin/{ps,stty} and usr/bin/tty.
# Apple installs ps setuid root (mode 4755) with its entitlements
# (PS_ENTITLED); NeoDarwin has no code-signing policy until P1-15, and the
# image rule sets modes.
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

# Warning flags change no interface and are left out; ps and tty add
# __FBSDID=__RCSID, ps OTHER_CFLAGS -DPS_ENTITLED.
base=("${TARGET_FLAGS[@]}" -Os -fno-common $(cmd_sysroot_flags "$SYSROOT"))
vers() { write_vers "$D/${1}_vers.c" "$1" adv_cmds 237 __; printf '%s' "$D/${1}_vers.c"; }
write_rsp "$B/ps.rsp" "${base[@]}" -D__FBSDID=__RCSID -DPS_ENTITLED
write_rsp "$B/stty.rsp" "${base[@]}"
write_rsp "$B/tty.rsp" "${base[@]}" -D__FBSDID=__RCSID

tool "$B" "$ROOT" "$OUT/bin/ps" "$B/ps.rsp" ps/fmt.c ps/keyword.c ps/nlist.c ps/print.c ps/ps.c ps/tasks.c "$(vers ps)"
tool "$B" "$ROOT" "$OUT/bin/stty" "$B/stty.rsp" stty/cchar.c stty/gfmt.c stty/key.c stty/modes.c stty/print.c \
	stty/stty.c stty/util.c "$(vers stty)"
tool "$B" "$ROOT" "$OUT/usr/bin/tty" "$B/tty.rsp" tty/tty.c "$(vers tty)"
