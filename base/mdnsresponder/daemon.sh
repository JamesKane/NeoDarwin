#!/usr/bin/env bash
# SPDX-License-Identifier: BSD-2-Clause
# The mDNSResponder daemon from mDNSResponder-2881.0.25 (docs/kernel/network.md,
# "Name resolution"): the published POSIX daemon (mDNSPosix/, the Makefile's
# mdnsd), the server behind libsystem_dnssd (build.sh) and so behind
# Libinfo's mdns module (getaddrinfo, gethostbyname, res_query).
#   daemon.sh OUT MDNSRESPONDER_SRC SYSROOT DEPROOT...   (DEPROOT: //base:root)
# OUT receives usr/sbin/mDNSResponder (macOS's path for its daemon),
# usr/bin/dns-sd and System/Library/LaunchDaemons/com.apple.mDNSResponder.plist.
#
# macOS's daemon is mDNSMacOSX/, last published in 1310.140.1 and built on
# closed frameworks (SystemConfiguration, Network.framework, XPC). The
# published tree's daemon for other systems is mDNSPosix's: the same core
# (mDNSCore: mDNS.c, uDNS.c, DNSCommon.c), the same client server
# (mDNSShared/uds_daemon.c) and wire protocol (dnssd_ipc), with a portable
# platform layer. It answers unicast DNS from the servers in
# /etc/resolv.conf (re-read on SIGHUP: dhclient-script sends it) and
# multicast DNS (.local) on every interface.
#
# Settings: the Makefile's DAEMONOBJS and CFLAGS_COMMON, with its os=x
# (Darwin) CFLAGS_OS less -Werror and the old compiler's flags
# (-no-cpp-precomp, __MAC_OS_X_VERSION_MIN_REQUIRED), and without TLS
# (POSIX_HAS_TLS needs mbedtls: no DNS over TLS). MDNS_UDS_SERVERPATH is
# left at dnssd_ipc.h's default, /var/run/mDNSResponder, where
# libsystem_dnssd connects; the Makefile's /var/run/mdnsd is for a Linux
# install. patches/ (build.sh's, the client library's files) are applied
# too, then daemon-patches/: 0001 takes the core's lock where the daemon
# re-reads resolv.conf (install_headers.sh applies patches/ to the client's
# headers alone, so the daemon's patches live apart). The pid file is
# uds_daemon.c's default, /var/run/mDNSResponder.pid (dhclient-script
# signals the daemon through it).
source "$(dirname "$0")/../../tools/base/common.sh"
source "$(dirname "$0")/../commands.sh"
OUT="$(abspath "$1")"; M="$(abspath "$2")"; SYSROOT="$(abspath "$3")"; shift 3
DEPS=(); for d in "$@"; do DEPS+=("$(abspath "$d")"); done
ROOT="$(find_root "${DEPS[@]}")"
PROJ="$(cd "$(dirname "$0")" && pwd)"
B="$(mktemp -d)"; trap 'rm -rf "$B"' EXIT
M="$(stage_src "$M" "$B/src" "$PROJ/patches")"   # patches/ applied
for p in "$PROJ"/daemon-patches/*.patch; do patch -d "$M" -p1 --quiet < "$p"; done
cd "$M"

# CFLAGS_COMMON and CFLAGS_OS (os=x), and the prod build's -Os and
# MDNS_DEBUGMSGS=0. Warning flags are left out.
write_rsp "$B/cflags" "${TARGET_FLAGS[@]}" -Os -fwrapv -fno-common \
	-ImDNSCore -ImDNSShared -ImDNSShared/utilities -IDSO -IServiceRegistration \
	-DPOSIX_BUILD -DMDNS_NO_STRICT=1 -DMDNS_DEBUGMSGS=0 \
	-DHAVE_IPV6 -DHAVE_STRLCPY=1 -D__APPLE_USE_RFC_2292 -DmDNSResponderVersion=2881.0.25 \
	$(cmd_sysroot_flags "$SYSROOT")

# DAEMONOBJS, less $(TLSOBJS).
tool "$B" "$ROOT" "$OUT/usr/sbin/mDNSResponder" "$B/cflags" \
	mDNSPosix/PosixDaemon.c mDNSPosix/mDNSPosix.c mDNSPosix/mDNSUNP.c mDNSCore/mDNS.c mDNSCore/DNSDigest.c \
	mDNSCore/uDNS.c mDNSCore/DNSCommon.c mDNSShared/uds_daemon.c mDNSShared/mDNSDebug.c mDNSShared/dnssd_ipc.c \
	mDNSShared/GenLinkedList.c mDNSShared/PlatformCommon.c mDNSShared/ClientRequests.c DSO/dso.c \
	DSO/dso-transport.c mDNSShared/dnssd_clientshim.c mDNSShared/utilities/mdns_addr_tailq.c \
	mDNSShared/utilities/misc_utilities.c

# Clients/Makefile's dns-sd (macOS's /usr/bin/dns-sd), with the daemon's
# flags (SUPMAKE_CFLAGS): it talks to the daemon through libsystem_dnssd.
tool "$B" "$ROOT" "$OUT/usr/bin/dns-sd" "$B/cflags" Clients/dns-sd.c Clients/ClientCommon.c

mkdir -p "$OUT/System/Library/LaunchDaemons"
install -m 0644 "$PROJ/com.apple.mDNSResponder.plist" "$OUT/System/Library/LaunchDaemons/"
