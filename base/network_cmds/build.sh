#!/usr/bin/env bash
# SPDX-License-Identifier: BSD-2-Clause
# network_cmds-726 (docs/base/session.md, "Loopback and sshd";
# docs/kernel/network.md, "P4-24"): replays network_cmds.xcodeproj's
# targets with their Release settings.
#   build.sh OUT NETWORK_CMDS_SRC SYSROOT DEPROOT...
#   (DEPROOT: //base:root, //base:libpcap_dylib, //base:libipsec_dylib)
# OUT receives each target's INSTALL_PATH: sbin/{ifconfig,ping,ping6,route},
# usr/sbin/{arp,ndp,netstat,rarpd,spray,traceroute,traceroute6} and
# usr/libexec/kdumpd, with kdumpd's launchd job (Disabled,
# as on macOS) in System/Library/LaunchDaemons. traceroute and traceroute6
# are setuid root on macOS (INSTALL_MODE_FLAG 4555); the images set the
# mode (images/BUILD.bazel, _SYSTEM_MODES). rtsol isn't built: no aggregate
# target installs it on macOS (IPConfiguration solicits routers itself), and
# NeoDarwin's rtsol and rtsold are FreeBSD's newer ones, with RDNSS
# (base/rtsold), which netconfigd runs. network_cmds-726
# has no rtadvd sources (rtadvd.tproj holds only a test script) and dnctl
# (dummynet) goes with pf. The project's GCC_PREPROCESSOR_DEFINITIONS
# (USE_RFC2292BIS=1, __APPLE_USE_RFC_3542=1, __APPLE_API_OBSOLETE=1) and
# HEADER_SEARCH_PATHS (network_cmds_lib) apply to every target; each target
# adds System.framework's PrivateHeaders (xnu's private net/ headers), so
# these are built with the libSystem sysroot flags (common.sh), not the
# command ones. Warning flags are left out. Apple signs ping and route with
# network-management-entitlements.plist (the sandbox's network client and
# server rights and private network-management ones); NeoDarwin has no
# sandbox or policy that reads them, so they're signed ad hoc without, as ps
# is (base/adv_cmds).
# traceroute and traceroute6 link libpcap (/usr/lib/libpcap.dylib, the
# Frameworks phase's) for TCP probes (-P tcp) only: base/libpcap.
# traceroute6 and ping6 link libipsec (base/libipsec). traceroute6's target
# defines IPSEC, for an IPsec bypass policy on its sockets; ping6's doesn't,
# so nothing in it calls libipsec, but it links it as on macOS.
# Apple signs ping6, traceroute and traceroute6 with the network-management
# entitlements and rarpd and kdumpd with the network client and
# server ones: ad hoc without here, as ping.
source "$(dirname "$0")/../../tools/base/common.sh"
source "$(dirname "$0")/../commands.sh"
OUT="$(abspath "$1")"; N="$(abspath "$2")"; SYSROOT="$(abspath "$3")"; shift 3
DEPS=(); for d in "$@"; do DEPS+=("$(abspath "$d")"); done
ROOT="$(find_root "${DEPS[@]}")"
PCAP=""; IPSEC=""
for d in "${DEPS[@]}"; do
	[ -f "$d/usr/lib/libpcap.A.dylib" ] && PCAP="$d"
	[ -f "$d/usr/lib/libipsec.A.dylib" ] && IPSEC="$d"
done
[ -n "$PCAP" ] || { echo "network_cmds: no DEPROOT holds usr/lib/libpcap.A.dylib (pass //base:libpcap_dylib)" >&2; exit 1; }
[ -n "$IPSEC" ] || { echo "network_cmds: no DEPROOT holds usr/lib/libipsec.A.dylib (pass //base:libipsec_dylib)" >&2; exit 1; }
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
write_rsp "$B/arp.rsp" "${base[@]}"
write_rsp "$B/ndp.rsp" "${base[@]}" -DINET6 -DIPSEC_DEBUG -DKAME_SCOPEID
write_rsp "$B/ping6.rsp" "${base[@]}"
write_rsp "$B/traceroute.rsp" "${base[@]}" -DHAVE_SOCKADDR_SA_LEN -isystem "$PCAP/usr/local/include"
write_rsp "$B/traceroute6.rsp" "${base[@]}" -DINET6 -DIPSEC -isystem "$PCAP/usr/local/include" -Itraceroute.tproj   # as.h: Xcode's header map
write_rsp "$B/rarpd.rsp" "${base[@]}" '-DTFTP_DIR=\"/tftpboot\"'
write_rsp "$B/spray.rsp" "${base[@]}"
write_rsp "$B/kdumpd.rsp" "${base[@]}"

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

tool "$B" "$ROOT" "$OUT/usr/sbin/arp" "$B/arp.rsp" arp.tproj/arp.c -- "${LIB[@]}"
tool "$B" "$ROOT" "$OUT/usr/sbin/ndp" "$B/ndp.rsp" ndp.tproj/ndp.c
tool "$B" "$ROOT" "$OUT/sbin/ping6" "$B/ping6.rsp" ping6.tproj/md5.c ping6.tproj/ping6.c -- "${LIB[@]}" \
	-L"$IPSEC/usr/lib" -lipsec
TR=(traceroute.tproj/as.c traceroute.tproj/findsaddr-socket.c traceroute.tproj/ifaddrlist.c traceroute.tproj/version.c)
tool "$B" "$ROOT" "$OUT/usr/sbin/traceroute" "$B/traceroute.rsp" "${TR[@]}" traceroute.tproj/traceroute.c -- "${LIB[@]}" \
	-L"$PCAP/usr/lib" -lpcap
tool "$B" "$ROOT" "$OUT/usr/sbin/traceroute6" "$B/traceroute6.rsp" "${TR[@]}" traceroute6.tproj/traceroute6.c -- "${LIB[@]}" \
	-L"$PCAP/usr/lib" -lpcap -L"$IPSEC/usr/lib" -lipsec
tool "$B" "$ROOT" "$OUT/usr/sbin/rarpd" "$B/rarpd.rsp" rarpd.tproj/rarpd.c
tool "$B" "$ROOT" "$OUT/usr/sbin/spray" "$B/spray.rsp" spray.tproj/spray.c spray.tproj/spray_xdr.c
tool "$B" "$ROOT" "$OUT/usr/libexec/kdumpd" "$B/kdumpd.rsp" kdumpd.tproj/kdumpd.c kdumpd.tproj/kdumpsubs.c -- "${LIB[@]}"
mkdir -p "$OUT/System/Library/LaunchDaemons"
install -m 0644 kdumpd.tproj/com.apple.kdumpd.plist "$OUT/System/Library/LaunchDaemons/"
