#!/usr/bin/env bash
# SPDX-License-Identifier: BSD-2-Clause
# libz from Apple's zlib-100.120.1 (zlib 1.2.12; P4-21 checkpoint 2,
# docs/architecture/freebsd-parity.md §2.1): replays zlib.xcodeproj's libz
# target with zlib.xcconfig and the project's Release settings.
#   build.sh OUT ZLIB_SRC SYSROOT DEPROOT...   (DEPROOT: //base:root)
# OUT receives usr/lib/libz.1.dylib, the script phase's names for it
# (libz.dylib and libz.1.{1.3,2.5,2.8,2.11,2.12}.dylib) and, build-only, its
# public headers in usr/local/include (zlib.h, zconf.h, as for the base's
# other libraries; macOS: usr/include).
# zlib.xcconfig: gnu11, GCC_PREPROCESSOR_DEFINITIONS USE_MMAP VEC_OPTIMIZE
# INFFAST_OPT (Apple's AddOn: the arm64 adler32 and crc32 assembly and the
# optimised inflate_fast), the project's OTHER_CFLAGS -DZ_ALLOC_WRAPPER=0,
# EXPORTED_SYMBOLS_FILE libz.exp, version 1.2.12, compatibility 1. The
# arm and x86 assembly files assemble to nothing on arm64 and are left out.
# Release.json for macOS 26.0 lists zlib-100; zlib-100.120.1 is its later
# update (one fix in AddOn/zopt_inffast.c's distance check), the tag the
# ZFS userland already pinned.
source "$(dirname "$0")/../../tools/base/common.sh"
source "$(dirname "$0")/../commands.sh"
OUT="$(abspath "$1")"; Z="$(abspath "$2")"; SYSROOT="$(abspath "$3")"; shift 3
DEPS=(); for d in "$@"; do DEPS+=("$(abspath "$d")"); done
ROOT="$(find_root "${DEPS[@]}")"
B="$(mktemp -d)"; trap 'rm -rf "$B"' EXIT
cd "$Z"

# The published zlib/ sources don't include AddOn's zopt_defs.h, which
# declares what VEC_OPTIMIZE and INFFAST_OPT use (the assembly entry points,
# packed_ushort8, INFLATE_SUB_EXTRA_BITS): the C files force-include it,
# through zopt_inffast.h, after zutil.h.
asflags=("${TARGET_FLAGS[@]}" -mcpu=cortex-a76 -DVEC_OPTIMIZE -ffile-prefix-map="$Z/"=zlib/)
write_rsp "$B/asflags" "${asflags[@]}"
write_rsp "$B/cflags" "${asflags[@]}" -Os -std=gnu11 -fno-common -DUSE_MMAP -DINFFAST_OPT -DZ_ALLOC_WRAPPER=0 \
	-I"$Z/zlib" -I"$Z/AddOn" -include zutil.h -include zopt_inffast.h $(cmd_sysroot_flags "$SYSROOT")
write_vers "$B/vers.c" libz zlib 100.120.1 __
compile "$B/obj" "$B/asflags" AddOn/ZAssembly/adler32vec_arm64.s AddOn/ZAssembly/crc32lt_arm64.s
compile "$B/obj" "$B/cflags" AddOn/zopt_inffast.c zlib/adler32.c zlib/compress.c zlib/crc32.c zlib/deflate.c \
	zlib/infback.c zlib/inflate.c zlib/inftrees.c zlib/trees.c zlib/uncompr.c zlib/zutil.c zlib/inffast.c \
	zlib/gzclose.c zlib/gzlib.c zlib/gzread.c zlib/gzwrite.c "$B/vers.c"
mkdir -p "$OUT/usr/lib" "$OUT/usr/local/include"
xcrun ld -arch arm64 -platform_version macos 26.0 26.0 -dylib -dead_strip -adhoc_codesign \
	-install_name /usr/lib/libz.1.dylib -current_version 1.2.12 -compatibility_version 1 \
	-exported_symbols_list libz.exp -syslibroot "$ROOT" "$B"/obj/*.o -lSystem -o "$OUT/usr/lib/libz.1.dylib"
for l in libz.1.1.3.dylib libz.1.2.5.dylib libz.1.2.8.dylib libz.1.2.11.dylib libz.1.2.12.dylib libz.dylib; do
	ln -sf libz.1.dylib "$OUT/usr/lib/$l"
done
install -m 0444 zlib/zlib.h zlib/zconf.h "$OUT/usr/local/include/"
