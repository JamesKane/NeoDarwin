#!/usr/bin/env bash
# SPDX-License-Identifier: BSD-2-Clause
# vis 0.9 (Marc André Tanner's editor, github.com/martanne/vis, ISC) as
# NeoDarwin's vi (user decision 2026-10-05, docs/architecture/freebsd-parity.md
# §6), with the three libraries it needs linked in statically: libtermkey
# 0.22 (keyboard input), Lua 5.4.9 (its configuration, file types and syntax
# highlighting) and LPeg 1.1.0 (its lexers). Nothing else in the base links
# Lua, so there is no shared liblua. TRE isn't built: vis's text-regex.c uses
# Libc's regcomp/regexec, as vis does wherever TRE is absent; TRE only adds
# matching across the gaps of vis's piece table and NUL bytes.
#   build.sh OUT VIS_SRC SYSROOT DEPROOT...   (DEPROOT: //base:root, //base:libncurses_dylib;
#       @leonerd_libtermkey, @puc_rio_lua and @puc_rio_lpeg in the rule's data,
#       found next to VIS_SRC in external/)
# The build is vis's Makefile with the config.mk its configure would write
# for this system (written out below, not run: configure's checks link test
# programs): CONFIG_CURSES (ncurses, the base's libncurses), CONFIG_LUA,
# CONFIG_LPEG (LPeg in the binary, registered in package.preload),
# CONFIG_TRE 0, CONFIG_ACL and CONFIG_SELINUX 0 (Linux only), Darwin's
# -D_DARWIN_C_SOURCE, and configure's hardening (-fstack-protector-all).
# OUT receives the editor as usr/bin/vi; usr/bin/{vis-menu,vis-digraph} and
# the scripts vis-clipboard, vis-complete and vis-open; vis's Lua runtime
# (lua/: vis.lua, visrc.lua, plugins, lexers, themes; not lua/doc) in
# usr/share/vis, which VIS_PATH names; and the pages in usr/share/man/man1,
# vis.1 as vi.1. The editor isn't installed as /usr/bin/vis: that is BSD's
# vis(1) (text_cmds; FreeBSD's usr.bin/vis), which displays non-printable
# characters, and it keeps its name. Nothing calls the editor by name (its
# Lua plugins run vis-menu, vis-complete, vis-open and vis-digraph). vis has
# no ex mode and no read-only mode, so there is no ex and no view.
source "$(dirname "$0")/../../tools/base/common.sh"
source "$(dirname "$0")/../commands.sh"
OUT="$(abspath "$1")"; V="$(abspath "$2")"; SYSROOT="$(abspath "$3")"; shift 3
DEPS=(); for d in "$@"; do DEPS+=("$(abspath "$d")"); done
ROOT="$(find_root "${DEPS[@]}")"
NC=""; for d in "${DEPS[@]}"; do [ -f "$d/usr/lib/libncurses.5.4.dylib" ] && NC="$d"; done
[ -n "$NC" ] || { echo "vis: no DEPROOT holds usr/lib/libncurses.5.4.dylib (pass //base:libncurses_dylib)" >&2; exit 1; }
sibling() { local d; d="$(ls -d "$(dirname "$V")"/*"$1" 2>/dev/null | head -1)"
	[ -f "$d/$2" ] || { echo "vis: no @$1 next to $V (its $2)" >&2; exit 1; }; printf '%s' "$d"; }
TK="$(sibling leonerd_libtermkey termkey.c)"
LUA="$(sibling puc_rio_lua src/lua.h)/src"
LPEG="$(sibling puc_rio_lpeg lpvm.c)"
PROJ="$(cd "$(dirname "$0")" && pwd)"
B="$(mktemp -d)"; trap 'rm -rf "$B"' EXIT
V="$(stage_src "$V" "$B/vis" "$PROJ/patches")"   # patches/ (none) applied
VERSION=0.9
cd "$V"
cp config.def.h config.h   # the Makefile's config.h rule

SYS=("${TARGET_FLAGS[@]}" $(cmd_sysroot_flags "$SYSROOT") -I"$NC/usr/local/include")

# Lua 5.4.9: src/Makefile's CORE_O and LIB_O (not lua.c or luac.c) with its
# CFLAGS (-O2 -std=gnu99 -DLUA_COMPAT_5_3) and LUA_USE_POSIX (its macosx
# target's LUA_USE_MACOSX less LUA_USE_DLOPEN: Lua is static and dead
# stripped, so C modules couldn't link against it; vis loads Lua files only).
write_rsp "$B/lua.rsp" "${SYS[@]}" -O2 -std=gnu99 -Wall -DLUA_COMPAT_5_3 -DLUA_USE_POSIX
LUA_SRCS=(); for s in lapi lcode lctype ldebug ldo ldump lfunc lgc llex lmem lobject lopcodes lparser lstate \
	lstring ltable ltm lundump lvm lzio lauxlib lbaselib lcorolib ldblib liolib lmathlib loadlib loslib \
	lstrlib ltablib lutf8lib linit; do LUA_SRCS+=("$LUA/$s.c"); done
