#!/usr/bin/env bash
# SPDX-License-Identifier: BSD-2-Clause
# libiconv, its converter modules and data, iconv, mkcsmapper and mkesdb
# from libiconv-113 (the Citrus iconv FreeBSD also has; P4-21 checkpoint 3,
# docs/architecture/freebsd-parity.md §2.1): replays libiconv.xcodeproj's
# charset, libiconv, iconv_modules (its 25 module targets), iconv,
# mkcsmapper and mkesdb targets with xcodeconfig/'s settings, and the
# default target's install-i18n.sh.
#   build.sh OUT LIBICONV_SRC SYSROOT DEPROOT...   (DEPROOT: //base:root)
# OUT receives:
# - usr/lib/libcharset.1.dylib (compatibility and current version 1) and
#   usr/lib/libiconv.2.dylib (7), which reexports it (libiconv.xcconfig's
#   -reexport-lcharset), with the libiconv.dylib and libcharset.dylib links;
# - the modules in usr/lib/i18n (libUTF8.dylib, libiconv_std.dylib, ...),
#   which citrus_module.c dlopen()s from _PATH_I18NMODULE;
# - usr/share/i18n's csmapper and esdb tables, prebuilt in the source;
# - usr/bin/iconv, and usr/bin/mkcsmapper and usr/bin/mkesdb: Apple
#   installs those two in /usr/local/bin (as build tools), but the base
#   root drops usr/local, and FreeBSD installs them in /usr/bin;
# - the pages; and build-only, iconv.h and libcharset.h in usr/local/include.
# Not built: the tests targets, mbopt_test, fallback_test, print_charset.
source "$(dirname "$0")/../../tools/base/common.sh"
source "$(dirname "$0")/../commands.sh"
OUT="$(abspath "$1")"; S="$(abspath "$2")"; SYSROOT="$(abspath "$3")"; shift 3
DEPS=(); for d in "$@"; do DEPS+=("$(abspath "$d")"); done
ROOT="$(find_root "${DEPS[@]}")"
PROJ="$(cd "$(dirname "$0")" && pwd)"
B="$(mktemp -d)"; trap 'rm -rf "$B"' EXIT
S="$(stage_src "$S" "$B/src" "$PROJ/patches")"   # patches/ applied
cd "$S"
D="$B/derived"; mkdir -p "$D"
LIB="$OUT/usr/lib"; mkdir -p "$LIB/i18n" "$OUT/usr/bin" "$OUT/usr/local/include"

