#!/usr/bin/env bash
# SPDX-License-Identifier: BSD-2-Clause
# resolvconf(8) from FreeBSD's contrib/openresolv (docs/kernel/network.md,
# "IPv6 DNS: RDNSS and DHCPv6"), installed as FreeBSD's sbin/resolvconf
# Makefile installs it: /sbin/resolvconf and the libc subscriber, which
# writes resolv.conf, with the Makefile's substitutions. NeoDarwin's
# differences: the subscribers go in /usr/libexec/resolvconf (Darwin has no
# /libexec); only libc is installed (dnsmasq, named, pdnsd, pdns_recursor
# and unbound configure servers NeoDarwin doesn't ship); there is no
# service(8) to restart a subscriber's daemon, so RESTARTCMD fails
# (resolvconf -r), and /etc/resolvconf.conf is NeoDarwin's: the merged file
# is /var/run/resolv.conf (/etc/resolv.conf is macOS's link to it), and
# mDNSResponder, which answers every lookup, rereads it on SIGHUP.
#   build.sh OUT OPENRESOLV_SRC SYSROOT DEPROOT...
# OUT receives sbin/resolvconf (0555), usr/libexec/resolvconf/libc (0444)
# and private/etc/resolvconf.conf (0644).
source "$(dirname "$0")/../../tools/base/common.sh"
OUT="$(abspath "$1")"; D="$(abspath "$2")"
PROJ="$(cd "$(dirname "$0")" && pwd)"
LIBEXECDIR=/usr/libexec/resolvconf
subst() {
	sed -e 's:@SYSCONFDIR@:/etc:g' \
		-e "s:@LIBEXECDIR@:$LIBEXECDIR:g" \
		-e 's:@VARDIR@:/var/run/resolvconf:g' \
		-e 's:@RESTARTCMD@:false:g' \
		-e 's:@RCDIR@:/etc/rc.d:g' \
		-e 's:@SBINDIR@:/sbin:g' \
		"$1" > "$2"
}
mkdir -p "$OUT/sbin" "$OUT$LIBEXECDIR" "$OUT/private/etc"
subst "$D/contrib/openresolv/resolvconf.in" "$OUT/sbin/resolvconf"
subst "$D/contrib/openresolv/libc.in" "$OUT$LIBEXECDIR/libc"
chmod 0555 "$OUT/sbin/resolvconf"
chmod 0444 "$OUT$LIBEXECDIR/libc"
install -m 0644 "$PROJ/resolvconf.conf" "$OUT/private/etc/resolvconf.conf"
