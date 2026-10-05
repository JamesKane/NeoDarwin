#!/usr/bin/env bash
# SPDX-License-Identifier: BSD-2-Clause
# calendar, leave, ncal and cal, tsort and units from misc_cmds-45 (P4-21,
# docs/architecture/freebsd-parity.md §2.1): replays misc_cmds.xcodeproj's
# targets with the project's Release settings (VERSIONING_SYSTEM
# apple-generic, VERSION_INFO_PREFIX __, DEAD_CODE_STRIPPING; INSTALL_PATH
# /usr/bin) and each target's own.
#   build.sh OUT MISC_CMDS_SRC SYSROOT DEPROOT...   (DEPROOT: //base:root, //base:libutil_dylib,
#                                                    //base:libedit_dylib, //base:libncurses_dylib)
# OUT receives usr/bin/{calendar,leave,ncal,cal,tsort,units}, calendar's
# calendar.apple and calendar.freebsd in usr/local/share/calendar (its copy
# phase's destination) and units' usr/share/misc/units.lib.
# calendar includes libutil.h (no library: io.c has its own getlocalbase),
# units links libedit (libedit.tbd), ncal and cal libncurses.
source "$(dirname "$0")/../../tools/base/common.sh"
source "$(dirname "$0")/../commands.sh"
OUT="$(abspath "$1")"; M="$(abspath "$2")"; SYSROOT="$(abspath "$3")"; shift 3
DEPS=(); for d in "$@"; do DEPS+=("$(abspath "$d")"); done
ROOT="$(find_root "${DEPS[@]}")"
UTIL=""; for d in "${DEPS[@]}"; do [ -f "$d/usr/lib/libutil.dylib" ] && UTIL="$d"; done
[ -n "$UTIL" ] || { echo "misc_cmds: no DEPROOT holds usr/lib/libutil.dylib (pass //base:libutil_dylib)" >&2; exit 1; }
LE=""; for d in "${DEPS[@]}"; do [ -f "$d/usr/lib/libedit.3.dylib" ] && LE="$d"; done
[ -n "$LE" ] || { echo "misc_cmds: no DEPROOT holds usr/lib/libedit.3.dylib (pass //base:libedit_dylib)" >&2; exit 1; }
NC=""; for d in "${DEPS[@]}"; do [ -f "$d/usr/lib/libncurses.5.4.dylib" ] && NC="$d"; done
[ -n "$NC" ] || { echo "misc_cmds: no DEPROOT holds usr/lib/libncurses.5.4.dylib (pass //base:libncurses_dylib)" >&2; exit 1; }
PROJ="$(cd "$(dirname "$0")" && pwd)"
B="$(mktemp -d)"; trap 'rm -rf "$B"' EXIT
M="$(stage_src "$M" "$B/src" "$PROJ/patches")"   # patches/ applied
cd "$M"
D="$B/derived"; mkdir -p "$D"

# The project's settings (GCC_TREAT_IMPLICIT_FUNCTION_DECLARATIONS_AS_ERRORS
# is clang's default); warning flags change no interface and are left out.
base=("${TARGET_FLAGS[@]}" -Os -fno-common $(cmd_sysroot_flags "$SYSROOT"))
vers() { write_vers "$D/${1}_vers.c" "$1" misc_cmds 45 __; printf '%s' "$D/${1}_vers.c"; }
write_rsp "$B/cflags" "${base[@]}"

# calendar: GCC_PREPROCESSOR_DEFINITIONS __FBSDID=__RCSID.
write_rsp "$B/calendar.rsp" "${base[@]}" -D__FBSDID=__RCSID -I"$UTIL/usr/local/include"
tool "$B" "$ROOT" "$OUT/usr/bin/calendar" "$B/calendar.rsp" calendar/calendar.c calendar/locale.c \
	calendar/parsedata.c calendar/day.c calendar/io.c calendar/sunpos.c calendar/events.c calendar/pom.c \
	calendar/dates.c calendar/ostern.c calendar/paskha.c "$(vers calendar)"
mkdir -p "$OUT/usr/local/share/calendar"
install -m 0644 calendar/calendars/calendar.apple calendar/calendars/calendar.freebsd "$OUT/usr/local/share/calendar/"
tool "$B" "$ROOT" "$OUT/usr/bin/leave" "$B/cflags" leave/leave.c "$(vers leave)"
tool "$B" "$ROOT" "$OUT/usr/bin/tsort" "$B/cflags" tsort/tsort.c "$(vers tsort)"
# units: links libedit; units.lib in /usr/share/misc.
write_rsp "$B/units.rsp" "${base[@]}" -isystem "$LE/usr/local/include"
tool "$B" "$ROOT" "$OUT/usr/bin/units" "$B/units.rsp" units/units.c "$(vers units)" -- -L"$LE/usr/lib" -ledit
mkdir -p "$OUT/usr/share/misc"
install -m 0644 units/units.lib "$OUT/usr/share/misc/"
# ncal and cal: two targets with the same sources and settings (OTHER_CFLAGS
# -D__FBSDID=__RCSID), linking libncurses; ncal.c includes its own
# calendar.h as <calendar.h>.
write_rsp "$B/ncal.rsp" "${base[@]}" -D__FBSDID=__RCSID -I"$M/ncal" -I"$NC/usr/local/include"
for t in ncal cal; do
	tool "$B" "$ROOT" "$OUT/usr/bin/$t" "$B/ncal.rsp" ncal/calendar.c ncal/easter.c ncal/ncal.c "$(vers $t)" \
		-- -L"$NC/usr/lib" -lncurses
done
