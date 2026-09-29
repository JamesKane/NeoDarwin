#!/usr/bin/env bash
# SPDX-License-Identifier: BSD-2-Clause
# libxo from FreeBSD's contrib/libxo (Juniper's libxo 1.6.0; docs/base/libsystem.md).
#   build.sh OUT LIBXO_ROOT SYSROOT DEPROOT...   (DEPROOT: //base:root)
# LIBXO_ROOT holds the freebsd-src files freebsd.lock pins, laid out as in
# freebsd-src. OUT receives usr/lib/libxo.dylib and, build-only, its headers
# in usr/local/include/libxo.
# Why FreeBSD: macOS ships libxo (/usr/lib/libxo.dylib, linked by wc, df, w,
# last, unvis) but Apple publishes no libxo project; the commands' sources
# are FreeBSD's, written against this libxo. Pinned at the commit libm and
# ndcrypto use. How: FreeBSD's lib/libxo/libxo/Makefile (libxo.c,
# xo_encoder.c, xo_syslog.c; its xo_config.h), with patch 0001 adapting the
# configure results to Darwin. Install name and versions are Apple's
# (/usr/lib/libxo.dylib 1.0, linking libSystem alone).
source "$(dirname "$0")/../../tools/base/common.sh"
source "$(dirname "$0")/../commands.sh"
OUT="$(abspath "$1")"; X="$(abspath "$2")"; SYSROOT="$(abspath "$3")"; shift 3
DEPS=(); for d in "$@"; do DEPS+=("$(abspath "$d")"); done
ROOT="$(find_root "${DEPS[@]}")"
PROJ="$(cd "$(dirname "$0")" && pwd)"
B="$(mktemp -d)"; trap 'rm -rf "$B"' EXIT
X="$(stage_src "$X" "$B/src" "$PROJ/patches")"   # patches/ applied
cd "$X"

# The Makefile's CFLAGS; encoders load from /usr/lib/libxo/encoder (LIBDIR).
write_rsp "$B/cflags" "${TARGET_FLAGS[@]}" -Os -fno-common -I"$X/contrib/libxo/libxo" -I"$X/lib/libxo/libxo" \
	'-DXO_ENCODERDIR=\"/usr/lib/libxo/encoder\"' $(cmd_sysroot_flags "$SYSROOT")
compile "$B/obj" "$B/cflags" contrib/libxo/libxo/libxo.c contrib/libxo/libxo/xo_encoder.c contrib/libxo/libxo/xo_syslog.c

mkdir -p "$OUT/usr/lib" "$OUT/usr/local/include/libxo"
xcrun ld -arch arm64 -platform_version macos 26.0 26.0 -dylib -dead_strip -adhoc_codesign \
	-install_name /usr/lib/libxo.dylib -current_version 1.0 -compatibility_version 1.0 \
	-syslibroot "$ROOT" "$B"/obj/*.o -lSystem -o "$OUT/usr/lib/libxo.dylib"
# INCS (INCSDIR = /usr/include/libxo): build-only here, as the base's other headers are.
cp contrib/libxo/libxo/xo.h contrib/libxo/libxo/xo_encoder.h "$OUT/usr/local/include/libxo/"
