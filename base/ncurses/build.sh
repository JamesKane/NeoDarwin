#!/usr/bin/env bash
# SPDX-License-Identifier: BSD-2-Clause
# libncurses and the terminfo database from ncurses-79 (ncurses 6.0-20150808
# with Apple's 5.4 ABI layer; docs/base/session.md): replays ncurses.xcodeproj's
# libncurses target (xcconfigs/libraries.xcconfig) with its "Bootstrap
# Sources" and "Derived Sources" script phases, and run_tic.sh.
#   build.sh OUT NCURSES_SRC SYSROOT DEPROOT...   (DEPROOT: //base:root)
# OUT receives usr/lib/libncurses.5.4.dylib and link_libs.sh's names for it
# (libncurses, libncurses.5, libcurses, libtermcap), usr/share/terminfo (all
# of terminfo.src, as macOS ships it), and, build-only, the installed headers
# (install_headers.sh) in usr/local/include.
# Configuration: Apple's build doesn't run configure. configure.sh ran it once
# on Apple's side (--enable-widec --enable-ext-colors --enable-termcap
# --with-abi-version=5.4 ...) and its results are committed: ncurses/include/
# ncurses_cfg.h, which libncurses.xcconfig's HAVE_CONFIG_H reads, and the
# generated nc_abi.c and curses.wide. They describe libSystem's POSIX
# interfaces, all present in NeoDarwin's, so they're used as Apple uses them.
# Host tools: make_hash, make_keys and tic_static are Xcode targets Apple
# builds for the build machine (native_execs.sh); they are compiled here with
# the host SDK. run_tic.sh defaults TIC_PATH to the build host's /usr/bin/tic;
# NeoDarwin passes the tic_static built from these sources instead.
# The commands: clear, infocmp, tic (with captoinfo and infotocap), toe,
# tput, tset (with reset) and tabs in usr/bin (P4-21 checkpoint 3).
# Not built: libform, libmenu and libpanel (nothing in the base links them).
source "$(dirname "$0")/../../tools/base/common.sh"
source "$(dirname "$0")/../commands.sh"
OUT="$(abspath "$1")"; N="$(abspath "$2")"; SYSROOT="$(abspath "$3")"; shift 3
DEPS=(); for d in "$@"; do DEPS+=("$(abspath "$d")"); done
ROOT="$(find_root "${DEPS[@]}")"
PROJ="$(cd "$(dirname "$0")" && pwd)"
B="$(mktemp -d)"; trap 'rm -rf "$B"' EXIT
N="$(stage_src "$N" "$B/src" "$PROJ/patches")"   # patches/ (none yet) applied
cd "$N"
D="$B/derived"; mkdir -p "$D"   # BUILT_PRODUCTS_DIR
NC="$N/ncurses"; CAPS="$NC/include/Caps"
export AWK=awk

# The project's HEADER_SEARCH_PATHS and libraries.xcconfig's definitions;
# GCC_C_LANGUAGE_STANDARD gnu99, Release -Os. The project's OTHER_CFLAGS are
# Apple-internal clang options (typed memory operations) and are left out.
INC=(-I"$D" -I"$NC/include" -I"$NC/ncurses" -I"$NC/progs")
DEFS=(-DHAVE_CONFIG_H -D_XOPEN_SOURCE=600 -DSIGWINCH=28 -DNDEBUG -D_XOPEN_SOURCE_EXTENDED -DNCURSES_OPAQUE=0
	-DNCURSES_WANT_BASEABI -D_NCURSES_LIBBUILD)
TCPP=("${TARGET_FLAGS[@]}" $(cmd_sysroot_flags "$SYSROOT"))

# Bootstrap Sources: bootstrap_headers.sh and bootstrap_sources.sh.
{ cat "$NC/include/curses.head"; sh "$NC/include/MKkey_defs.sh" "$CAPS"; cat "$NC/include/curses.wide" \
	"$NC/include/curses.tail"; } > "$D/curses.h"
sh "$NC/include/MKhashsize.sh" "$CAPS" > "$D/hashsize.h"
sh "$NC/include/MKncurses_def.sh" "$NC/include/ncurses_defs" > "$D/ncurses_def.h"
awk -f "$NC/include/MKterm.h.awk" "$CAPS" > "$D/term.h"
sh "$NC/include/edit_cfg.sh" "$NC/include/ncurses_cfg.h" "$D/term.h" > /dev/null
awk -f "$NC/ncurses/tinfo/MKnames.awk" bigstrings=1 < "$CAPS" > "$D/names.c"

