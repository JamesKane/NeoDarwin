#!/usr/bin/env bash
# SPDX-License-Identifier: BSD-2-Clause
# zsh 5.9 from zsh-110.1.1 (docs/base/session.md): Apple's GNUSource build
# (Makefile) of the zsh/ tree: configure, make, make install.
#   build.sh OUT ZSH_SRC SYSROOT DEPROOT...   (DEPROOT: //base:root, //base:ncurses)
# OUT receives bin/zsh; its modules as bundles in usr/lib/zsh/5.9/zsh (+ net/,
# param/), as macOS has them; the functions (usr/share/zsh/5.9/functions,
# flat) and run-help files (usr/share/zsh/5.9/help); and private/etc/zshrc
# and zprofile (post-install).
# Configure: Apple's flags (--with-tcsetpgrp --enable-multibyte
# --enable-unicode9 --enable-max-function-depth=700, -DUSE_GETCWD), less
# --enable-pcre (no PCRE in the base). configure runs cross-compiling with
# config.cache, as Apple's embedded build does with configure.cache-embedded,
# so no answer comes from running a program against the host's libSystem;
# its compile and link checks see NeoDarwin's headers and link against the
# runtime root (-isysroot ROOT, so ld's -syslibroot is the root) and
# libncurses. The host SDK's headers come last (-idirafter), as they do
# after the sysroot's in TARGET_FLAGS; its libraries are never searched.
# The terminal library is libncurses (--with-term-lib); iconv comes from
# nowhere (Apple links libiconv, which the base doesn't have yet), so
# conversion in the multibyte code falls back to zsh's own UTF-8 handling.
# Modules are loadable bundles, as on macOS: DLLDFLAGS -bundle
# -flat_namespace -undefined suppress (configure's Darwin setting), so
# their references to zsh's own functions bind to /bin/zsh's exports at
# dlopen time.
source "$(dirname "$0")/../../tools/base/common.sh"
source "$(dirname "$0")/../commands.sh"
OUT="$(abspath "$1")"; Z="$(abspath "$2")"; SYSROOT="$(abspath "$3")"; shift 3
DEPS=(); for d in "$@"; do DEPS+=("$(abspath "$d")"); done
ROOT="$(find_root "${DEPS[@]}")"
NC=""; for d in "${DEPS[@]}"; do [ -f "$d/usr/lib/libncurses.5.4.dylib" ] && NC="$d"; done
[ -n "$NC" ] || { echo "zsh: no DEPROOT holds usr/lib/libncurses.5.4.dylib (pass //base:ncurses)" >&2; exit 1; }
PROJ="$(cd "$(dirname "$0")" && pwd)"
B="$(mktemp -d)"; trap 'rm -rf "$B"' EXIT
Z="$(stage_src "$Z" "$B/src" "$PROJ/patches")"   # patches/ (none yet) applied
cd "$Z/zsh"

# The build machine and a host triple that differs from it: configure then
# cross-compiles (cross_compiling=yes), and config.cache answers. The host
# is xnu-12377's Darwin release (uname -r 25.0.0), which sets $OSTYPE
# (darwin25.0.0) as a native build on macOS 26 would; config.sub knows arm64
# only as aarch64, so $MACHTYPE is aarch64 (Apple's init.c says arm64 only
# in --version).
BUILD="$(sh ./config.guess)"
cp "$PROJ/config.cache" "$B/config.cache"
export CC="xcrun clang" INSTALL="/usr/bin/install -c"
export CPPFLAGS="-I$NC/usr/local/include $(cmd_sysroot_flags "$SYSROOT" | tr '\n' ' ') -idirafter $SDK/usr/include -DUSE_GETCWD"
export CFLAGS="-arch arm64 -mmacosx-version-min=26.0 -isysroot $ROOT -Os -Wno-error=int-conversion -Wno-error=implicit-int"
export LDFLAGS="-arch arm64 -mmacosx-version-min=26.0 -isysroot $ROOT -L$NC/usr/lib"
./configure --build="$BUILD" --host=aarch64-apple-darwin25.0.0 --cache-file="$B/config.cache" \
	--prefix=/usr --bindir=/bin --mandir=/usr/share/man --infodir=/usr/share/info --sysconfdir=/private/etc \
	--enable-etcdir=/etc --with-term-lib=ncurses --with-tcsetpgrp --enable-multibyte --enable-unicode9 \
	--enable-max-function-depth=700 > "$B/configure.log" 2>&1 || { tail -30 "$B/configure.log"; exit 1; }
grep -q 'whether we are cross compiling... yes' "$B/configure.log" || { echo "zsh: configure didn't cross-compile" >&2; exit 1; }

make -j"$JOBS" > "$B/make.log" 2>&1 || { grep -m 20 -E "error|Error" "$B/make.log"; exit 1; }
# install.bin, install.modules, install.fns, install.runhelp (Makefile's
# install less the man pages). post-install: no zsh-5.9 link, no newuser
# script; zprofile and zshrc to private/etc, 0444.
make install.bin install.modules install.fns install.runhelp DESTDIR="$OUT" > "$B/install.log" 2>&1 ||
	{ tail -20 "$B/install.log"; exit 1; }
rm -f "$OUT/bin/zsh-5.9" "$OUT/usr/share/zsh/5.9/scripts/newuser"
mkdir -p "$OUT/private/etc"
install -m 0444 "$Z/zprofile" "$Z/zshrc" "$OUT/private/etc/"
