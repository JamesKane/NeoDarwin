#!/usr/bin/env bash
# SPDX-License-Identifier: BSD-2-Clause
# nc from netcat-56 (OpenBSD's netcat with Apple's connectx, traffic class
# and keepalive options; P4-21 checkpoint 3,
# docs/architecture/freebsd-parity.md §2.1): replays netcat.xcodeproj's nc
# target (USE_SELECT, HEADER_SEARCH_PATHS System.framework/PrivateHeaders).
#   build.sh OUT NETCAT_SRC SYSROOT DEPROOT...   (DEPROOT: //base:root)
# OUT receives usr/bin/nc and nc.1. The target links libnetwork (closed) for
# copyconninfo() and freeconninfo() alone; compat/network/conninfo.h has
# them over SIOCGCONNINFO, and nothing else of libnetwork is linked. The
# target's entitlements (network client and server, private interface and
# keepalive offload ones) aren't signed in: NeoDarwin has no sandbox
# enforcing them.
source "$(dirname "$0")/../../tools/base/common.sh"
source "$(dirname "$0")/../commands.sh"
OUT="$(abspath "$1")"; S="$(abspath "$2")"; SYSROOT="$(abspath "$3")"; shift 3
DEPS=(); for d in "$@"; do DEPS+=("$(abspath "$d")"); done
ROOT="$(find_root "${DEPS[@]}")"
PROJ="$(cd "$(dirname "$0")" && pwd)"
B="$(mktemp -d)"; trap 'rm -rf "$B"' EXIT
S="$(stage_src "$S" "$B/src" "$PROJ/patches")"   # patches/ (none) applied
cd "$S"
D="$B/derived"; mkdir -p "$D"

write_vers "$D/nc_vers.c" nc netcat 56 __
write_rsp "$B/nc.rsp" "${TARGET_FLAGS[@]}" -Os -DUSE_SELECT -I"$PROJ/compat" \
	-isystem "$SYSROOT/System/Library/Frameworks/System.framework/PrivateHeaders" $(cmd_sysroot_flags "$SYSROOT")
tool "$B" "$ROOT" "$OUT/usr/bin/nc" "$B/nc.rsp" atomicio.c netcat.c sourceroute.c socks.c "$D/nc_vers.c"
mkdir -p "$OUT/usr/share/man/man1"
install -m 0444 nc.1 "$OUT/usr/share/man/man1/"