# native_make_hash, native_make_keys: the make_hash and make_keys targets for
# the build machine (their GCC_PREPROCESSOR_DEFINITIONS, the project's
# search paths and gnu99). make_hash's comp_hash.c needs comp_captab.c's
# tables, which it doesn't list; dead stripping (BSD.xcconfig) drops the
# callers.
NATIVE=(-std=gnu99 -Os -Wl,-dead_strip -w "${INC[@]}" -D_XOPEN_SOURCE_EXTENDED -D_DARWIN_C_SOURCE=_DARWIN_C_SOURCE)
xcrun -sdk macosx clang "${NATIVE[@]}" -DMAIN_PROGRAM "$NC/ncurses/tinfo/comp_hash.c" \
	"$NC/ncurses/tinfo/make_hash.c" -o "$D/make_hash"
xcrun -sdk macosx clang "${NATIVE[@]}" "$NC/ncurses/tinfo/make_keys.c" -o "$D/make_keys"

# Derived Sources: derived_headers.sh (the ncurses.modulemap is left out:
# headers are build-only) and derived_sources.sh. The preprocessor passes
# (expanded.c, lib_gen.c) see the target's headers.
sh "$NC/ncurses/tinfo/MKkeys_list.sh" "$CAPS" | LC_ALL=C sort > "$D/keys.list"
"$D/make_keys" "$D/keys.list" > "$D/init_keytry.h"
sh "$NC/include/MKparametrized.sh" "$CAPS" > "$D/parametrized.h"
cat > "$D/transform.h" <<EOF
#ifndef __TRANSFORM_H
#define __TRANSFORM_H 1
#include <progs.priv.h>
extern bool same_program(const char *, const char *);
#define PROG_CAPTOINFO "captoinfo"
#define PROG_INFOTOCAP "infotocap"
#define PROG_RESET     "reset"
#define PROG_INIT      "init"
#endif /* __TRANSFORM_H */
EOF
MACROS=(-DHAVE_CONFIG_H -U_XOPEN_SOURCE -D_XOPEN_SOURCE=600 -D_XOPEN_SOURCE_EXTENDED -DNDEBUG -DSIGWINCH=28)
CPP="xcrun clang -E ${TCPP[*]} -I$D -I$NC/ncurses -I$NC/include ${MACROS[*]}"
awk -f "$NC/ncurses/tinfo/MKcodes.awk" bigstrings=1 "$CAPS" > "$D/codes.c"
(cd "$D" && sh "$NC/ncurses/tinfo/MKcaptab.sh" awk 1 "$NC/ncurses/tinfo/MKcaptab.awk" "$CAPS" > comp_captab.c)
(cd "$D" && sh "$NC/ncurses/tty/MKexpanded.sh" "$CPP" > expanded.c)
# No fallback entries (the script's terminal list is empty).
(cd "$D" && sh "$NC/ncurses/tinfo/MKfallback.sh" /usr/share/terminfo "$NC/misc/terminfo.src" > fallback.c)
(cd "$D" && sh "$NC/ncurses/base/MKlib_gen.sh" "$CPP -DHAVE_CONFIG" awk generated < curses.h > lib_gen.c)
awk -f "$NC/ncurses/base/MKkeyname.awk" bigstrings=1 "$D/keys.list" > "$D/lib_keyname.c"
sh "$NC/progs/MKtermsort.sh" awk "$CAPS" > "$D/termsort.c"
echo | awk -f "$NC/ncurses/base/MKunctrl.awk" bigstrings=1 > "$D/unctrl.c"

