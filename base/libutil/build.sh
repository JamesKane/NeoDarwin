#!/usr/bin/env bash
# SPDX-License-Identifier: BSD-2-Clause
# libutil from libutil-73 (docs/base/libsystem.md): replays libutil.xcodeproj's
# util target with xcconfigs/lib.xcconfig. ls(1) links it for
# humanize_number(3).
#   build.sh OUT LIBUTIL_SRC SYSROOT DEPROOT...   (DEPROOT: //base:root)
# OUT receives usr/lib/libutil.dylib (+ the libutil1.0.dylib link) and its
# private headers in usr/local/include.
# tzlink(3) asks tzlinkd over XPC; NeoDarwin runs no tzlinkd and its libxpc
# stand-in has no connections, so patch 0001 (LIBUTIL_NO_TZLINKD) selects the
# simulator's version, which returns ENOTSUP. wipefs(3) needs
# IOKit's IOStorage.h for ioctl definitions only (the host SDK's).
source "$(dirname "$0")/../../tools/base/common.sh"
source "$(dirname "$0")/../commands.sh"
OUT="$(abspath "$1")"; U="$(abspath "$2")"; SYSROOT="$(abspath "$3")"; shift 3
DEPS=(); for d in "$@"; do DEPS+=("$(abspath "$d")"); done
ROOT="$(find_root "${DEPS[@]}")"
PROJ="$(cd "$(dirname "$0")" && pwd)"
B="$(mktemp -d)"; trap 'rm -rf "$B"' EXIT
U="$(stage_src "$U" "$B/src" "$PROJ/patches")"   # patches/ applied
cd "$U"

# The target's settings (HEADER_SEARCH_PATHS = SRCROOT, GCC_NO_COMMON_BLOCKS);
# warning flags are left out. ExtentManager.cpp and wipefs.cpp compile
# against the sysroot's libc++ headers, ahead of the C headers.
c=("${TARGET_FLAGS[@]}" -Os -fno-common -DLIBUTIL_NO_TZLINKD -I"$U")
write_rsp "$B/cflags" "${c[@]}" $(cmd_sysroot_flags "$SYSROOT")
write_rsp "$B/cxxflags" "${c[@]}" -nostdinc++ -isystem "$SYSROOT/usr/include/c++/v1" $(cmd_sysroot_flags "$SYSROOT")
compile "$B/obj" "$B/cflags" getmntopts.c humanize_number.c pidfile.c expand_number.c realhostname.c \
	reexec_to_match_kernel.c trimdomain.c tzlink.c tzbootuuid.c
compile "$B/obj" "$B/cxxflags" ExtentManager.cpp wipefs.cpp

# EXPORTED_SYMBOLS_FILE libutil.exports; DYLIB_CURRENT/COMPATIBILITY_VERSION
# 1.0; CLANG_CXX_LIBRARY libc++.
mkdir -p "$OUT/usr/lib" "$OUT/usr/local/include"
xcrun ld -arch arm64 -platform_version macos 26.0 26.0 -dylib -dead_strip -adhoc_codesign \
	-install_name /usr/lib/libutil.dylib -current_version 1.0 -compatibility_version 1.0 \
	-exported_symbols_list libutil.exports -syslibroot "$ROOT" "$B"/obj/*.o -lc++ -lSystem \
	-o "$OUT/usr/lib/libutil.dylib"
ln -sf libutil.dylib "$OUT/usr/lib/libutil1.0.dylib"   # the target's script phase
# PBXHeadersBuildPhase: Private headers (PRIVATE_HEADERS_FOLDER_PATH).
cp libutil.h mntopts.h tzlink.h wipefs.h "$OUT/usr/local/include/"
