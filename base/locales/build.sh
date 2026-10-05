#!/usr/bin/env bash
# SPDX-License-Identifier: BSD-2-Clause
# The locale data in /usr/share/locale for C.UTF-8 and en_US.UTF-8 (P4-21
# checkpoint 3, docs/architecture/freebsd-parity.md §2.1). macOS 26's
# release set publishes none, so it is compiled from FreeBSD's CLDR-derived
# sources at 050683bb8e13 (freebsd.lock) as FreeBSD's share/*def Makefiles
# do, with adv_cmds-237's localedef, the base's own, which writes Apple's
# formats (RuneMagB LC_CTYPE, Apple's LC_COLLATE). It is built for the build
# machine from the same pinned source, as file's magic and ncurses' tic are.
#   build.sh OUT FREEBSD_SRC SYSROOT   (@apple_adv_cmds in the rule's data,
#                                       found next to FREEBSD_SRC in external/,
#                                       or ADV_CMDS_SRC)
# OUT receives usr/share/locale with the layout macOS has:
# - C.UTF-8/LC_CTYPE (share/ctypedef's C.UTF-8.src with map.UTF-8 and
#   widths.txt). Libc gives every other category of a "C." locale the C one,
#   so, as on macOS, that is all C.UTF-8 has;
# - en_US.UTF-8: LC_CTYPE a link to C.UTF-8's (ctypedef's SAME); LC_COLLATE
#   from colldef_unicode (CLDR 48.2); LC_TIME (timedef), LC_NUMERIC,
#   LC_MONETARY and LC_MESSAGES/LC_MESSAGES (the *def_unicode directories),
#   the .src files less their comments, as their .src.out rules do;
# - UTF-8/LC_CTYPE, a link to C.UTF-8's: macOS's codeset-only locale.
source "$(dirname "$0")/../../tools/base/common.sh"
OUT="$(abspath "$1")"; F="$(abspath "$2")"
ADV="${ADV_CMDS_SRC:-$(ls -d "$(dirname "$F")"/*apple_adv_cmds 2>/dev/null | head -1)}"
[ -f "$ADV/localedef/localedef.c" ] || { echo "locales: no adv_cmds source next to $F (set ADV_CMDS_SRC)" >&2; exit 1; }
B="$(mktemp -d)"; trap 'rm -rf "$B"' EXIT

# localedef for the build machine: adv_cmds/build.sh's localedef target
# (gnu17, -I localedef/libc -I localedef, parser.c from parser.y).
A="$(abspath "$ADV")/localedef"
xcrun yacc -d -o "$B/parser.c" "$A/parser.y"
xcrun -sdk macosx clang -std=gnu17 -Os -w -I"$A/libc" -I"$A" -I"$B" "$A"/scanner.c "$A"/charmap.c "$B/parser.c" \
	"$A"/collate.c "$A"/ctype.c "$A"/localedef.c "$A"/messages.c "$A"/monetary.c "$A"/numeric.c "$A"/time.c \
	"$A"/wide.c -o "$B/localedef"

MAPS="$F/tools/tools/locale/etc/final-maps"
L="$OUT/usr/share/locale"; mkdir -p "$L"
"$B/localedef" -U -c -w "$MAPS/widths.txt" -f "$MAPS/map.UTF-8" -i "$F/share/ctypedef/C.UTF-8.src" "$L/C.UTF-8"
"$B/localedef" -U -V 48.2 -f "$MAPS/map.UTF-8" -i "$F/share/colldef_unicode/en_US.UTF-8.src" "$L/en_US.UTF-8"
E="$L/en_US.UTF-8"
strip_src() { grep -v -E '^(#$|#[ ])' < "$1" > "$2"; }
strip_src "$F/share/timedef/en_US.UTF-8.src" "$E/LC_TIME"
strip_src "$F/share/numericdef_unicode/en_US.UTF-8.src" "$E/LC_NUMERIC"
strip_src "$F/share/monetdef_unicode/en_US.UTF-8.src" "$E/LC_MONETARY"
mkdir -p "$E/LC_MESSAGES"
strip_src "$F/share/msgdef_unicode/en_US.UTF-8.src" "$E/LC_MESSAGES/LC_MESSAGES"
ln -sf ../C.UTF-8/LC_CTYPE "$E/LC_CTYPE"
mkdir -p "$L/UTF-8"; ln -sf ../C.UTF-8/LC_CTYPE "$L/UTF-8/LC_CTYPE"
for f in C.UTF-8/LC_CTYPE en_US.UTF-8/LC_COLLATE; do
	[ -s "$L/$f" ] || { echo "locales: localedef wrote no $f" >&2; exit 1; }
done
chmod 0444 "$L"/C.UTF-8/LC_CTYPE "$E"/LC_COLLATE "$E"/LC_TIME "$E"/LC_NUMERIC "$E"/LC_MONETARY "$E"/LC_MESSAGES/LC_MESSAGES
