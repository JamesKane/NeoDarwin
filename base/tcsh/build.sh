#!/usr/bin/env bash
# SPDX-License-Identifier: BSD-2-Clause
# tcsh 6.21 from tcsh-75 as /bin/tcsh and /bin/csh (P4-21 checkpoint 3,
# docs/architecture/freebsd-parity.md §2.1 and §6): Apple's GNUSource build
# (Makefile) of the tcsh/ tree: configure (--bindir=/bin, ac_cv_func_sbrk=no),
# make with Extra_CC_Flags (_PATH_TCSHELL "/bin/tcsh", DARWIN; the
# -fno-typed-* options are Apple-internal clang's and -mdynamic-no-pic and
# -no-cpp-precomp are obsolete, so they're left out), and its install,
# install-links (csh is a hard link), install-rc and install.man targets.
#   build.sh OUT TCSH_SRC SYSROOT DEPROOT...   (DEPROOT: //base:root, //base:libncurses_dylib)
# OUT receives bin/tcsh and bin/csh, private/etc/csh.{cshrc,login,logout},
# and tcsh.1 and csh.1.
# configure cross-compiles with config.cache, as base/zsh does: its checks
# see NeoDarwin's headers and link against the runtime root and libncurses
# (tgetent, for the termcap library). gethost, which generates tc.defs.c's
# host table, runs on the build machine (CC_FOR_GETHOST, the host SDK).
source "$(dirname "$0")/../../tools/base/common.sh"
source "$(dirname "$0")/../commands.sh"
OUT="$(abspath "$1")"; T="$(abspath "$2")"; SYSROOT="$(abspath "$3")"; shift 3
DEPS=(); for d in "$@"; do DEPS+=("$(abspath "$d")"); done
ROOT="$(find_root "${DEPS[@]}")"
NC=""; for d in "${DEPS[@]}"; do [ -f "$d/usr/lib/libncurses.5.4.dylib" ] && NC="$d"; done
[ -n "$NC" ] || { echo "tcsh: no DEPROOT holds usr/lib/libncurses.5.4.dylib (pass //base:libncurses_dylib)" >&2; exit 1; }
PROJ="$(cd "$(dirname "$0")" && pwd)"
B="$(mktemp -d)"; trap 'rm -rf "$B"' EXIT
T="$(stage_src "$T" "$B/src" "$PROJ/patches")"   # patches/ applied
cd "$T/tcsh"

BUILD="$(sh ./config.guess)"
cp "$PROJ/config.cache" "$B/config.cache"
export CC="xcrun clang" INSTALL="/usr/bin/install -c"
export CPPFLAGS="-I$NC/usr/local/include $(cmd_sysroot_flags "$SYSROOT" | tr '\n' ' ') -idirafter $SDK/usr/include"
export CFLAGS="-arch arm64 -mcpu=$TARGET_CPU -mmacosx-version-min=26.0 -isysroot $ROOT -Os -Wno-error=int-conversion -D_PATH_TCSHELL=\\\"/bin/tcsh\\\" -DDARWIN -fstack-protector-all"
export LDFLAGS="-arch arm64 -mmacosx-version-min=26.0 -isysroot $ROOT -L$NC/usr/lib"
./configure --build="$BUILD" --host=aarch64-apple-darwin25.0.0 --cache-file="$B/config.cache" \
	--prefix=/usr --bindir=/bin --mandir=/usr/share/man --sysconfdir=/private/etc ac_cv_func_sbrk=no \
	> "$B/configure.log" 2>&1 || { tail -30 "$B/configure.log"; exit 1; }
grep -q 'whether we are cross compiling... yes' "$B/configure.log" || { echo "tcsh: configure didn't cross-compile" >&2; exit 1; }

# gethost's rule passes the target's CPPFLAGS, CFLAGS and LDFLAGS: build it
# first for the host, after the headers it includes are generated.
make sh.err.h tc.const.h ed.defns.h > "$B/make.log" 2>&1 || { grep -m 20 -E "error:|Error [0-9]" "$B/make.log"; exit 1; }
make gethost CC_FOR_GETHOST="xcrun -sdk macosx clang" CPPFLAGS="-I." CFLAGS="-Os -w" LDFLAGS= >> "$B/make.log" 2>&1 ||
	{ grep -m 20 -E "error:|Error [0-9]" "$B/make.log"; exit 1; }
make -j"$JOBS" >> "$B/make.log" 2>&1 || { grep -m 20 -E "error:|Error [0-9]" "$B/make.log"; exit 1; }
make install install.man DESTBIN="$OUT/bin" MANSECT=1 DESTMAN="$OUT/usr/share/man/man1" DESTDIR="$OUT" \
	> "$B/install.log" 2>&1 || { tail -20 "$B/install.log"; exit 1; }
ln -f "$OUT/bin/tcsh" "$OUT/bin/csh"
ln -f "$OUT/usr/share/man/man1/tcsh.1" "$OUT/usr/share/man/man1/csh.1"
mkdir -p "$OUT/private/etc"
install -m 0644 "$T/csh.cshrc" "$T/csh.login" "$T/csh.logout" "$OUT/private/etc/"
