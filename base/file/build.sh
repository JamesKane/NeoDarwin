#!/usr/bin/env bash
# SPDX-License-Identifier: BSD-2-Clause
# file and its magic database from file-104 (file 5.41; P4-21 checkpoint 3,
# docs/architecture/freebsd-parity.md §2.1): replays file.xcodeproj's file
# target (xcconfigs/file.xcconfig over common.xcconfig: HEADER_SEARCH_PATHS
# $(SRCROOT), whose config.h is Apple's configured one; HAVE_CONFIG_H,
# MAGIC="/usr/share/file/magic", BUILTIN_MACHO), linking libz, libbz2 and
# liblzma; its man-pages.sh; and the magic target's magic.sh, which compiles
# Magdir with a file built for the build host (build_magichost.sh's
# magic_host, here compiled with the host SDK from the same sources).
#   build.sh OUT FILE_SRC SYSROOT DEPROOT...   (DEPROOT: //base:root, //base:libz_dylib,
#                                               //base:bzip2_commands, //base:xz_commands)
# OUT receives usr/bin/file, usr/share/file/magic.mgc and the Magdir sources
# in usr/share/file/magic, and file.1 and magic.5. libmagic (a static
# library in /usr/local/lib) isn't installed: nothing in the base links it.
source "$(dirname "$0")/../../tools/base/common.sh"
source "$(dirname "$0")/../commands.sh"
OUT="$(abspath "$1")"; S="$(abspath "$2")"; SYSROOT="$(abspath "$3")"; shift 3
DEPS=(); for d in "$@"; do DEPS+=("$(abspath "$d")"); done
ROOT="$(find_root "${DEPS[@]}")"
lib() { local d; for d in "${DEPS[@]}"; do [ -f "$d/usr/lib/$1" ] && { printf '%s' "$d"; return 0; }; done
	echo "file: no DEPROOT holds usr/lib/$1" >&2; return 1; }
Z="$(lib libz.1.dylib)"; BZ="$(lib libbz2.1.0.dylib)"; XZ="$(lib liblzma.5.dylib)"
PROJ="$(cd "$(dirname "$0")" && pwd)"
B="$(mktemp -d)"; trap 'rm -rf "$B"' EXIT
S="$(stage_src "$S" "$B/src" "$PROJ/patches")"   # patches/ (none) applied
cd "$S/file/src"
D="$B/derived"; mkdir -p "$D"

SRCS=(apprentice.c apptype.c ascmagic.c cdf_time.c cdf.c compress.c buffer.c encoding.c is_csv.c fsmagic.c
	funcs.c is_tar.c is_json.c magic.c print.c readcdf.c readelf.c readmacho.c der.c softmagic.c file.c)
DEFS=(-DHAVE_CONFIG_H '-DMAGIC=\"/usr/share/file/magic\"' -DBUILTIN_MACHO -I"$S")
write_vers "$D/file_vers.c" file file 104 __
write_rsp "$B/file.rsp" "${TARGET_FLAGS[@]}" -Os "${DEFS[@]}" $(cmd_sysroot_flags "$SYSROOT") \
	-isystem "$Z/usr/local/include" -isystem "$BZ/usr/local/include" -isystem "$XZ/usr/local/include"
tool "$B" "$ROOT" "$OUT/usr/bin/file" "$B/file.rsp" "${SRCS[@]}" "$D/file_vers.c" \
	-- -L"$Z/usr/lib" -L"$BZ/usr/lib" -L"$XZ/usr/lib" -lbz2 -lz -llzma

# magic.sh with build_magichost.sh's magic_host: the same target for the
# build machine, against the host SDK and its libraries (the SDK has
# liblzma.tbd but no lzma.h: the headers are base/xz's).
write_rsp "$B/host.rsp" -arch "$(uname -m)" -isysroot "$SDK" -O1 "${DEFS[@]}" -w -isystem "$XZ/usr/local/include"
compile "$B/hostobj" "$B/host.rsp" "${SRCS[@]}"
xcrun clang -arch "$(uname -m)" -isysroot "$SDK" "$B"/hostobj/*.o -lbz2 -lz -llzma -o "$B/magic_host"
(cd "$B" && ./magic_host -C -m "$S/file/magic/Magdir")
install -d -m 0755 "$OUT/usr/share/file/magic"
install -m 0644 "$B/Magdir.mgc" "$OUT/usr/share/file/magic.mgc"
install -m 0644 "$S"/file/magic/Magdir/* "$OUT/usr/share/file/magic/"

# man-pages.sh, the file target's part.
sub=(sed -e s@__CSECTION__@1@g -e s@__FSECTION__@5@g -e s@__VERSION__@5.41@g -e s@__MAGIC__@/usr/share/file/magic@g)
install -d -m 0755 "$OUT/usr/share/man/man1" "$OUT/usr/share/man/man5"
"${sub[@]}" "$S/file/doc/file.man" > "$OUT/usr/share/man/man1/file.1"
"${sub[@]}" "$S/file/doc/magic.man" > "$OUT/usr/share/man/man5/magic.5"