# libncurses: 158 sources plus the 8 generated (sources.txt, from the
# target's PBXSourcesBuildPhase; no per-file flags). The apple-generic
# version symbols are left out: Apple's libncurses doesn't export them.
write_rsp "$B/cflags" "${TCPP[@]}" -std=gnu99 -Os -fno-common "${INC[@]}" "${DEFS[@]}"
SRCS=($(grep -v '^#' "$PROJ/sources.txt"))
compile "$B/obj" "$B/cflags" "${SRCS[@]}" "$D"/{codes,comp_captab,fallback,names,unctrl,lib_keyname,lib_gen,expanded}.c
# DYLIB_CURRENT/COMPATIBILITY_VERSION 5.4.
mkdir -p "$OUT/usr/lib"
xcrun ld -arch arm64 -platform_version macos 26.0 26.0 -dylib -dead_strip -adhoc_codesign \
	-install_name /usr/lib/libncurses.5.4.dylib -current_version 5.4 -compatibility_version 5.4 \
	-syslibroot "$ROOT" "$B"/obj/*.o -lSystem -o "$OUT/usr/lib/libncurses.5.4.dylib"
# link_libs.sh (install): libform, libmenu and libpanel aren't built.
for l in libncurses.dylib libncurses.5.dylib libcurses.dylib libtermcap.dylib; do
	ln -sf libncurses.5.4.dylib "$OUT/usr/lib/$l"
done

# install_headers.sh (macosx: usr/include), build-only here as the base's
# other headers are; the ncurses.h link.
H="$OUT/usr/local/include"; mkdir -p "$H"
cp "$NC/include/tic.h" "$NC/include/ncurses_dll.h" "$NC/include/unctrl.h" "$NC/include/nc_tparm.h" \
	"$D/term.h" "$NC/include/termcap.h" "$D/curses.h" "$NC/include/term_entry.h" "$H/"
ln -sf curses.h "$H/ncurses.h"

# run_tic.sh through the tic_static target, built for the build machine (its
# GCC_PREPROCESSOR_DEFINITIONS and sources, tic_static.txt). tic writes the
# hashed-directory tree macOS has (/usr/share/terminfo/76/vt100: no
# MIXEDCASE_FILENAMES); aliases are hard links, which the install tree holds
# as copies.
TIC_SRCS=(); for s in $(grep -v '^#' "$PROJ/tic_static.txt"); do case "$s" in @*) TIC_SRCS+=("$D/${s#@}") ;; *) TIC_SRCS+=("$s") ;; esac; done
mkdir -p "$B/native"
xcrun -sdk macosx clang -std=gnu99 -Os "${INC[@]}" -DHAVE_CONFIG_H -D_XOPEN_SOURCE=600 -D_XOPEN_SOURCE_EXTENDED \
	-w "${TIC_SRCS[@]}" -o "$B/native/tic_static"
mkdir -p "$OUT/usr/share"
(cd "$NC/misc" && PATH="$B/native:$PATH" DESTDIR="$OUT" suffix=_static prefix=/usr exec_prefix=/usr \
	bindir=/usr/bin datadir=/usr/share top_srcdir="$NC" srcdir="$NC/misc" TIC_PATH="$B/native/tic_static" \
	/bin/sh ./run_tic.sh > "$B/run_tic.log" 2>&1) || { tail -20 "$B/run_tic.log"; exit 1; }
for t in 76/vt100 78/xterm 78/xterm-256color 64/dumb; do
	[ -f "$OUT/usr/share/terminfo/$t" ] || { echo "ncurses: terminfo lacks $t" >&2; exit 1; }
done

# The commands (P4-21 checkpoint 3): the clear, infocmp, tic, toe, tput and
# tset targets (executables.xcconfig: /usr/bin, gnu99, the project's search
# paths and no definitions), linking libncurses, and fix_bin.sh's links (reset, captoinfo,
# infotocap). tabs (progs/tabs.c, FreeBSD's usr.bin/ncurses builds it) has
# no target in Apple's project and is built the same way.
write_rsp "$B/progs.rsp" "${TCPP[@]}" -std=gnu99 -Os -fno-common "${INC[@]}"
P="$NC/progs"
prog() { local t="$1"; shift; write_vers "$D/${t}_vers.c" "$t" ncurses 79 __
	tool "$B" "$ROOT" "$OUT/usr/bin/$t" "$B/progs.rsp" "$@" "$D/${t}_vers.c" -- -L"$OUT/usr/lib" -lncurses; }
prog clear "$P/clear.c"
prog infocmp "$P/infocmp.c" "$P/dump_entry.c"
prog tput "$P/transform.c" "$P/tput.c" "$P/tparm_type.c"
prog tset "$P/transform.c" "$P/tset.c"
prog toe "$P/toe.c"
prog tic "$P/tic.c" "$P/transform.c" "$P/tparm_type.c" "$P/dump_entry.c"
prog tabs "$P/tabs.c"
ln -sf tset "$OUT/usr/bin/reset"
ln -sf tic "$OUT/usr/bin/captoinfo"
ln -sf tic "$OUT/usr/bin/infotocap"
