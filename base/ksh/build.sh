#!/usr/bin/env bash
# SPDX-License-Identifier: BSD-2-Clause
# ksh93u+m 1.0.10 (github.com/ksh93/ksh, EPL-2.0) for the OpenZFS test suite
# (docs/base/session.md, "ksh"; docs/architecture/filesystems.md §7).
#   build.sh OUT KSH_SRC SYSROOT DEPROOT...   (DEPROOT: //base:root)
# OUT receives bin/ksh, where macOS installs it (the suite's scripts start
# #!/bin/ksh -p).
#
# Why not Apple's ksh-42: it is ksh93u+ 2012-08-01, and its AST build fails
# under Xcode 27's clang (libast's va_list probe on arm64, libdll's dlopen
# probe, int-conversion and implicit-declaration errors, <sys/codesign.h>).
# ksh93u+m is the maintained continuation of the same AT&T code, and the
# ksh FreeBSD's ports ship (shells/ksh, ksh93), which OpenZFS's FreeBSD runs
# of the suite use.
#
# The build is the project's own: bin/package make (mamake over the
# Mamfiles, iffe for the feature tests), for libast, libcmd, libdll, libsum
# and ksh93. iffe has no cross-compiling mode: many of its tests run the
# program they built. So unlike zsh's configure (cross-compiling, answers
# from config.cache), this build compiles and links every probe for
# NeoDarwin, and runs it on the build machine. $CC is a wrapper that
# compiles against NeoDarwin's headers (the sysroot, then the host SDK's
# with -idirafter, as zsh's build does) and links against the runtime root
# (-isysroot ROOT: ld's -syslibroot), so a probe sees only what NeoDarwin
# declares and exports. A probe program is an arm64 Mach-O that loads
# /usr/lib/libSystem.B.dylib by that name, which on the build machine is
# macOS's: run probes measure behaviour (type sizes, signal numbers, stdio
# internals) of the same Libc and xnu lineage NeoDarwin's libSystem is
# built from. Links are ad hoc signed (ld's default for arm64; -adhoc_codesign
# as link_tool passes).
source "$(dirname "$0")/../../tools/base/common.sh"
source "$(dirname "$0")/../commands.sh"
OUT="$(abspath "$1")"; K="$(abspath "$2")"; SYSROOT="$(abspath "$3")"; shift 3
DEPS=(); for d in "$@"; do DEPS+=("$(abspath "$d")"); done
ROOT="$(find_root "${DEPS[@]}")"
PROJ="$(cd "$(dirname "$0")" && pwd)"
B="$(mktemp -d)"; trap 'rm -rf "$B"' EXIT
K="$(stage_src "$K" "$B/src" "$PROJ/patches")"   # patches/ (none yet) applied

# The compiler: Xcode's clang for NeoDarwin (TARGET_FLAGS' architecture and
# deployment target), headers from the sysroot ahead of the SDK's, libraries
# from the root only. -O2 as package make's default.
CLANG="$(xcrun -f clang)"
cat > "$B/cc" <<CC
#!/bin/sh
exec "$CLANG" -arch arm64 -mmacosx-version-min=26.0 -isysroot "$ROOT" \\
	$(cmd_sysroot_flags "$SYSROOT" | tr '\n' ' ') -idirafter "$SDK/usr/include" \\
	-Wl,-adhoc_codesign "\$@"
CC
chmod +x "$B/cc"

# package make builds in arch/<HOSTTYPE> under the tree; its own temporaries
# go to TMPDIR. HOME keeps it from reading the user's files. No tests are
# run (package test is a separate action).
cd "$K"
export CC="$B/cc" CCFLAGS="-O2" TMPDIR="$B/tmp" HOME="$B/home"
mkdir -p "$TMPDIR" "$HOME"
unset SDKROOT
bin/package make > "$B/make.log" 2>&1 || true
KSH="$(ls "$K"/arch/*/bin/ksh 2>/dev/null | head -1)"
if [ -z "$KSH" ] || grep -q '\*\*\* exit code' "$K"/arch/*/lib/package/gen/make.out 2>/dev/null; then
	grep -h -B5 '\*\*\* exit code' "$K"/arch/*/lib/package/gen/make.out 2>/dev/null | tail -60
	tail -20 "$B/make.log"; echo "ksh: package make failed" >&2; exit 1
fi
# Only libSystem: libast, libcmd, libdll and libsum are linked statically.
deps="$(xcrun otool -L "$KSH" | tail -n +2 | awk '{print $1}')"
[ "$deps" = "/usr/lib/libSystem.B.dylib" ] || { echo "ksh: unexpected libraries: $deps" >&2; exit 1; }
install -d "$OUT/bin"
install -m 0555 "$KSH" "$OUT/bin/ksh"