# base.xcconfig (gnu89, -Os, HAVE_CONFIG_H; VERSION_INFO_PREFIX hidden) and
# lib.xcconfig's library definitions.
base=("${TARGET_FLAGS[@]}" -Os -fno-common $(cmd_sysroot_flags "$SYSROOT") -DHAVE_CONFIG_H)
libdefs=(-DBUILDING_DLL -DENABLE_RELOCATABLE -DIN_LIBRARY -DNO_XMALLOC -DPIC)
dylib() {   # dylib OUT INSTALL_NAME COMPAT CURRENT OBJDIR LDFLAG...
	local out="$1" name="$2" compat="$3" cur="$4" obj="$5"; shift 5
	xcrun ld -arch arm64 -platform_version macos 26.0 26.0 -dylib -dead_strip -adhoc_codesign \
		-install_name "$name" -compatibility_version "$compat" -current_version "$cur" \
		-syslibroot "$ROOT" "$obj"/*.o "$@" -lSystem -o "$out"
}
vers() { write_vers "$D/${1}_vers.c" "$1" libiconv 113 '__attribute__((visibility("hidden")))'; printf '%s' "$D/${1}_vers.c"; }

# charset: libcharset.1.dylib, gnu11.
write_rsp "$B/charset.rsp" "${base[@]}" -std=gnu11 "${libdefs[@]}" -I"$S/libcharset"
compile "$B/obj/charset" "$B/charset.rsp" libcharset/libcharset.c "$(vers charset_1)"
dylib "$LIB/libcharset.1.dylib" /usr/lib/libcharset.1.dylib 1 1 "$B/obj/charset"
ln -sf libcharset.1.dylib "$LIB/libcharset.dylib"

# libiconv: libiconv.2.dylib, c99, with libiconv.xcconfig's and the target's
# definitions; reexports libcharset.
write_rsp "$B/libiconv.rsp" "${base[@]}" -std=c99 "${libdefs[@]}" -DBUILDING_LIBICONV \
	-Drelocate=libiconv_relocate -Dset_relocation_prefix=libiconv_set_relocation_prefix \
	'-D_PATH_I18NMODULE=\"/usr/lib/i18n\"' '-D_PATH_ESDB=\"/usr/share/i18n/esdb\"' \
	'-D_PATH_CSMAPPER=\"/usr/share/i18n/csmapper\"' -I"$S/citrus" -I"$S/libcharset"
LIBSRCS=(__iconv_get_list.c __iconv_free_list.c __iconv.c bsd_iconv.c citrus_bcs_strtol.c citrus_bcs_strtoul.c
	citrus_bcs.c citrus_csmapper.c citrus_db.c citrus_db_factory.c citrus_db_hash.c citrus_esdb.c citrus_hash.c
	citrus_iconv.c citrus_lookup_factory.c citrus_lookup.c citrus_mapper.c citrus_memstream.c citrus_mmap.c
	citrus_module.c citrus_none.c citrus_pivot_factory.c citrus_prop.c citrus_stdenc.c iconv_canonicalize.c
	iconv_close.c iconv_compat.c iconv_open_into.c iconv_open.c iconv_set_relocation_prefix.c iconvctl.c
	iconvlist.c iconv.c)
compile "$B/obj/libiconv" "$B/libiconv.rsp" "${LIBSRCS[@]/#/citrus/}" "$(vers iconv_2)"
dylib "$LIB/libiconv.2.dylib" /usr/lib/libiconv.2.dylib 7 7 "$B/obj/libiconv" -L"$LIB" -reexport-lcharset
ln -sf libiconv.2.dylib "$LIB/libiconv.dylib"
ln -sf libiconv.2.dylib "$LIB/libiconv.2.4.0.dylib"
install -m 0444 citrus/iconv.h libcharset/libcharset.h libcharset/localcharset.h "$OUT/usr/local/include/"

# iconv_modules: each module target, lib.xcconfig, gnu11, linking libiconv.2.
# Xcode's header map makes every project header visible (UTF8MAC includes
# UTF8's citrus_utf8.h).
modinc=(); for m in libiconv_modules/*/; do modinc+=(-I"$S/$m"); done
write_rsp "$B/mod.rsp" "${base[@]}" -std=gnu11 "${libdefs[@]}" -I"$S/citrus" "${modinc[@]}"
for m in libiconv_modules/*/; do
	m="$(basename "$m")"
	compile "$B/obj/mod_$m" "$B/mod.rsp" libiconv_modules/"$m"/*.c
	dylib "$LIB/i18n/lib$m.dylib" "/usr/lib/i18n/lib$m.dylib" 1 1 "$B/obj/mod_$m" -L"$LIB" -liconv.2
done
# mapper_parallel: mapper_serial's source, which defines both modules' ops.
dylib "$LIB/i18n/libmapper_parallel.dylib" /usr/lib/i18n/libmapper_parallel.dylib 1 1 "$B/obj/mod_mapper_serial" \
	-L"$LIB" -liconv.2

# iconv: iconv.xcconfig (DEPENDS_ON_LIBICONV), /usr/bin.
write_rsp "$B/iconv.rsp" "${base[@]}" -DDEPENDS_ON_LIBICONV -I"$S/citrus"
tool "$B" "$ROOT" "$OUT/usr/bin/iconv" "$B/iconv.rsp" iconv/iconv.c "$(vers iconv)" -- -L"$LIB" -liconv.2

# mkcsmapper and mkesdb: Xcode's lex and yacc rules, then the sources.
for t in mkcsmapper mkesdb; do
	mkdir -p "$D/$t"
	(cd "$D/$t" && xcrun yacc -d "$S/$t/yacc.y" && xcrun lex "$S/$t/lex.l")
	write_rsp "$B/$t.rsp" "${base[@]}" -I"$S/$t" -I"$D/$t" -I"$S/citrus" "${modinc[@]}"
	tool "$B" "$ROOT" "$OUT/usr/bin/$t" "$B/$t.rsp" "$D/$t/y.tab.c" "$D/$t/lex.yy.c" "$(vers $t)" -- -L"$LIB" -liconv.2
done

# install-i18n.sh
mkdir -p "$OUT/usr/share/i18n"
(cd i18n && tar cf - --exclude README .) | (cd "$OUT/usr/share/i18n" && tar xf -)

mkdir -p "$OUT/usr/share/man/man1" "$OUT/usr/share/man/man3"
install -m 0444 iconv/iconv.1 mkcsmapper/mkcsmapper.1 mkesdb/mkesdb.1 "$OUT/usr/share/man/man1/"
install -m 0444 citrus/*.3 "$OUT/usr/share/man/man3/"
