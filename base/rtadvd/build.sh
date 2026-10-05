#!/usr/bin/env bash
# SPDX-License-Identifier: BSD-2-Clause
# rtadvd from FreeBSD's usr.sbin/rtadvd (docs/kernel/network.md, "rtadvd"):
# the IPv6 router advertisement daemon and its configuration, as FreeBSD
# installs them.
#   build.sh OUT RTADVD_SRC SYSROOT DEPROOT...   (DEPROOT: //base:root, //base:libutil_dylib)
# OUT receives usr/sbin/rtadvd, usr/sbin/rtadvctl and private/etc/rtadvd.conf.
#
# network_cmds-726 has no rtadvd sources (rtadvd.tproj holds only run-rtadvd,
# a test script for a newer Apple rtadvd with options FreeBSD's lacks); by
# the reuse order (docs/repository.md §3.1), FreeBSD's comes next. Its
# Makefile: SRCS, LIBADD util (Apple's libutil has pidfile(3)); no
# definitions. NeoDarwin adds __APPLE_USE_RFC_3542 (Darwin's
# <netinet6/in6.h> otherwise gives RFC 2292's socket options, and rtadvd
# uses RFC 3542's IPV6_RECVPKTINFO and IPV6_RECVHOPLIMIT, as network_cmds'
# targets do) and System.framework's PrivateHeaders (xnu's private
# <netinet6/nd6.h> and <netinet6/in6_var.h>, SIOCGIFEFLAGS and
# IFEF_ACCEPT_RTADV), as network_cmds' rtsol and ndp are built. Warning
# flags are left out.
# compat/nd_rtadvd_compat.h, included first in every source: nitems,
# CLOCK_MONOTONIC_FAST, INFTIM. patches/0001: xnu's routing socket (32-bit
# address padding, no RTM_IFANNOUNCE), SIOCGIFINFO_IN6 and
# IFEF_ACCEPT_RTADV, and its RDNSS, DNSSL and PREF64 structures.
source "$(dirname "$0")/../../tools/base/common.sh"
source "$(dirname "$0")/../commands.sh"
OUT="$(abspath "$1")"; D="$(abspath "$2")"; SYSROOT="$(abspath "$3")"; shift 3
DEPS=(); for d in "$@"; do DEPS+=("$(abspath "$d")"); done
ROOT="$(find_root "${DEPS[@]}")"
UTIL=""; for d in "${DEPS[@]}"; do [ -f "$d/usr/lib/libutil.dylib" ] && UTIL="$d"; done
[ -n "$UTIL" ] || { echo "rtadvd: no DEPROOT holds usr/lib/libutil.dylib (pass //base:libutil_dylib)" >&2; exit 1; }
PROJ="$(cd "$(dirname "$0")" && pwd)"
B="$(mktemp -d)"; trap 'rm -rf "$B"' EXIT
D0="$D"
D="$(stage_src "$D/usr.sbin/rtadvd" "$B/src" "$PROJ/patches")"   # patches/ applied
cd "$D"

write_rsp "$B/cflags" "${TARGET_FLAGS[@]}" -Os -fno-common -D__APPLE_USE_RFC_3542=1 \
	-include "$PROJ/compat/nd_rtadvd_compat.h" -I"$UTIL/usr/local/include" $(sysroot_flags "$SYSROOT")
tool "$B" "$ROOT" "$OUT/usr/sbin/rtadvd" "$B/cflags" rtadvd.c rrenum.c advcap.c if.c config.c timer.c \
	timer_subr.c control.c control_server.c -- -L"$UTIL/usr/lib" -lutil
# rtadvctl (usr.sbin/rtadvctl's Makefile: rtadvctl.c with rtadvd's
# control.c, control_client.c, if.c and timer_subr.c, patched as for rtadvd)
# talks to rtadvd over its control socket, /var/run/rtadvd.sock.
{ cat "$B/cflags"; echo "-I$D"; } > "$B/ctlflags"
tool "$B" "$ROOT" "$OUT/usr/sbin/rtadvctl" "$B/ctlflags" "$D0/usr.sbin/rtadvctl/rtadvctl.c" control.c control_client.c \
	if.c timer_subr.c
mkdir -p "$OUT/private/etc"
install -m 0644 rtadvd.conf "$OUT/private/etc/rtadvd.conf"
