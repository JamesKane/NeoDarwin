#!/usr/bin/env bash
# SPDX-License-Identifier: BSD-2-Clause
# libarchive and its commands from Apple's libarchive-158 (libarchive 3.7.4;
# P4-21 checkpoint 2, docs/architecture/freebsd-parity.md §2.1): replays
# libarchive.xcodeproj's libarchive, libarchive_fe, tar and cpio targets with
# xcodeconfig/{base,libs,executables}.xcconfig, and builds bsdcat from the
# same tree as FreeBSD's usr.bin/bsdcat does (Apple's project has no cat
# target).
#   build.sh OUT LIBARCHIVE_SRC SYSROOT DEPROOT...   (DEPROOT: //base:root, //base:libz_dylib,
#                                                     //base:bzip2_commands, //base:xz_commands,
#                                                     //base:libmd_dylib)
# OUT receives usr/lib/libarchive.2.dylib with create_dylib_symlinks.sh's
# libarchive.dylib, usr/bin/bsdtar with create_tar_symlinks.sh's tar link,
# usr/bin/cpio (the target's PRODUCT_NAME), usr/bin/bsdcat and, build-only,
# archive.h and archive_entry.h in usr/local/include (libs.xcconfig's
# PUBLIC_HEADERS_FOLDER_PATH).
# libs.xcconfig: version 9.2, compatibility 9, UNEXPORTED_SYMBOLS_FILE
# unexports. base.xcconfig: PLATFORM_CONFIG_H names the configuration header;
# here compat/nd_libarchive_config.h, Apple's config.h less libiconv,
# libxml2 (xar) and CommonCrypto (digests from libmd instead). The Frameworks
# phase's CoreFoundation and Security serve archive_check_entitlement.c
# alone, which compat/nd_archive_check_entitlement.c replaces (every format
# and filter allowed, as for a process without the entitlements); libiconv
# and libxml2 aren't linked; libz, libbz2 and liblzma are the base's, and
# libmd is added. Patch 0001 drops libquarantine and CommonCrypto.
source "$(dirname "$0")/../../tools/base/common.sh"
source "$(dirname "$0")/../commands.sh"
OUT="$(abspath "$1")"; A="$(abspath "$2")"; SYSROOT="$(abspath "$3")"; shift 3
DEPS=(); for d in "$@"; do DEPS+=("$(abspath "$d")"); done
ROOT="$(find_root "${DEPS[@]}")"
ZL=""; BZ=""; LZ=""; MD=""
for d in "${DEPS[@]}"; do
	[ -f "$d/usr/lib/libz.1.dylib" ] && ZL="$d"; [ -f "$d/usr/lib/libbz2.1.0.dylib" ] && BZ="$d"
	[ -f "$d/usr/lib/liblzma.5.dylib" ] && LZ="$d"; [ -f "$d/usr/lib/libmd.dylib" ] && MD="$d"
done
[ -n "$ZL" ] && [ -n "$BZ" ] && [ -n "$LZ" ] && [ -n "$MD" ] || { echo "libarchive: pass //base:libz_dylib," \
	"//base:bzip2_commands, //base:xz_commands and //base:libmd_dylib" >&2; exit 1; }
PROJ="$(cd "$(dirname "$0")" && pwd)"
B="$(mktemp -d)"; trap 'rm -rf "$B"' EXIT
A="$(stage_src "$A" "$B/src" "$PROJ/patches")"   # patches/ applied
cd "$A"
D="$B/derived"; mkdir -p "$D"
cp config.h "$D/apple_config.h"
cp "$PROJ/compat/nd_libarchive_config.h" "$D/"

# Release -Os; warning flags change no interface and are left out.
# VERSION_INFO_PREFIX hides the apple-generic version symbols, which are
# therefore left out of the library.
base=("${TARGET_FLAGS[@]}" -mcpu=cortex-a76 -Os -fno-common '-DPLATFORM_CONFIG_H=\"nd_libarchive_config.h\"'
	-DND_NO_QUARANTINE -DND_NO_COMMONCRYPTO -I"$D" -I"$ZL/usr/local/include" -I"$BZ/usr/local/include"
	-I"$LZ/usr/local/include" -I"$MD/usr/local/include" -include libmd_cdefs.h $(cmd_sysroot_flags "$SYSROOT")
	-ffile-prefix-map="$A/"=libarchive/)
write_rsp "$B/lib.rsp" "${base[@]}" -I"$A/libarchive/libarchive"
compile "$B/obj/lib" "$B/lib.rsp" $(grep -v '^#' "$PROJ/sources.txt") "$PROJ/compat/nd_archive_check_entitlement.c"
mkdir -p "$OUT/usr/lib" "$OUT/usr/bin" "$OUT/usr/local/include"
xcrun ld -arch arm64 -platform_version macos 26.0 26.0 -dylib -dead_strip -adhoc_codesign \
	-install_name /usr/lib/libarchive.2.dylib -current_version 9.2 -compatibility_version 9 \
	-unexported_symbols_list unexports -syslibroot "$ROOT" "$B"/obj/lib/*.o \
	-L"$ZL/usr/lib" -lz -L"$BZ/usr/lib" -lbz2 -L"$LZ/usr/lib" -llzma -L"$MD/usr/lib" -lmd -lSystem \
	-o "$OUT/usr/lib/libarchive.2.dylib"
ln -sf libarchive.2.dylib "$OUT/usr/lib/libarchive.dylib"
install -m 0444 libarchive/libarchive/archive.h libarchive/libarchive/archive_entry.h "$OUT/usr/local/include/"

# libarchive_fe (static, linked into each command) and the commands:
# executables.xcconfig, USER_HEADER_SEARCH_PATHS libarchive_fe and
# libarchive; INSTALL_PATH /usr/bin. <archive.h> is the built product's
# public header.
write_rsp "$B/cmd.rsp" "${base[@]}" -iquote "$A/libarchive/libarchive_fe" -iquote "$A/libarchive/libarchive" \
	-I"$OUT/usr/local/include"
compile "$B/obj/fe" "$B/cmd.rsp" libarchive/libarchive_fe/err.c libarchive/libarchive_fe/passphrase.c \
	libarchive/libarchive_fe/line_reader.c
la=("$B"/obj/fe/*.o -L"$OUT/usr/lib" -larchive)
tool "$B" "$ROOT" "$OUT/usr/bin/bsdtar" "$B/cmd.rsp" libarchive/tar/creation_set.c libarchive/tar/bsdtar.c \
	libarchive/tar/cmdline.c libarchive/tar/read.c libarchive/tar/subst.c libarchive/tar/util.c \
	libarchive/tar/write.c -- "${la[@]}"
ln -sf bsdtar "$OUT/usr/bin/tar"
tool "$B" "$ROOT" "$OUT/usr/bin/cpio" "$B/cmd.rsp" libarchive/cpio/cmdline.c libarchive/cpio/cpio.c -- "${la[@]}"
# bsdcat: FreeBSD's usr.bin/bsdcat Makefile (cat/bsdcat.c and cmdline.c
# with libarchive_fe); it installs no cat link.
tool "$B" "$ROOT" "$OUT/usr/bin/bsdcat" "$B/cmd.rsp" libarchive/cat/bsdcat.c libarchive/cat/cmdline.c -- "${la[@]}"