compile "$B/obj/lua" "$B/lua.rsp" "${LUA_SRCS[@]}"

# LPeg 1.1.0: its makefile's five sources (-O2, -std=c99 for its gnu99).
write_rsp "$B/lpeg.rsp" "${SYS[@]}" -O2 -std=gnu99 -I"$LUA"
compile "$B/obj/lpeg" "$B/lpeg.rsp" "$LPEG"/{lpvm,lpcap,lptree,lpcode,lpprint,lpcset}.c

# libtermkey 0.22: termkey.c and its CSI and terminfo drivers, with ncurses
# for terminfo (its Makefile's fallback without unibilium: -lncurses).
write_rsp "$B/termkey.rsp" "${SYS[@]}" -O2 -std=c99 -D_DARWIN_C_SOURCE
compile "$B/obj/termkey" "$B/termkey.rsp" "$TK"/{termkey,driver-csi,driver-ti}.c

# vis: the Makefile's SRC with CFLAGS_VIS, CFLAGS_STD and configure's
# CFLAGS (-O2 -ffunction-sections -fdata-sections; dead stripping is the
# linker's -dead_strip) and CFLAGS_AUTO. vis-lua.c uses Lua 5.3's integer
# casts (luaL_checkint, lua_pushunsigned), which LUA_COMPAT_5_3 keeps, as
# distributions' Lua 5.4 builds do.
VIS_SRCS=(array.c buffer.c libutf.c main.c map.c sam.c text.c text-common.c text-io.c text-iterator.c
	text-motions.c text-objects.c text-util.c ui-terminal.c view.c vis.c vis-lua.c vis-marks.c vis-modes.c
	vis-motions.c vis-operators.c vis-prompt.c vis-registers.c vis-text-objects.c vis-subprocess.c text-regex.c)
STD=(-std=c99 -U_XOPEN_SOURCE -D_XOPEN_SOURCE=700 -DNDEBUG -D_DARWIN_C_SOURCE "-DVERSION=\\\"$VERSION\\\""
	-DHAVE_MEMRCHR=0 -O2 -fstack-protector-all)
write_rsp "$B/vis.rsp" "${SYS[@]}" "${STD[@]}" -I"$TK" -I"$LUA" -I"$V" '-DVIS_PATH=\"/usr/share/vis\"' \
	-DCONFIG_HELP=1 -DCONFIG_CURSES=1 -DCONFIG_LUA=1 -DCONFIG_LPEG=1 -DCONFIG_TRE=0 -DCONFIG_SELINUX=0 \
	-DCONFIG_ACL=0 -DLUA_COMPAT_5_3
compile "$B/obj/vis" "$B/vis.rsp" "${VIS_SRCS[@]}"
link_tool "$ROOT" "$OUT/usr/bin/vi" "$B"/obj/vis/*.o "$B"/obj/termkey/*.o "$B"/obj/lpeg/*.o "$B"/obj/lua/*.o \
	-L"$NC/usr/lib" -lncurses
write_rsp "$B/std.rsp" "${SYS[@]}" "${STD[@]}"
tool "$B" "$ROOT" "$OUT/usr/bin/vis-menu" "$B/std.rsp" vis-menu.c
tool "$B" "$ROOT" "$OUT/usr/bin/vis-digraph" "$B/std.rsp" vis-digraph.c
install -m 0755 vis-clipboard vis-complete vis-open "$OUT/usr/bin/"

# The Makefile's install target: lua/ less lua/doc, and the pages with
# VERSION filled in.
mkdir -p "$OUT/usr/share/vis" "$OUT/usr/share/man/man1"
(cd lua && tar cf - --exclude ./doc .) | (cd "$OUT/usr/share/vis" && tar xf -)
chmod -R u=rwX,go=rX "$OUT/usr/share/vis"
# vis.1 becomes vi.1 for the program's name here: its title and its NAME
# section's .Nm (which every later bare .Nm repeats) say vi, so whatis(1)
# doesn't list the editor under BSD vis(1)'s name.
for m in vis-menu vis-digraph vis-clipboard vis-complete vis-open; do
	sed -e "s/VERSION/$VERSION/" "man/$m.1" > "$OUT/usr/share/man/man1/$m.1"
done
sed -e "s/VERSION/$VERSION/" -e 's/^\.Dt VIS 1$/.Dt VI 1/' -e '1,/^\.Nm vis$/s/^\.Nm vis$/.Nm vi/' man/vis.1 \
	> "$OUT/usr/share/man/man1/vi.1"
chmod 0444 "$OUT"/usr/share/man/man1/*.1
[ -f "$OUT/usr/share/vis/vis.lua" ] && [ -d "$OUT/usr/share/vis/lexers" ] || { echo "vis: lua/ not installed" >&2; exit 1; }
