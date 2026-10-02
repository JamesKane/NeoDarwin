#!/usr/bin/env bash
# SPDX-License-Identifier: BSD-2-Clause
# dhcp6c, the WIDE project's DHCPv6 client, from FreeBSD's net/dhcp6 port
# (hrs@'s fork of WIDE-DHCPv6 20080615; docs/kernel/network.md, "IPv6 DNS:
# RDNSS and DHCPv6"), and NeoDarwin's two scripts around it:
#   build.sh OUT WIDE_SRC SYSROOT DEPROOT...   (DEPROOT: //base:root)
# OUT receives usr/sbin/dhcp6c (the port's sbindir is /usr/local/sbin; the
# base's tools are in /usr/sbin), usr/libexec/dhcp6c-managed and
# dhcp6c-other (one script, dhcp6c-start: rtsold's M, O and "always"
# scripts, which start dhcp6c) and usr/libexec/dhcp6c-script (dhcp6c's
# script, which hands the name servers to resolvconf(8)).
#
# Apple's DHCPv6 client is IPConfiguration's (bootp), inseparable from its
# service threads, CoreFoundation and SystemConfiguration's private
# interfaces; FreeBSD's base has none. FreeBSD's ports have two: net/dhcp6
# (KAME's client, the same lineage as rtsold, which only speaks DHCPv6) and
# dhcpcd (which would also take over router advertisements and SLAAC from
# the kernel, and has no Darwin port). dhcp6c is the one that leaves the
# kernel's autoconfiguration alone.
#
# The port's configure, answered for Darwin (a KAME stack: in6.h defines
# __KAME__): the functions it would replace from missing/ are all in libc
# (arc4random, strlcpy, strlcat, getifaddrs, daemon, warnx), struct sockaddr
# has sa_len, TAILQ_FOREACH_REVERSE takes the new argument order;
# --sysconfdir=/etc (dhcp6c.conf, dhcp6cctlkey), --with-localdbdir=/var/db
# (the DUID) as the port's. The parser and scanner go through the
# toolchain's yacc and lex, as the Makefile's rules run them. The server,
# relay and dhcp6ctl aren't built. patches/: 0001 the interface in the
# script's environment; 0002 a stateless reply without a server ID (QEMU's).
source "$(dirname "$0")/../../tools/base/common.sh"
source "$(dirname "$0")/../commands.sh"
OUT="$(abspath "$1")"; D="$(abspath "$2")"; SYSROOT="$(abspath "$3")"; shift 3
DEPS=(); for d in "$@"; do DEPS+=("$(abspath "$d")"); done
ROOT="$(find_root "${DEPS[@]}")"
PROJ="$(cd "$(dirname "$0")" && pwd)"
B="$(mktemp -d)"; trap 'rm -rf "$B"' EXIT
D="$(stage_src "$D" "$B/src" "$PROJ/patches")"   # patches/ applied
cd "$D"

xcrun yacc -d cfparse.y 2> "$B/yacc.log" || { cat "$B/yacc.log" >&2; exit 1; }
mv y.tab.c cfparse.c
xcrun lex --noyywrap cftoken.l   # the Makefile links LEXLIB (-ll) for yywrap(), which returns 1
mv lex.yy.c cftoken.c

write_rsp "$B/cflags" "${TARGET_FLAGS[@]}" -Os -fno-common -I. \
	-DHAVE_IF_NAMETOINDEX=1 -DHAVE_GETIFADDRS=1 -DHAVE_STRLCPY=1 -DHAVE_STRLCAT=1 -DHAVE_DAEMON=1 \
	-DHAVE_WARNX=1 -DHAVE_ARC4RANDOM=1 -DHAVE_CLOCK_GETTIME=1 -DHAVE_SA_LEN=1 -DHAVE_STDARG_H=1 \
	-DHAVE_TAILQ_FOREACH_REVERSE=1 -DHAVE_ANSI_FUNC=1 -DHAVE_GCC_FUNCTION=1 -DHAVE_FCNTL_H=1 \
	-DHAVE_SYS_IOCTL_H=1 -DHAVE_SYS_TIME_H=1 -DHAVE_SYSLOG_H=1 -DHAVE_UNISTD_H=1 -DHAVE_IFADDRS_H=1 \
	-DSTDC_HEADERS=1 -DTIME_WITH_SYS_TIME=1 -DINET6 -D__APPLE_USE_RFC_3542 \
	'-DSYSCONFDIR=\"/etc\"' '-DLOCALDBDIR=\"/var/db\"' \
	$(cmd_sysroot_flags "$SYSROOT")
tool "$B" "$ROOT" "$OUT/usr/sbin/dhcp6c" "$B/cflags" dhcp6c.c common.c config.c prefixconf.c dhcp6c_ia.c \
	timer.c dhcp6c_script.c if.c base64.c auth.c dhcp6_ctl.c addrconf.c lease.c cfparse.c cftoken.c
mkdir -p "$OUT/usr/libexec"
install -m 0555 "$PROJ/dhcp6c-start" "$OUT/usr/libexec/dhcp6c-other"
ln -f "$OUT/usr/libexec/dhcp6c-other" "$OUT/usr/libexec/dhcp6c-managed"
install -m 0555 "$PROJ/dhcp6c-script" "$OUT/usr/libexec/dhcp6c-script"
