#!/usr/bin/env bash
# SPDX-License-Identifier: BSD-2-Clause
# zlib for the ZFS userland only (P3-01 checkpoint 2), until the base
# carries zlib (P3-02): Apple's zlib-100.120.1 (zlib 1.2.12, @apple_zlib,
# zlib licence), its zlib/ sources as a private static library. libzfs
# needs uncompress() for resume tokens (zfs send -t, zfs receive -A) and
# libefi crc32() for GPT headers; the test suite's draid helper uses gz*.
#   zlib.sh OUT ZLIB_SRC SYSROOT DEPROOT...   (DEPROOT: //base:root)
# OUT receives usr/local/lib/nd_zfs/libz.a and usr/local/include/nd_zfs/
# {zlib.h,zconf.h}: build-only paths, never installed in an image.
# zlib.xcconfig's GCC_PREPROCESSOR_DEFINITIONS (VEC_OPTIMIZE, INFFAST_OPT)
# select the AddOn/ optimised paths; they are left out, so this is the
# reference zlib (USE_MMAP only affects deflate's window allocation).
source "$(dirname "$0")/../../tools/base/common.sh"
source "$(dirname "$0")/../../base/commands.sh"
OUT="$(abspath "$1")"; Z="$(abspath "$2")/zlib"; SYSROOT="$(abspath "$3")"
B="$(mktemp -d)"; trap 'rm -rf "$B"' EXIT
cd "$Z"
write_rsp "$B/cflags" "${TARGET_FLAGS[@]}" -mcpu=cortex-a76 -O2 -std=gnu11 -DHAVE_UNISTD_H -DHAVE_STDARG_H \
	-I. $(cmd_sysroot_flags "$SYSROOT") -ffile-prefix-map="$Z/"=zlib/
compile "$B/obj" "$B/cflags" adler32.c compress.c crc32.c deflate.c gzclose.c gzlib.c gzread.c gzwrite.c \
	infback.c inffast.c inflate.c inftrees.c trees.c uncompr.c zutil.c
mkdir -p "$OUT/usr/local/lib/nd_zfs" "$OUT/usr/local/include/nd_zfs"
xcrun libtool -static -o "$OUT/usr/local/lib/nd_zfs/libz.a" "$B"/obj/*.o
install -m 0444 zlib.h zconf.h "$OUT/usr/local/include/nd_zfs/"
