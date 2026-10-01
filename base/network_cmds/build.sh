#!/usr/bin/env bash
# SPDX-License-Identifier: BSD-2-Clause
# ifconfig, ping, netstat and route from network_cmds-726 (docs/base/session.md,
# "Loopback and sshd"): replays network_cmds.xcodeproj's ifconfig, ping,
# netstat, route and network_cmds_lib targets with their Release settings.
#   build.sh OUT NETWORK_CMDS_SRC SYSROOT DEPROOT...   (DEPROOT: //base:root)
# OUT receives sbin/{ifconfig,ping,route} and usr/sbin/netstat (each
# target's INSTALL_PATH). The project's GCC_PREPROCESSOR_DEFINITIONS
# (USE_RFC2292BIS=1, __APPLE_USE_RFC_3542=1, __APPLE_API_OBSOLETE=1) and
# HEADER_SEARCH_PATHS (network_cmds_lib) apply to every target; each target
# adds System.framework's PrivateHeaders (xnu's private net/ headers), so
# these are built with the libSystem sysroot flags (common.sh), not the
# command ones. Warning flags are left out. Apple signs ping and route with
# network-management-entitlements.plist (the sandbox's network client and
# server rights and private network-management ones); NeoDarwin has no
# sandbox or policy that reads them, so they're signed ad hoc without, as ps
# is (base/adv_cmds).
# The rest of network_cmds (arp, ndp, traceroute, ping6, rtadvd, ...) is
# P4-24's (roadmap/backlog.yaml).
source "$(dirname "$0")/../../tools/base/common.sh"
source "$(dirname "$0")/../commands.sh"
OUT="$(abspath "$1")"; N="$(abspath "$2")"; SYSROOT="$(abspath "$3")"; shift 3
DEPS=(); for d in "$@"; do DEPS+=("$(abspath "$d")"); done
ROOT="$(find_root "${DEPS[@]}")"
PROJ="$(cd "$(dirname "$0")" && pwd)"
B="$(mktemp -d)"; trap 'rm -rf "$B"' EXIT
N="$(stage_src "$N" "$B/src" "$PROJ/patches")"   # patches/ applied
cd "$N"

base=("${TARGET_FLAGS[@]}" -Os -DUSE_RFC2292BIS=1 -D__APPLE_USE_RFC_3542=1 -D__APPLE_API_OBSOLETE=1
	-Inetwork_cmds_lib $(sysroot_flags "$SYSROOT"))
write_rsp "$B/lib.rsp" "${base[@]}"
write_rsp "$B/ifconfig.rsp" "${base[@]}" -DUSE_IF_MEDIA -DNO_IPX -DINET6 -DUSE_VLANS -DUSE_BONDS -DNEODARWIN_XNU_12377
write_rsp "$B/ping.rsp" "${base[@]}"
write_rsp "$B/netstat.rsp" "${base[@]}" -DINET6 -DIPSEC
write_rsp "$B/route.rsp" "${base[@]}" -DINET6 -DIPSEC

# network_cmds_lib: a static library ping, netstat and route link.
compile "$B/obj/lib" "$B/lib.rsp" network_cmds_lib/network_cmds_lib.c network_cmds_lib/gmt2local.c
LIB=("$B"/obj/lib/*.o)

tool "$B" "$ROOT" "$OUT/sbin/ifconfig" "$B/ifconfig.rsp" \
	$(printf 'ifconfig.tproj/%s\n' ifbond.c ifconfig.c ifmedia.c ifvlan.c af_inet.c af_inet6.c af_link.c ifbridge.c \
		ifclone.c iffake.c nexus.c)
tool "$B" "$ROOT" "$OUT/sbin/ping" "$B/ping.rsp" ping.tproj/ping.c -- "${LIB[@]}"
tool "$B" "$ROOT" "$OUT/usr/sbin/netstat" "$B/netstat.rsp" \
	$(printf 'netstat.tproj/%s\n' bpf.c data.c if.c inet.c inet6.c ipsec.c main.c vsock.c mbuf.c mcast.c systm.c \
		route.c tp_astring.c mptcp.c unix.c misc.c) -- "${LIB[@]}"
tool "$B" "$ROOT" "$OUT/sbin/route" "$B/route.rsp" route.tproj/route.c -- "${LIB[@]}"
