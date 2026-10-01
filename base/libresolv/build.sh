#!/usr/bin/env bash
# SPDX-License-Identifier: BSD-2-Clause
# libresolv from libresolv-93 (docs/kernel/network.md, "Name resolution"):
# replays libresolv.xcodeproj's libresolv target with
# xcodescripts/libresolv.xcconfig, and its headers.sh and links.sh phases.
#   build.sh OUT LIBRESOLV_SRC SYSROOT DEPROOT...   (DEPROOT: //base:root)
# OUT receives usr/lib/libresolv.9.dylib and the libresolv.dylib link
# (links.sh), and, build-only (in usr/local, which the image leaves out, as
# OpenSSL's are), the headers the project installs, for the commands built
# against it (OpenSSH): usr/local/libresolv/include/{resolv.h,nameser.h,
# dns.h,dns_util.h,dns_private.h} and arpa/nameser.h -> ../nameser.h
# (headers.sh).
#
# The resolver reads /etc/resolv.conf (a link to /var/run/resolv.conf, which
# the DHCP client writes) and /etc/resolver/*. On macOS dns.c asks configd
# first (dnsinfo's dns_configuration_copy()); NeoDarwin's
# libsystem_configuration stand-in answers that there is no configuration,
# and dns.c reads the files, as it does when configd isn't running. The
# declarations come from configd-1385.0.7's dnsinfo.h (@apple_configd_dnsinfo,
# pinned by configd.lock), and res_query.c's notify_register_plain() from
# Libnotify-344.0.1's notify_private.h (@apple_libnotify): both are headers
# of Apple's internal SDK that the sysroot doesn't stage.
#
# HMAC-MD5 (TSIG, hmac_link.c): Apple's internal <md5.h> maps MD5Init and
# friends to CommonCrypto's CC_MD5_*; NeoDarwin has no libcommonCrypto. The
# library links FreeBSD's MD5 (sys/crypto/md5c.c, from ndcrypto's pinned
# files, compiled as dyld's copy is) privately, and hmac_link.c finds
# FreeBSD's <md5.h> (sys/sys/md5.h).
# Patch 0001: the dyld version set res_state.c tests, which
# AvailabilityVersions-155's dyld_priv.h predates.
#
# The xcconfig: gnu99, -fno-common, VERSION_INFO_PREFIX hidden,
# FRAMEWORK_VERSION 9 (the product is libresolv.9), install path /usr/lib.
# BSD.xcconfig (Apple-internal) gives the usual -Os and private externs off:
# the library exports its non-static symbols. Warning flags are left out.
source "$(dirname "$0")/../../tools/base/common.sh"
source "$(dirname "$0")/../commands.sh"
OUT="$(abspath "$1")"; R="$(abspath "$2")"; SYSROOT="$(abspath "$3")"; shift 3
DEPS=(); for d in "$@"; do DEPS+=("$(abspath "$d")"); done
ROOT="$(find_root "${DEPS[@]}")"
PROJ="$(cd "$(dirname "$0")" && pwd)"
# find_tree FILE: the repository next to LIBRESOLV_SRC that holds FILE (as dyld's build.sh).
find_tree() {
	local d; for d in "$(dirname "$R")"/*/; do [ -f "$d$1" ] && { printf '%s' "${d%/}"; return; }; done
	echo "libresolv: no repository next to $R holds $1" >&2; exit 1
}
DNSINFO="$(find_tree dnsinfo/dnsinfo.h)/dnsinfo"
NOTIFY="$(find_tree notify_private.h)"
FREEBSD="$(find_tree sys/crypto/md5c.c)"
NDCRYPTO="$(cd "$PROJ/../../kernel/neodarwin/crypto" && pwd)"
B="$(mktemp -d)"; trap 'rm -rf "$B"' EXIT
R="$(stage_src "$R" "$B/src" "$PROJ/patches")"   # patches/ applied
cd "$R"

# The project's headers by quoted and angle include from its own directory
# (the sources include <dns.h>, <dns_util.h> and <port_before.h> as headers
# of Apple's SDK), then dnsinfo.h and notify_private.h, then the sysroot
# (Libinfo's private si_module.h, for dns_async.c).
base=("${TARGET_FLAGS[@]}" -Os -std=gnu99 -fno-common -I"$R" -I"$DNSINFO" -idirafter "$NOTIFY")
write_rsp "$B/cflags" "${base[@]}" $(sysroot_flags "$SYSROOT")
write_rsp "$B/hmac.rsp" "${base[@]}" -include nd_freebsd.h -I"$NDCRYPTO/compat" -I"$FREEBSD/sys/sys" \
	$(sysroot_flags "$SYSROOT")
write_rsp "$B/md5.rsp" "${TARGET_FLAGS[@]}" -Os -std=c17 -fvisibility=hidden -w -include nd_freebsd.h \
	-I"$NDCRYPTO/compat" -I"$FREEBSD/sys" $(sysroot_flags "$SYSROOT")
write_vers "$B/vers.c" libresolv libresolv 93 '__attribute__((visibility("hidden")))'
srcs=(base64.c dns_async.c dns_util.c dns.c dst_api.c mtctxres.c ns_date.c ns_name.c ns_netint.c
	ns_parse.c ns_print.c ns_samedomain.c ns_sign.c ns_ttl.c ns_verify.c res_comp.c res_data.c res_debug.c
	res_findzonecut.c res_init.c res_mkquery.c res_mkupdate.c res_query.c res_send.c res_sendsigned.c
	res_state.c res_update.c support.c)
compile "$B/obj" "$B/cflags" "${srcs[@]}" "$B/vers.c"
compile "$B/obj" "$B/hmac.rsp" hmac_link.c
compile "$B/obj" "$B/md5.rsp" "$FREEBSD/sys/crypto/md5c.c"

mkdir -p "$OUT/usr/lib"
xcrun ld -arch arm64 -platform_version macos 26.0 26.0 -dylib -dead_strip -adhoc_codesign -syslibroot "$ROOT" \
	-install_name /usr/lib/libresolv.9.dylib -current_version 93 -compatibility_version 1 \
	"$B"/obj/*.o -lSystem -o "$OUT/usr/lib/libresolv.9.dylib"
ln -s libresolv.9.dylib "$OUT/usr/lib/libresolv.dylib"

# The headers phase (Public: dns.h, dns_util.h, nameser.h, resolv.h;
# Private: dns_private.h) and headers.sh's arpa/nameser.h link.
H="$OUT/usr/local/libresolv/include"; mkdir -p "$H/arpa"
install -m 0644 dns.h dns_util.h nameser.h resolv.h dns_private.h "$H/"
ln -s ../nameser.h "$H/arpa/nameser.h"
