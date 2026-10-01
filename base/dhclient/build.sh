#!/usr/bin/env bash
# SPDX-License-Identifier: BSD-2-Clause
# dhclient from FreeBSD's sbin/dhclient (docs/kernel/network.md, "DHCP"):
# the program, its script and its configuration, as FreeBSD's Makefile
# installs them.
#   build.sh OUT DHCLIENT_SRC SYSROOT DEPROOT...   (DEPROOT: //base:root, //base:libutil_dylib)
# OUT receives sbin/dhclient, sbin/dhclient-script (mode 0555) and
# private/etc/dhclient.conf.
#
# Apple's DHCP client is IPConfiguration (bootp-527), a configd plugin; it
# needs configd and SystemConfiguration, which NeoDarwin doesn't build. By
# the reuse order (docs/repository.md §3.1), FreeBSD's comes next.
#
# The Makefile's settings: its SRCS, -DINET6 (MK_INET6_SUPPORT), and
# -DWITHOUT_NETLINK (xnu has no netlink: the IPv6-only option, RFC 8925,
# then never finds IPv6 connectivity and dhclient keeps IPv4, as on a
# FreeBSD built without netlink). Without WITH_CASPER (no libcasper). LIBADD
# util: Apple's libutil has pidfile(3).
# FreeBSD's kernel and libc interfaces xnu and Darwin's libc lack come from
# compat/: Capsicum (no-ops; dhclient falls back to chroot and an
# unprivileged user, as it does on a kernel without Capsicum), Casper's
# syslog (syslog(3)), <sys/endian.h>, and nd_dhclient_compat.h, included
# first in every source (nitems, setproctitle, daemonfd, reallocarray).
# patches/: 0001 the BPF and routing-socket requests xnu doesn't have, the
# pid file's directory, and Darwin's one-way pipes and setuid(2); 0002 dhclient-script for Darwin's ifconfig and
# NeoDarwin's resolv.conf and mDNSResponder.
source "$(dirname "$0")/../../tools/base/common.sh"
source "$(dirname "$0")/../commands.sh"
OUT="$(abspath "$1")"; D="$(abspath "$2")"; SYSROOT="$(abspath "$3")"; shift 3
DEPS=(); for d in "$@"; do DEPS+=("$(abspath "$d")"); done
ROOT="$(find_root "${DEPS[@]}")"
UTIL=""; for d in "${DEPS[@]}"; do [ -f "$d/usr/lib/libutil.dylib" ] && UTIL="$d"; done
[ -n "$UTIL" ] || { echo "dhclient: no DEPROOT holds usr/lib/libutil.dylib (pass //base:libutil_dylib)" >&2; exit 1; }
PROJ="$(cd "$(dirname "$0")" && pwd)"
B="$(mktemp -d)"; trap 'rm -rf "$B"' EXIT
D="$(stage_src "$D/sbin/dhclient" "$B/src" "$PROJ/patches")"   # patches/ applied
cd "$D"

write_rsp "$B/cflags" "${TARGET_FLAGS[@]}" -Os -fno-common -DINET6 -DWITHOUT_NETLINK \
	-include "$PROJ/compat/nd_dhclient_compat.h" -I"$PROJ/compat" -I"$UTIL/usr/local/include" \
	$(cmd_sysroot_flags "$SYSROOT")
tool "$B" "$ROOT" "$OUT/sbin/dhclient" "$B/cflags" dhclient.c clparse.c alloc.c dispatch.c hash.c bpf.c \
	options.c tree.c conflex.c errwarn.c inet.c packet.c convert.c tables.c parse.c privsep.c inet6.c \
	-- -L"$UTIL/usr/lib" -lutil
install -m 0555 dhclient-script "$OUT/sbin/dhclient-script"
mkdir -p "$OUT/private/etc"
install -m 0644 dhclient.conf "$OUT/private/etc/dhclient.conf"
