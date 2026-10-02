#!/usr/bin/env bash
# SPDX-License-Identifier: BSD-2-Clause
# libipsec from ipsec-1125 (KAME's libipsec as ipsec-tools 0.7 ships it;
# docs/kernel/network.md, "libpcap and tcpdump"): replays ipsec.xcodeproj's
# libipsec target (Deployment). It parses and prints IPsec policy strings
# (ipsec_set_policy(3), ipsec_dump_policy(3)), which ping6 and traceroute6
# link for their -P option and traceroute6 for its "bypass" socket policy.
#   build.sh OUT IPSEC_SRC SYSROOT DEPROOT...   (DEPROOT: //base:root)
# OUT receives usr/lib/libipsec.A.dylib (install name /usr/lib/libipsec.A.dylib,
# DYLIB_CURRENT_VERSION 300, compatibility 1, as macOS 26's) and the target's
# libipsec.dylib link, and, build-only, ipsec_strerror.h in usr/local/include
# (macOS installs no libipsec headers: <netinet6/ipsec.h> declares the
# calls). racoon, setkey and the rest of the project are not built.
source "$(dirname "$0")/../../tools/base/common.sh"
OUT="$(abspath "$1")"; I="$(abspath "$2")"; SYSROOT="$(abspath "$3")"; shift 3
ROOT=""; for d in "$@"; do d="$(abspath "$d")"; [ -f "$d/usr/lib/libSystem.B.dylib" ] && ROOT="$d"; done
[ -n "$ROOT" ] || { echo "libipsec: no DEPROOT holds usr/lib/libSystem.B.dylib (pass //base:root)" >&2; exit 1; }
B="$(mktemp -d)"; trap 'rm -rf "$B"' EXIT
T="$I/ipsec-tools"
D="$B/derived"; mkdir -p "$D"
# YACCFLAGS -d -p__libipsec and LEXFLAGS -P__libipsec, through the
# toolchain's yacc (bison 2.3) and flex 2.6.4, as Xcode's rules run them;
# policy_token.l includes the parser's header as y.tab.h.
(cd "$D" && xcrun yacc -d -p__libipsec -o policy_parse.c "$T/libipsec/policy_parse.y" 2> "$B/yacc.log") ||
	{ cat "$B/yacc.log" >&2; exit 1; }
cp "$D/policy_parse.h" "$D/y.tab.h"
(cd "$D" && xcrun flex -P__libipsec -o policy_token.c "$T/libipsec/policy_token.l")
# HAVE_CONFIG_H (Common/config.h); HEADER_SEARCH_PATHS ../Common and the
# project's System.framework PrivateHeaders (net/pfkeyv2.h); racoon/var.h
# through Xcode's header map. Default visibility, -Os. Warning flags are
# left out.
write_rsp "$B/cflags" "${TARGET_FLAGS[@]}" -Os -DHAVE_CONFIG_H=1 -I"$T/Common" -I"$T/libipsec" -I"$T/racoon" \
	-I"$D" $(sysroot_flags "$SYSROOT")
compile "$B/obj" "$B/cflags" "$T/libipsec/ipsec_dump_policy.c" "$T/libipsec/ipsec_get_policylen.c" \
	"$T/libipsec/ipsec_strerror.c" "$D/policy_parse.c" "$D/policy_token.c"
mkdir -p "$OUT/usr/lib"
xcrun ld -arch arm64 -platform_version macos 26.0 26.0 -dylib -dead_strip -adhoc_codesign \
	-install_name /usr/lib/libipsec.A.dylib -current_version 300 -compatibility_version 1 \
	-syslibroot "$ROOT" "$B"/obj/*.o -lSystem -o "$OUT/usr/lib/libipsec.A.dylib"
ln -sf libipsec.A.dylib "$OUT/usr/lib/libipsec.dylib"
mkdir -p "$OUT/usr/local/include"
install -m 0444 "$T/libipsec/ipsec_strerror.h" "$OUT/usr/local/include/"
