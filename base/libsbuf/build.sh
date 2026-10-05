#!/usr/bin/env bash
# SPDX-License-Identifier: BSD-2-Clause
# libsbuf from FreeBSD's lib/libsbuf (docs/base/libsystem.md): sbuf(9) in
# userland, for shell_cmds' apply and w.
#   build.sh OUT LIBSBUF_ROOT SYSROOT DEPROOT...   (DEPROOT: //base:root)
# LIBSBUF_ROOT holds the freebsd-src files freebsd.lock pins, laid out as in
# freebsd-src. OUT receives usr/lib/libsbuf.dylib and, build-only, the
# headers that declare it in usr/local/include: usbuf.h, usbuf_names.h and
# FreeBSD's sys/sbuf.h as usbuf_sbuf.h.
# Why FreeBSD: macOS ships /usr/lib/libsbuf.dylib (apply and w link it) but
# Apple publishes no source for it; its exports are FreeBSD's userland sbuf
# functions renamed usbuf_* (libsbuf.tbd), declared by a private <usbuf.h>.
# compat/ has that mapping. How: lib/libsbuf/Makefile (subr_sbuf.c and
# subr_prf.c, for sbuf_hexdump), compiled with the usbuf_ names. Install
# name and versions are Apple's (/usr/lib/libsbuf.dylib 1.0).
source "$(dirname "$0")/../../tools/base/common.sh"
source "$(dirname "$0")/../commands.sh"
OUT="$(abspath "$1")"; F="$(abspath "$2")"; SYSROOT="$(abspath "$3")"; shift 3
DEPS=(); for d in "$@"; do DEPS+=("$(abspath "$d")"); done
ROOT="$(find_root "${DEPS[@]}")"
PROJ="$(cd "$(dirname "$0")" && pwd)"
B="$(mktemp -d)"; trap 'rm -rf "$B"' EXIT
D="$B/inc"; mkdir -p "$D/sys"
# FreeBSD's <sys/sbuf.h> and <sys/ctype.h> (subr_prf.c's), ahead of xnu's.
cp "$F/sys/sys/sbuf.h" "$F/sys/sys/ctype.h" "$D/sys/"

write_rsp "$B/cflags" "${TARGET_FLAGS[@]}" -Os -std=gnu11 -fno-common -w -include "$PROJ/compat/usbuf_names.h" \
	-I"$D" $(cmd_sysroot_flags "$SYSROOT")
compile "$B/obj" "$B/cflags" "$F/sys/kern/subr_sbuf.c" "$F/sys/kern/subr_prf.c"

mkdir -p "$OUT/usr/lib" "$OUT/usr/local/include"
xcrun ld -arch arm64 -platform_version macos 26.0 26.0 -dylib -dead_strip -adhoc_codesign \
	-install_name /usr/lib/libsbuf.dylib -current_version 1.0 -compatibility_version 1.0 \
	-syslibroot "$ROOT" "$B"/obj/*.o -lSystem -o "$OUT/usr/lib/libsbuf.dylib"
cp "$PROJ/compat/usbuf.h" "$PROJ/compat/usbuf_names.h" "$OUT/usr/local/include/"
cp "$F/sys/sys/sbuf.h" "$OUT/usr/local/include/usbuf_sbuf.h"
