#!/usr/bin/env bash
# SPDX-License-Identifier: BSD-2-Clause
# libedit from libedit-65 (NetBSD libedit 20121213-3.0; docs/base/session.md):
# replays libedit.xcodeproj's libedit target (xcodescripts/libedit.xcconfig)
# and the "all" target's install_misc.sh.
#   build.sh OUT LIBEDIT_SRC SYSROOT DEPROOT...   (DEPROOT: //base:root, //base:libncurses_dylib)
# OUT receives usr/lib/libedit.3.dylib and install_misc.sh's names for it
# (libedit.2, libedit.3.0, libedit, libreadline), and, build-only, its
# headers in usr/local/include: histedit.h, editline/readline.h and the
# readline/readline.h and readline/history.h links to it.
# Configuration: Apple's build doesn't run configure. Its config.h (from a
# configure run on Apple's side) and the make_lists.sh outputs (local/:
# fcns, help, the vi/emacs/common tables, historyn.c, tokenizern.c) are
# committed and used as they are, as ncurses' ncurses_cfg.h is: config.h
# describes libSystem's interfaces and the termcap calls of libncurses.
source "$(dirname "$0")/../../tools/base/common.sh"
source "$(dirname "$0")/../commands.sh"
OUT="$(abspath "$1")"; E="$(abspath "$2")"; SYSROOT="$(abspath "$3")"; shift 3
DEPS=(); for d in "$@"; do DEPS+=("$(abspath "$d")"); done
ROOT="$(find_root "${DEPS[@]}")"
NC=""; for d in "${DEPS[@]}"; do [ -f "$d/usr/lib/libncurses.5.4.dylib" ] && NC="$d"; done
[ -n "$NC" ] || { echo "libedit: no DEPROOT holds usr/lib/libncurses.5.4.dylib (pass //base:libncurses_dylib)" >&2; exit 1; }
PROJ="$(cd "$(dirname "$0")" && pwd)"
B="$(mktemp -d)"; trap 'rm -rf "$B"' EXIT
E="$(stage_src "$E" "$B/src" "$PROJ/patches")"   # patches/ (none yet) applied
cd "$E"

# libedit.xcconfig: gnu99; USER_HEADER_SEARCH_PATHS = src, and the target's
# header map, which finds config.h and local/'s headers. Release -Os. The
# warning flags change no interface and are left out. ncurses' headers
# (curses.h, term.h, termcap.h) are build-only in its tree.
write_rsp "$B/cflags" "${TARGET_FLAGS[@]}" -std=gnu99 -Os -fno-common -iquote "$E" -iquote "$E/local" \
	-iquote "$E/src" -isystem "$NC/usr/local/include" $(cmd_sysroot_flags "$SYSROOT")
SRCS=(src/chared.c src/common.c src/el.c src/emacs.c src/filecomplete.c src/hist.c src/history.c src/keymacro.c
	src/map.c src/parse.c src/prompt.c src/read.c src/readline.c src/refresh.c src/search.c src/sig.c
	src/terminal.c src/tokenizer.c src/tty.c src/unvis.c src/vi.c src/vis.c local/fcns.c local/help.c
	src/chartype.c src/eln.c src/wcsdup.c local/historyn.c local/tokenizern.c)
compile "$B/obj" "$B/cflags" "${SRCS[@]}"
# PRODUCT_NAME edit.3, DYLIB_CURRENT_VERSION 3.0, COMPATIBILITY_VERSION 2;
# OTHER_LDFLAGS' unexports list hides the internal symbols (and the
# apple-generic version symbols, which are therefore left out); the
# Frameworks phase links /usr/lib/libncurses.dylib.
mkdir -p "$OUT/usr/lib"
xcrun ld -arch arm64 -platform_version macos 26.0 26.0 -dylib -dead_strip -adhoc_codesign \
	-install_name /usr/lib/libedit.3.dylib -current_version 3.0 -compatibility_version 2 \
	-unexported_symbols_list "$E/unexports" -syslibroot "$ROOT" "$B"/obj/*.o \
	-L"$NC/usr/lib" -lncurses -lSystem -o "$OUT/usr/lib/libedit.3.dylib"
# install_misc.sh: the links, and the headers (macOS: usr/include; here
# build-only, as the base's other headers are). Man pages are left out.
for l in libedit.2.dylib libedit.3.0.dylib libedit.dylib libreadline.dylib; do
	ln -sf libedit.3.dylib "$OUT/usr/lib/$l"
done
H="$OUT/usr/local/include"; mkdir -p "$H/editline" "$H/readline"
install -m 0444 "$E/src/histedit.h" "$H/"
install -m 0444 "$E/src/editline/readline.h" "$H/editline/"
for h in readline.h history.h; do ln -sf ../editline/readline.h "$H/readline/$h"; done
