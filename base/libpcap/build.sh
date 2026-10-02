#!/usr/bin/env bash
# SPDX-License-Identifier: BSD-2-Clause
# libpcap from libpcap-144 (libpcap 1.10.1 with Apple's pktap and pcapng
# additions; docs/kernel/network.md, "libpcap and tcpdump"): replays
# libpcap.xcodeproj's libpcap target (Release) and its gen_libpcap_version
# dependency.
#   build.sh OUT LIBPCAP_SRC SYSROOT DEPROOT...   (DEPROOT: //base:root)
# OUT receives usr/lib/libpcap.A.dylib (install name /usr/lib/libpcap.A.dylib,
# current and compatibility version 1, as macOS 26's) and the target's
# libpcap.dylib link, and, build-only, the public headers in
# usr/local/include (pcap.h, pcap-bpf.h, pcap-namedb.h, pcap/*.h; macOS:
# usr/include) with the private ones (pcap/pcap-ng.h, pcap/pcap-util.h and
# the PRIVATE parts of the others: tcpdump uses them) as the target's script
# leaves them in System.framework's PrivateHeaders. pcap-config and the man
# pages are left out.
# Configuration: Apple's build doesn't run configure; the drop's config.h
# (configure --prefix=/usr --enable-ipv6, run on Apple's side) is used as it
# is. grammar.y (configured, %pure-parser) and scanner.l go through the
# toolchain's bison 2.3 and flex 2.6.4, as the target's build rule runs them.
source "$(dirname "$0")/../../tools/base/common.sh"
OUT="$(abspath "$1")"; L="$(abspath "$2")"; SYSROOT="$(abspath "$3")"; shift 3
ROOT=""; for d in "$@"; do d="$(abspath "$d")"; [ -f "$d/usr/lib/libSystem.B.dylib" ] && ROOT="$d"; done
[ -n "$ROOT" ] || { echo "libpcap: no DEPROOT holds usr/lib/libSystem.B.dylib (pass //base:root)" >&2; exit 1; }
PROJ="$(cd "$(dirname "$0")" && pwd)"
B="$(mktemp -d)"; trap 'rm -rf "$B"' EXIT
L="$(stage_src "$L" "$B/src" "$PROJ/patches")"   # patches/ applied
cd "$L"
D="$B/derived"; mkdir -p "$D"

# gen_libpcap_version: pcap_version.h from libpcap.plist's OpenSourceVersion
# and RC_ProjectSourceVersion (the tag's).
v="$(plutil -extract OpenSourceVersion raw libpcap.plist)"
echo "static const char pcap_version_string[] = \"libpcap version $v -- Apple version 144\";" > "$D/pcap_version.h"
# The build rule for scanner.l: the parser and the scanner.
(cd "$D" && xcrun bison --yacc --name-prefix=pcap_ -d "$L/libpcap/grammar.y" --output-file=grammar.c \
	--defines=grammar.h 2> "$B/bison.log") || { cat "$B/bison.log" >&2; exit 1; }
(cd "$D" && xcrun flex -Ppcap_ --header-file=scanner.h --nounput -oscanner.c "$L/libpcap/scanner.l")

# The project's GCC_PREPROCESSOR_DEFINITIONS; HEADER_SEARCH_PATHS libpcap,
# System.framework's PrivateHeaders (net/pktap.h, net/iptap.h) and the
# derived files; gnu99, Release -Os; GCC_SYMBOLS_PRIVATE_EXTERN, so only
# what PCAP_API marks is exported. Warning flags are left out.
write_rsp "$B/cflags" "${TARGET_FLAGS[@]}" -std=gnu99 -Os -fno-common -fvisibility=hidden \
	-DHAVE_CONFIG_H '-D_U_=__attribute__((unused))' -include "$PROJ/compat/nd_spi_available.h" -Dyylval=pcap_lval -DPRIVATE -DHAVE_PKTAP_API \
	-I"$L/libpcap" -I"$D" $(sysroot_flags "$SYSROOT")
SRCS=(bpf_dump.c bpf_filter.c bpf_image.c etherent.c fad-getad.c fmtutils.c gencode.c nametoaddr.c optimize.c
	pcap-bpf.c pcap-apple-stubs.c pcap-common.c pcap-darwin.c pcap-util.c pcap.c pcapng.c savefile.c sf-pcap.c
	sf-pcapng.c)
compile "$B/obj" "$B/cflags" $(printf "$L/libpcap/%s\n" "${SRCS[@]}") "$D/grammar.c" "$D/scanner.c"
# PRODUCT_NAME libpcap.A, INSTALL_PATH /usr/lib; the project's versions are
# Xcode's defaults (1), as macOS 26's libpcap.A.dylib has. The
# apple-generic version symbols are hidden (GCC_SYMBOLS_PRIVATE_EXTERN).
mkdir -p "$OUT/usr/lib"
xcrun ld -arch arm64 -platform_version macos 26.0 26.0 -dylib -dead_strip -adhoc_codesign \
	-install_name /usr/lib/libpcap.A.dylib -current_version 1 -compatibility_version 1 \
	-syslibroot "$ROOT" "$B"/obj/*.o -lSystem -o "$OUT/usr/lib/libpcap.A.dylib"
ln -sf libpcap.A.dylib "$OUT/usr/lib/libpcap.dylib"

# Headers: the target's Headers phase puts pcap.h, pcap-bpf.h and
# pcap-namedb.h in /usr/include and pcap/*.h in System.framework's
# PrivateHeaders; its script then copies the latter, PRIVATE removed with
# unifdef, to /usr/include/pcap (all but pcap-ng.h and pcap-util.h). Here
# the private variants are installed (build-only): tcpdump's target searches
# PrivateHeaders.
H="$OUT/usr/local/include"; mkdir -p "$H/pcap"
install -m 0444 libpcap/pcap.h libpcap/pcap-bpf.h libpcap/pcap-namedb.h "$H/"
install -m 0444 libpcap/pcap/*.h libpcap/pcap-util.h "$H/pcap/"
