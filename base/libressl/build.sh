#!/usr/bin/env bash
# SPDX-License-Identifier: BSD-2-Clause
# libcrypto from LibreSSL 3.3.6 (docs/base/session.md, "Loopback and
# sshd"): the library macOS 26 ships as /usr/lib/libcrypto.46.dylib and
# links OpenSSH against. Apple's LibreSSL project isn't in the macOS 26.0
# release set (distribution-macOS macos-260 has only OpenSSL098-85, the
# deprecated 0.9.8 libcrypto.0.9.8), so NeoDarwin pins upstream LibreSSL at
# the version macOS ships (`/usr/bin/openssl version`, `ssh -V`: LibreSSL
# 3.3.6; libcrypto's libtool version 46:2:0 gives macOS's install name and
# its compatibility 47.0.0 and current 47.2.0 versions).
#   build.sh OUT LIBRESSL_SRC SYSROOT DEPROOT...   (DEPROOT: //base:root)
# OUT receives usr/lib/libcrypto.46.dylib and, build-only, the headers and
# a libcrypto.dylib link to it in usr/local/libressl (include/, lib/), where
# Apple's internal SDK keeps them for OpenSSH (openssh.xcconfig's
# Libcrypto_include and Libcrypto_lib); macOS publishes neither.
# configure runs cross-compiling, as zsh's does, so no answer comes from a
# program run against the host's libSystem (LibreSSL's configure has no run
# checks); compile and link checks see NeoDarwin's headers and link against
# the runtime root (-isysroot ROOT). No assembly (arm64 Darwin has none in
# 3.3.6). Only crypto/ is built: OpenSSH needs no libssl or libtls, and
# macOS's libssl isn't something any NeoDarwin program links yet.
# libcrypto's randomness is libc's arc4random_buf (configure finds it, so
# LibreSSL's own getentropy compat isn't built).
source "$(dirname "$0")/../../tools/base/common.sh"
source "$(dirname "$0")/../commands.sh"
OUT="$(abspath "$1")"; L="$(abspath "$2")"; SYSROOT="$(abspath "$3")"; shift 3
DEPS=(); for d in "$@"; do DEPS+=("$(abspath "$d")"); done
ROOT="$(find_root "${DEPS[@]}")"
PROJ="$(cd "$(dirname "$0")" && pwd)"
B="$(mktemp -d)"; trap 'rm -rf "$B"' EXIT
L="$(stage_src "$L" "$B/src" "$PROJ/patches")"   # patches/ (none yet) applied
cd "$L"

BUILD="$(sh ./config.guess)"
export CC="xcrun clang"
export CPPFLAGS="$(cmd_sysroot_flags "$SYSROOT" | tr '\n' ' ') -idirafter $SDK/usr/include"
export CFLAGS="-arch arm64 -mmacosx-version-min=26.0 -isysroot $ROOT -Os"
export LDFLAGS="-arch arm64 -mmacosx-version-min=26.0 -isysroot $ROOT -Wl,-adhoc_codesign"
./configure --build="$BUILD" --host=aarch64-apple-darwin25.0.0 --prefix=/usr --disable-asm --disable-tests \
	--disable-static --enable-shared --with-openssldir=/private/etc/ssl > "$B/configure.log" 2>&1 || { tail -30 "$B/configure.log"; exit 1; }
grep -q 'whether we are cross compiling... yes' "$B/configure.log" || { echo "libressl: configure didn't cross-compile" >&2; exit 1; }

make -C crypto -j"$JOBS" libcrypto.la > "$B/make.log" 2>&1 || { grep -m 20 -E "error|Error" "$B/make.log"; exit 1; }
mkdir -p "$OUT/usr/lib" "$OUT/usr/local/libressl/lib"
install -m 0755 crypto/.libs/libcrypto.46.dylib "$OUT/usr/lib/libcrypto.46.dylib"
ln -s ../../../lib/libcrypto.46.dylib "$OUT/usr/local/libressl/lib/libcrypto.dylib"
otool -D "$OUT/usr/lib/libcrypto.46.dylib" | grep -qx /usr/lib/libcrypto.46.dylib ||
	{ echo "libressl: unexpected install name" >&2; otool -D "$OUT/usr/lib/libcrypto.46.dylib" >&2; exit 1; }
# The public headers (include/openssl), configure's generated ones among them.
mkdir -p "$OUT/usr/local/libressl/include"
cp -R include/openssl "$OUT/usr/local/libressl/include/"
