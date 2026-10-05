#!/usr/bin/env bash
# SPDX-License-Identifier: BSD-2-Clause
# libbz2 and the bzip2 commands from Apple's bzip2-47 (bzip2 1.0.8; P4-21
# checkpoint 2, docs/architecture/freebsd-parity.md §2.1): replays
# bzip2.xcodeproj's libbz2.dylib, bzip2, bzip2recover and "Scripts and
# Documentation" targets with xcodeconfig/{base,libs,executables}.xcconfig.
#   build.sh OUT BZIP2_SRC SYSROOT DEPROOT...   (DEPROOT: //base:root)
# OUT receives usr/lib/libbz2.1.0.dylib with create_dylib_symlinks.sh's
# libbz2.dylib and libbz2.1.0.8.dylib, usr/bin/{bzip2,bunzip2,bzcat,
# bzip2recover} (create_bzip2_links.sh's hard links: copies here), the
# bzdiff and bzmore scripts with their bzcmp and bzless links, and,
# build-only, bzlib.h in usr/local/include (macOS: usr/include).
# libs.xcconfig: version 1.0.8 (compatibility 1, Xcode's default),
# unexports (the internal BZ2_* and the version symbols). The arm64
# crc32vec.s is the project's; the x86_64 one assembles to nothing.
# Man pages are left out, as for the base's other projects.
source "$(dirname "$0")/../../tools/base/common.sh"
source "$(dirname "$0")/../commands.sh"
OUT="$(abspath "$1")"; Z="$(abspath "$2")"; SYSROOT="$(abspath "$3")"; shift 3
DEPS=(); for d in "$@"; do DEPS+=("$(abspath "$d")"); done
ROOT="$(find_root "${DEPS[@]}")"
B="$(mktemp -d)"; trap 'rm -rf "$B"' EXIT
cd "$Z"
D="$B/derived"; mkdir -p "$D"

# crc32vec.s uses PMULL (the crypto extension, which cortex-a76 has).
base=("${TARGET_FLAGS[@]}" -mcpu=cortex-a76+crypto -Os -fno-common $(cmd_sysroot_flags "$SYSROOT")
	-ffile-prefix-map="$Z/"=bzip2/)
write_rsp "$B/cflags" "${base[@]}"
write_vers "$D/lib_vers.c" bz2_1_0 bzip2 47 __
compile "$B/obj/lib" "$B/cflags" bzip2/blocksort.c bzip2/bzlib.c bzip2/compress.c bzip2/crctable.c \
	bzip2/decompress.c bzip2/arm64/crc32vec.s bzip2/huffman.c bzip2/randtable.c "$D/lib_vers.c"
mkdir -p "$OUT/usr/lib" "$OUT/usr/bin" "$OUT/usr/local/include"
xcrun ld -arch arm64 -platform_version macos 26.0 26.0 -dylib -dead_strip -adhoc_codesign \
	-install_name /usr/lib/libbz2.1.0.dylib -current_version 1.0.8 -compatibility_version 1 \
	-unexported_symbols_list unexports -syslibroot "$ROOT" "$B"/obj/lib/*.o -lSystem \
	-o "$OUT/usr/lib/libbz2.1.0.dylib"
ln -sf libbz2.1.0.dylib "$OUT/usr/lib/libbz2.dylib"
ln -sf libbz2.1.0.dylib "$OUT/usr/lib/libbz2.1.0.8.dylib"
install -m 0444 bzip2/bzlib.h "$OUT/usr/local/include/"

# executables.xcconfig: INSTALL_PATH /usr/bin. bzip2 links libbz2;
# bzip2recover is self-contained.
vers() { write_vers "$D/${1}_vers.c" "$1" bzip2 47; printf '%s' "$D/${1}_vers.c"; }
tool "$B" "$ROOT" "$OUT/usr/bin/bzip2" "$B/cflags" bzip2/bzip2.c "$(vers bzip2)" \
	-- -L"$OUT/usr/lib" -lbz2
tool "$B" "$ROOT" "$OUT/usr/bin/bzip2recover" "$B/cflags" bzip2/bzip2recover.c "$(vers bzip2recover)"
cp "$OUT/usr/bin/bzip2" "$OUT/usr/bin/bunzip2"; cp "$OUT/usr/bin/bzip2" "$OUT/usr/bin/bzcat"
# Scripts and Documentation: bzdiff and bzmore, and create_script_symlinks.sh.
install -m 0755 bzip2/bzdiff bzip2/bzmore "$OUT/usr/bin/"
ln -sf bzdiff "$OUT/usr/bin/bzcmp"; ln -sf bzmore "$OUT/usr/bin/bzless"
