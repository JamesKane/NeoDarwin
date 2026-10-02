#!/usr/bin/env bash
# SPDX-License-Identifier: BSD-2-Clause
# rtsold and rtsol from FreeBSD's usr.sbin/rtsold (docs/kernel/network.md,
# "IPv6 DNS: RDNSS and DHCPv6"): router solicitation, and the DNS servers
# and search domains of router advertisements (RFC 8106 RDNSS and DNSSL)
# handed to resolvconf(8); the M and O flags run a script (netconfigd's
# DHCPv6 starter). The kernel still does the address autoconfiguration.
#   build.sh OUT RTSOLD_SRC SYSROOT DEPROOT...   (DEPROOT: //base:root, //base:libutil_dylib)
# OUT receives usr/sbin/rtsold and sbin/rtsol, one program (a name other
# than rtsold is the one-shot rtsol, rtsold.c's main), hard-linked, as
# FreeBSD installs them (its sbin/rtsol/Makefile builds the same sources).
#
# Apple's rtsol (network_cmds-726, which macOS doesn't install) is KAME's
# program from before RDNSS and the M/O scripts; IPConfiguration, macOS's
# client for both, is a configd plugin. By the reuse order
# (docs/repository.md §3.1), FreeBSD's comes next, and replaces Apple's.
#
# The Makefile's settings: its SRCS, LIBADD util (Apple's libutil has
# pidfile(3)), without WITH_CASPER (no libcasper: the services' functions
# are called directly, as in FreeBSD's rescue build). compat/: Capsicum and
# Casper's headers (nothing to limit), and nd_rtsold_compat.h, included first
# in every source (nitems, __DECONST, CLOCK_MONOTONIC_FAST, closefrom, and
# xnu's interface flags); -D__APPLE_USE_RFC_3542 for RFC 3542's IPV6_PKTINFO
# and IPV6_HOPLIMIT, as network_cmds' targets set it.
# patches/: 0001 xnu's interface flags and routing messages.
source "$(dirname "$0")/../../tools/base/common.sh"
source "$(dirname "$0")/../commands.sh"
OUT="$(abspath "$1")"; D="$(abspath "$2")"; SYSROOT="$(abspath "$3")"; shift 3
DEPS=(); for d in "$@"; do DEPS+=("$(abspath "$d")"); done
ROOT="$(find_root "${DEPS[@]}")"
UTIL=""; for d in "${DEPS[@]}"; do [ -f "$d/usr/lib/libutil.dylib" ] && UTIL="$d"; done
[ -n "$UTIL" ] || { echo "rtsold: no DEPROOT holds usr/lib/libutil.dylib (pass //base:libutil_dylib)" >&2; exit 1; }
PROJ="$(cd "$(dirname "$0")" && pwd)"
B="$(mktemp -d)"; trap 'rm -rf "$B"' EXIT
D="$(stage_src "$D/usr.sbin/rtsold" "$B/src" "$PROJ/patches")"   # patches/ applied
cd "$D"

write_rsp "$B/cflags" "${TARGET_FLAGS[@]}" -Os -fno-common -D__APPLE_USE_RFC_3542 \
	-include "$PROJ/compat/nd_rtsold_compat.h" -I"$PROJ/compat" -I"$UTIL/usr/local/include" \
	$(cmd_sysroot_flags "$SYSROOT")
tool "$B" "$ROOT" "$OUT/usr/sbin/rtsold" "$B/cflags" cap_llflags.c cap_script.c cap_sendmsg.c dump.c \
	if.c rtsock.c rtsol.c rtsold.c -- -L"$UTIL/usr/lib" -lutil
mkdir -p "$OUT/sbin" && ln -f "$OUT/usr/sbin/rtsold" "$OUT/sbin/rtsol"
