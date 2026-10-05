#!/usr/bin/env bash
# SPDX-License-Identifier: BSD-2-Clause
# OpenSSL 3.5 (docs/base/session.md, "Loopback and sshd"; "The crypto
# library"): the library FreeBSD's base ships (crypto/openssl, OpenSSL
# 3.5.8 at freebsd-src 050683bb8e13), at the newest 3.5 LTS patch release,
# 3.5.9. It replaces LibreSSL 3.3.6, macOS's libcrypto, which dates from
# 2022 and has unfixed advisories.
#   build.sh OUT OPENSSL_SRC SYSROOT DEPROOT...   (DEPROOT: //base:root)
# OUT receives:
#   usr/lib/libcrypto.3.dylib and usr/lib/libssl.3.dylib (OpenSSL's own
#     install names; macOS has no libcrypto.3, so nothing it ships clashes);
#   usr/lib/ossl-modules/legacy.dylib (the legacy provider, as FreeBSD
#     builds it) and usr/lib/engines-3/{capi,loader_attic}.dylib (FreeBSD's
#     engines for aarch64, less devcrypto: no /dev/crypto);
#   usr/bin/openssl; private/etc/ssl/openssl.cnf (OPENSSLDIR, macOS's
#     /private/etc/ssl; FreeBSD's is /etc/ssl);
#   build-only, in usr/local/openssl: the headers (include/openssl) and
#     libcrypto.dylib and libssl.dylib links for -lcrypto, as LibreSSL's
#     were in usr/local/libressl. No unversioned library is installed in
#     usr/lib (macOS's /usr/lib/libcrypto.dylib is a stub that aborts).
# Configure is OpenSSL's own (Perl). It runs no compile, link or run checks:
# the target (darwin64-arm64) and the options below are its whole answer,
# pinned here. FreeBSD's choices (its configuration.h): no-aria no-idea
# no-mdc2 no-sm2 no-sm3 no-sm4 and ec_nistp_64_gcc_128 on 64-bit, the rest
# OpenSSL's defaults (engines, the legacy provider and deprecated APIs on;
# ssl3, md2, rc5, zlib, ktls off). Also off: padlock (x86 only; FreeBSD
# builds it only for amd64 and i386), tests and docs. Assembly is on, as
# FreeBSD's is for aarch64 (perlasm's ios64 flavour, which Xcode's clang
# assembles). CPU features come from armcap.c's Apple path: AES, PMULL,
# SHA-1 and SHA-256 assumed (every Apple arm64 core, QEMU's cortex-a76 and
# the Q8B's Cortex-X1C/A78C have them), SHA-512 and SHA-3 from
# hw.optional sysctls.
# OPENSSL_NO_APPLE_CRYPTO_RANDOM: on Darwin, OpenSSL seeds from CommonCrypto's
# CCRandomGenerateBytes, which the base doesn't have; with it off it uses
# getentropy(2), libSystem's.
# Compile and link see NeoDarwin's headers and link against the runtime
# root (-isysroot ROOT), as the configure-driven builds do. They go through
# links in the build directory so that the compiler flags OpenSSL records
# (`openssl version -f`) don't name the sandbox; SOURCE_DATE_EPOCH, the
# release date, gives `openssl version -b`.
source "$(dirname "$0")/../../tools/base/common.sh"
source "$(dirname "$0")/../commands.sh"
OUT="$(abspath "$1")"; O="$(abspath "$2")"; SYSROOT="$(abspath "$3")"; shift 3
DEPS=(); for d in "$@"; do DEPS+=("$(abspath "$d")"); done
ROOT="$(find_root "${DEPS[@]}")"
PROJ="$(cd "$(dirname "$0")" && pwd)"
B="$(mktemp -d)"; trap 'rm -rf "$B"' EXIT
O="$(stage_src "$O" "$B/src" "$PROJ/patches")"   # patches/ (none yet) applied
ln -s "$ROOT" "$B/root"; ln -s "$SYSROOT" "$B/sysroot"
cd "$O"

export SOURCE_DATE_EPOCH=1790640000   # 29 Sep 2026, OpenSSL 3.5.9's release date
export CC="xcrun clang"
export CPPFLAGS="$(cmd_sysroot_flags ../sysroot | tr '\n' ' ') -idirafter $SDK/usr/include"
export CFLAGS="-O3 -Wall -mcpu=$TARGET_CPU -mmacosx-version-min=26.0 -isysroot ../root"
export LDFLAGS="-mmacosx-version-min=26.0 -isysroot ../root -Wl,-adhoc_codesign"
OPTIONS=(--prefix=/usr --libdir=lib --openssldir=/private/etc/ssl --release shared no-tests no-docs
	no-aria no-idea no-mdc2 no-sm2 no-sm3 no-sm4 enable-ec_nistp_64_gcc_128 no-padlockeng
	-DOPENSSL_NO_APPLE_CRYPTO_RANDOM)
perl ./Configure darwin64-arm64 "${OPTIONS[@]}" > "$B/configure.log" 2>&1 || { tail -30 "$B/configure.log"; exit 1; }
perl configdata.pm -d > "$B/configdata.txt"
for f in asm legacy engine deprecated shared; do
	grep -qx "    $f" "$B/configdata.txt" || { echo "openssl: Configure didn't enable $f" >&2; exit 1; }
done

# The shared libraries, modules and the command only (build_sw would also
# make libcrypto.a and libssl.a, which nothing installs).
make build_generated > "$B/make.log" 2>&1 &&
	make -j"$JOBS" libcrypto.3.dylib libssl.3.dylib providers/legacy.dylib engines/capi.dylib \
		engines/loader_attic.dylib apps/openssl >> "$B/make.log" 2>&1 ||
	{ grep -m 20 -E "error|Error" "$B/make.log"; exit 1; }

mkdir -p "$OUT/usr/lib/ossl-modules" "$OUT/usr/lib/engines-3" "$OUT/usr/bin" "$OUT/private/etc/ssl" \
	"$OUT/usr/local/openssl/lib" "$OUT/usr/local/openssl/include"
install -m 0755 libcrypto.3.dylib libssl.3.dylib "$OUT/usr/lib/"
install -m 0755 providers/legacy.dylib "$OUT/usr/lib/ossl-modules/"
install -m 0755 engines/capi.dylib engines/loader_attic.dylib "$OUT/usr/lib/engines-3/"
install -m 0755 apps/openssl "$OUT/usr/bin/openssl"
install -m 0644 apps/openssl.cnf "$OUT/private/etc/ssl/openssl.cnf"
for l in crypto ssl; do
	otool -D "$OUT/usr/lib/lib$l.3.dylib" | grep -qx "/usr/lib/lib$l.3.dylib" ||
		{ echo "openssl: unexpected install name" >&2; otool -D "$OUT/usr/lib/lib$l.3.dylib" >&2; exit 1; }
	ln -s "../../../lib/lib$l.3.dylib" "$OUT/usr/local/openssl/lib/lib$l.dylib"
done
# The public headers, Configure's generated ones (configuration.h,
# opensslv.h, ...) among them.
cp -R include/openssl "$OUT/usr/local/openssl/include/"
find "$OUT/usr/local/openssl/include" -name '*.in' -delete
