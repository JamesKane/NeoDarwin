#!/usr/bin/env bash
# SPDX-License-Identifier: BSD-2-Clause
# bash 3.2.57 from bash-140 (docs/base/session.md): replays bash.xcodeproj's
# bash target and the static libraries it links (readline, glob, libsh,
# intl), its ostype.h script phase, and its RC files phase.
#   build.sh OUT BASH_SRC SYSROOT DEPROOT...   (DEPROOT: //base:root, //base:libncurses_dylib)
# OUT receives bin/bash and private/etc/bashrc and profile.
# Configuration: Apple's build doesn't run configure. The project commits
# what configure and the Makefile would generate: config.h, pathnames.h,
# signames.h, syntax.c, version.h and builtins/*.c (from the .def files).
# They are used as they are, as ncurses' ncurses_cfg.h is: config.h
# describes libSystem's POSIX interfaces, all present in NeoDarwin's, and
# READLINE with HAVE_TERMCAP_H, for readline's termcap calls into
# libncurses. Two things are generated, as the project generates them:
# ostype.h, which the script phase writes from the build machine's uname -r
# (here xnu-12377's Darwin release, 25: OSTYPE darwin25, as on macOS 26),
# and y.tab.c and y.tab.h from parse.y, with the toolchain's yacc (bison 2.3)
# as Xcode's yacc rule runs it.
source "$(dirname "$0")/../../tools/base/common.sh"
source "$(dirname "$0")/../commands.sh"
OUT="$(abspath "$1")"; S="$(abspath "$2")"; SYSROOT="$(abspath "$3")"; shift 3
DEPS=(); for d in "$@"; do DEPS+=("$(abspath "$d")"); done
ROOT="$(find_root "${DEPS[@]}")"
NC=""; for d in "${DEPS[@]}"; do [ -f "$d/usr/lib/libncurses.5.4.dylib" ] && NC="$d"; done
[ -n "$NC" ] || { echo "bash: no DEPROOT holds usr/lib/libncurses.5.4.dylib (pass //base:libncurses_dylib)" >&2; exit 1; }
PROJ="$(cd "$(dirname "$0")" && pwd)"
B="$(mktemp -d)"; trap 'rm -rf "$B"' EXIT
S="$(stage_src "$S" "$B/src" "$PROJ/patches")"   # patches/ applied
cd "$S"
D="$B/derived"; mkdir -p "$D"   # SYMROOT

# ostype.h: the script phase's, for the target's Darwin release.
cat > "$D/ostype.h" <<EOF
#ifndef __OSTYPE__
#define __OSTYPE__

#define OSTYPE "darwin25"
#endif /* __OSTYPE__ */
EOF
# parse.y through yacc -d; execute_cmd.c and print_cmd.c include <y.tab.h>.
(cd "$D" && xcrun yacc -d "$S/bash-3.2/parse.y")

# The project's settings: GCC_PREPROCESSOR_DEFINITIONS, gnu99, -fno-common
# (GCC_NO_COMMON_BLOCKS); USER_HEADER_SEARCH_PATHS = $(SYMROOT) $(SRCROOT)/**
# with ALWAYS_SEARCH_USER_PATHS, so <...> includes search them too. The
# recursive path is given as the directories whose headers the sources
# include: SRCROOT (Apple's config.h and generated headers) ahead of
# bash-3.2's. lib/termcap (unbuilt) is left out, so <termcap.h> is
# libncurses' (the SDK's is too). Warning flags change no interface and
# are left out, as are the format-security errors.
# conftypes.h takes HOSTTYPE from the compiler's architecture macros under
# MACOSX, and knows no arm64: CONF_HOSTTYPE gives it macOS's "arm64"
# (MACHTYPE arm64-apple-darwin25, as /bin/bash on macOS 26 says).
BS="$S/bash-3.2"
INC=(-I"$D" -I"$S" -I"$BS" -I"$BS/include" -I"$BS/lib" -I"$BS/builtins" -I"$BS/lib/readline" -I"$BS/lib/glob"
	-I"$BS/lib/sh" -I"$BS/lib/intl" -I"$BS/lib/tilde")
write_rsp "$B/cflags" "${TARGET_FLAGS[@]}" -std=gnu99 -Os -fno-common "${INC[@]}" \
	-isystem "$NC/usr/local/include" $(cmd_sysroot_flags "$SYSROOT")
# GCC_PREPROCESSOR_DEFINITIONS, quoted as Xcode passes them (clang's
# response files take shell quoting).
cat >> "$B/cflags" <<'EOF'
-DM_UNIX
-DIN_LIBINTL
-DLIBDIR='"/usr/libdata"'
-DLOCALEDIR='"/usr/share/locale"'
-DLOCALE_ALIAS_PATH='"/usr/share/locale"'
-DPACKAGE='"BASH"'
-DSSH_SOURCE_BASHRC
-DCONF_VENDOR='"apple"'
-DCONF_MACHTYPE='"Mac"'
-DMACOSX
-DSHELL
-DHAVE_CONFIG_H
-DCONF_HOSTTYPE='"arm64"'
EOF

# The static libraries: readline, glob, libsh (libsh.a) and intl, each the
# sources of its PBXSourcesBuildPhase (sources.txt).
lib_srcs() { awk -v t="$1" '$1 == t { print $2 }' "$PROJ/sources.txt"; }
for t in readline glob sh intl; do
	compile "$B/obj/$t" "$B/cflags" $(lib_srcs "$t")
	xcrun libtool -static -no_warning_for_no_symbols -o "$B/lib$t.a" "$B/obj/$t"/*.o
done
# bash: its sources (sources.txt, parse.y as the generated y.tab.c), and the
# Frameworks phase: the four libraries and /usr/lib/libncurses.dylib.
# OTHER_LDFLAGS -search_paths_first is ld's default.
compile "$B/obj/bash" "$B/cflags" $(lib_srcs bash) "$D/y.tab.c"
link_tool "$ROOT" "$OUT/bin/bash" "$B"/obj/bash/*.o "$B/libintl.a" "$B/libreadline.a" "$B/libsh.a" \
	"$B/libglob.a" -L"$NC/usr/lib" -lncurses

# The RC files phase: bashrc and profile to /private/etc, read-only as the
# finale script leaves them. Not installed: the docs, man pages and bashbug,
# and the finale's /usr/local/bin/bash link (usr/local isn't part of the
# root, tools/base/stage_root.sh).
mkdir -p "$OUT/private/etc"
install -m 0444 "$S/bashrc" "$S/profile" "$OUT/private/etc/"
