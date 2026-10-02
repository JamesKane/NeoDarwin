#!/usr/bin/env bash
# SPDX-License-Identifier: BSD-2-Clause
# tcpdump from tcpdump-153 (tcpdump 4.99.1 with Apple's pktap, pcapng and
# packet-metadata additions; docs/kernel/network.md, "libpcap and
# tcpdump"): replays tcpdump.xcodeproj's tcpdump target (Release) and its
# gen_tcpdump_version dependency.
#   build.sh OUT TCPDUMP_SRC SYSROOT DEPROOT...
#   (DEPROOT: //base:root, //base:libpcap_dylib, //base:libcrypto_dylib)
# OUT receives usr/sbin/tcpdump (INSTALL_PATH; 0555, root-only in use
# because /dev/bpf* are root's, mode 0600, as on macOS).
# Configuration: Apple's build doesn't run configure; the drop's config.h
# is used as it is, with the target's macOS OTHER_CFLAGS on top
# (HAVE_LIBCRYPTO, HAVE_OPENSSL_EVP_H, GUESS_TSO). macOS links LibreSSL's
# libssl and libcrypto (/usr/local/libressl at build time) for ESP
# decryption (-E); NeoDarwin's libcrypto is OpenSSL 3.5 (base/openssl,
# headers and -lcrypto links in usr/local/openssl), as FreeBSD's tcpdump
# links it. libssl is left out: tcpdump calls nothing in it.
# Patch 0001 leaves out the copies to the unified log (NEODARWIN_NO_LOG_STORE).
# Apple signs tcpdump with com.apple.private.skywalk.observe-all (Skywalk
# flow observation); NeoDarwin has no Skywalk or policy that reads it, so it
# is signed ad hoc without, as ping is.
source "$(dirname "$0")/../../tools/base/common.sh"
source "$(dirname "$0")/../commands.sh"
OUT="$(abspath "$1")"; T="$(abspath "$2")"; SYSROOT="$(abspath "$3")"; shift 3
DEPS=(); for d in "$@"; do DEPS+=("$(abspath "$d")"); done
ROOT="$(find_root "${DEPS[@]}")"
PCAP=""; CRYPTO=""
for d in "${DEPS[@]}"; do
	[ -f "$d/usr/lib/libpcap.A.dylib" ] && PCAP="$d"
	[ -f "$d/usr/lib/libcrypto.3.dylib" ] && CRYPTO="$d"
done
[ -n "$PCAP" ] || { echo "tcpdump: no DEPROOT holds usr/lib/libpcap.A.dylib (pass //base:libpcap_dylib)" >&2; exit 1; }
[ -n "$CRYPTO" ] || { echo "tcpdump: no DEPROOT holds usr/lib/libcrypto.3.dylib (pass //base:libcrypto_dylib)" >&2; exit 1; }
PROJ="$(cd "$(dirname "$0")" && pwd)"
B="$(mktemp -d)"; trap 'rm -rf "$B"' EXIT
T="$(stage_src "$T" "$B/src" "$PROJ/patches")"   # patches/ applied
cd "$T"
D="$B/derived"; mkdir -p "$D"
# gen_tcpdump_version: RC_ProjectSourceVersion, the tag's.
echo 'const char apple_version_string[] = "Apple version 153";' > "$D/tcpdump_version.h"

# The project's GCC_PREPROCESSOR_DEFINITIONS and OTHER_CFLAGS for macOS;
# HEADER_SEARCH_PATHS: System.framework's PrivateHeaders (net/pktap.h and
# libpcap's private headers, here libpcap's usr/local/include), tcpdump,
# usr/include, tcpdump/missing, the derived files, the crypto library's
# headers. gnu99, Release -Os. Warning flags are left out. SPI_AVAILABLE as
# the internal SDK has it (libpcap's compat header): tcpdump declares its
# own SPI with it.
write_rsp "$B/cflags" "${TARGET_FLAGS[@]}" -std=gnu99 -Os -fno-common \
	-DHAVE_CONFIG_H '-D_U_=__attribute__((unused))' -DHAVE_OPENSSL_EVP_H=1 -DHAVE_LIBCRYPTO=1 -DGUESS_TSO \
	-DNEODARWIN_NO_LOG_STORE -include "$PROJ/../libpcap/compat/nd_spi_available.h" \
	-isystem "$PCAP/usr/local/include" -I"$T/tcpdump" -I"$T/tcpdump/missing" -I"$D" \
	$(sysroot_flags "$SYSROOT") -isystem "$CRYPTO/usr/local/openssl/include"
SRCS=(addrtoname.c addrtostr.c af.c ascii_strcasecmp.c bpf_dump.c checksum.c cpack.c fptype.c gmpls.c in_cksum.c
	ipproto.c l2vpn.c machdep.c netdissect-alloc.c netdissect.c nlpid.c ntp.c oui.c parsenfsfh.c pktaputil.c
	pktmetadatafilter.c print.c print_pktap.c signature.c smbutil.c strtoaddr.c tcpdump.c util-print.c)
# and every print-*.c but print-pktap.c (tcpdump.org's pktap printer; the
# target has Apple's print_pktap.c instead).
SRCS+=($(cd tcpdump && ls print-*.c | grep -vx print-pktap.c))
compile "$B/obj" "$B/cflags" $(printf 'tcpdump/%s\n' "${SRCS[@]}")
# The Frameworks phase links /usr/lib/libpcap.dylib (install name
# libpcap.A.dylib); OTHER_LDFLAGS -lssl -lcrypto (above).
link_tool "$ROOT" "$OUT/usr/sbin/tcpdump" "$B"/obj/*.o -L"$PCAP/usr/lib" -lpcap \
	-L"$CRYPTO/usr/local/openssl/lib" -lcrypto
