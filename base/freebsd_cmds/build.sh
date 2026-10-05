#!/usr/bin/env bash
# SPDX-License-Identifier: BSD-2-Clause
# FreeBSD's leaf utilities from the source drop at 050683bb8e13 (P4-21
# checkpoint 4, docs/architecture/freebsd-parity.md §2.1): the programs
# macOS 26's release set doesn't publish, each built as its FreeBSD
# Makefile builds it (PROG, SRCS, CFLAGS, LIBADD, BINDIR, LINKS), one
# `prog` line per Makefile.
#   build.sh OUT FREEBSD_SRC SYSROOT DEPROOT...
#     (DEPROOT: //base:root, //base:libxo_dylib, //base:libutil_dylib,
#      //base:libsbuf_dylib, //base:libmd_dylib, //base:libz_dylib,
#      //base:bzip2_commands, //base:libpam_dylib, //base:libncurses_dylib)
# FREEBSD_SRC holds the freebsd-src files freebsd.lock pins, laid out as in
# freebsd-src. OUT receives the programs at FreeBSD's paths; base_library's
# man_pages installs their pages from FREEBSD_SRC.
#
# The shared pattern (compat/): every program is compiled with
#   -include compat/nd_freebsd.h  FreeBSD <sys/cdefs.h> names, reallocarray;
#   -I compat                     headers FreeBSD has and Darwin lacks:
#                                 <sys/capsicum.h> and <capsicum_helpers.h>
#                                 (no-ops, as a WITHOUT_CAPSICUM build),
#                                 <libcasper.h> and <casper/cap_fileargs.h>
#                                 (WITHOUT_CASPER's fallbacks), <sys/cpuset.h>,
#                                 FreeBSD's <uuid.h> and <sys/uuid.h>, and
#                                 <sys/sbuf.h> as libsbuf's;
# and linked with compat/nd_freebsd.c (and nd_uuid.c), whose unused
# functions -dead_strip drops. FreeBSD's internal libraries a Makefile
# names in LIBADD (libopenbsd, libnetbsd, libfifolog) are compiled into
# the program from their pinned sources, as INTERNALLIBs are static
# archives on FreeBSD. patches/ holds the changes to FreeBSD's sources.
source "$(dirname "$0")/../../tools/base/common.sh"
source "$(dirname "$0")/../commands.sh"
OUT="$(abspath "$1")"; F="$(abspath "$2")"; SYSROOT="$(abspath "$3")"; shift 3
DEPS=(); for d in "$@"; do DEPS+=("$(abspath "$d")"); done
ROOT="$(find_root "${DEPS[@]}")"
# dep LIB TARGET: the DEPROOT holding usr/lib/LIB.
dep() {
	local d; for d in "${DEPS[@]}"; do [ -e "$d/usr/lib/$1" ] && { printf '%s' "$d"; return 0; }; done
	echo "freebsd_cmds: no DEPROOT holds usr/lib/$1 (pass $2)" >&2; return 1
}
XO="$(dep libxo.dylib //base:libxo_dylib)"; UTIL="$(dep libutil.dylib //base:libutil_dylib)"
SBUF="$(dep libsbuf.dylib //base:libsbuf_dylib)"; MD="$(dep libmd.dylib //base:libmd_dylib)"
Z="$(dep libz.1.dylib //base:libz_dylib)"; BZ="$(dep libbz2.1.0.dylib //base:bzip2_commands)"
PAM="$(dep libpam.2.dylib //base:libpam_dylib)"; NC="$(dep libncurses.5.4.dylib //base:libncurses_dylib)"
PROJ="$(cd "$(dirname "$0")" && pwd)"
B="$(mktemp -d)"; trap 'rm -rf "$B"' EXIT
F="$(stage_src "$F" "$B/src" "$PROJ/patches")"   # patches/ applied
cd "$F"
C="$PROJ/compat"
# FreeBSD's <sys/tree.h> (macros only; xnu's userland headers have none),
# and an empty "namespace.h" for libc's pwcache.c.
I="$B/inc"; mkdir -p "$I/sys"; cp sys/sys/tree.h "$I/sys/"; : > "$I/namespace.h"

# Warnings change no interface and are left out (FreeBSD's WARNS levels
# are its own -Werror policy).
base=("${TARGET_FLAGS[@]}" -Os -std=gnu11 -fno-common -w -D__FBSDID=__RCSID -include "$C/nd_freebsd.h" -I"$C" -I"$I")

# prog OUTPATH 'CFLAG...' SRC... [-- LDFLAG...]: one FreeBSD Makefile's
# PROG, with its CFLAGS (word-split) and SRCS, linked with the compat
# layer and the LDFLAGs (its LIBADD).
n=0
prog() {
	local out="$1" cflags="$2"; shift 2
	n=$((n + 1))
	# shellcheck disable=SC2086
	write_rsp "$B/$n.rsp" "${base[@]}" $cflags $(cmd_sysroot_flags "$SYSROOT")
	local -a srcs=(); while [ $# -gt 0 ] && [ "$1" != "--" ]; do srcs+=("$1"); shift; done
	[ "${1:-}" = "--" ] && shift
	tool "$B" "$ROOT" "$OUT/$out" "$B/$n.rsp" "${srcs[@]}" "$C/nd_freebsd.c" -- "$@"
}
lib() { printf '%s\n' "-L$1/usr/lib"; }

# --- bin ---
prog bin/nproc "" bin/nproc/nproc.c
prog bin/uuidgen "" bin/uuidgen/uuidgen.c "$C/nd_uuid.c"
prog bin/pwait "" bin/pwait/pwait.c
prog bin/domainname "" bin/domainname/domainname.c

# --- usr.bin ---
prog usr/bin/asa "" usr.bin/asa/asa.c
prog usr/bin/fsync "" usr.bin/fsync/fsync.c
prog usr/bin/ident "-isystem $SBUF/usr/local/include" usr.bin/ident/ident.c -- $(lib "$SBUF") -lsbuf
prog usr/bin/ministat "" usr.bin/ministat/ministat.c
prog usr/bin/perror "" usr.bin/perror/perror.c
prog usr/bin/resizewin "" usr.bin/resizewin/resizewin.c
prog usr/bin/revoke "" usr.bin/revoke/revoke.c
prog usr/bin/ts "" contrib/ts/ts.c
# lock: setuid root (BINMODE 4555; images/BUILD.bazel sets the mode), PAM.
prog usr/bin/lock "-isystem $PAM/usr/local/include" usr.bin/lock/lock.c -- $(lib "$PAM") -lpam
# xo: contrib/libxo's xo.c with its headers and lib/libxo's xo_config.h.
prog usr/bin/xo "-I$F/lib/libxo/libxo -I$F/contrib/libxo/libxo" \
	contrib/libxo/xo/xo.c -- $(lib "$XO") -lxo
# getaddrinfo: tables.h from tables.awk over Darwin's <sys/socket.h> (the
# families and socket types this kernel has); libnetbsd's sockaddr_snprintf.
mkdir -p "$B/gai"
LC_ALL=C awk -f usr.bin/getaddrinfo/tables.awk "$SYSROOT/usr/include/sys/socket.h" > "$B/gai/tables.h"
prog usr/bin/getaddrinfo "-I$B/gai -Ilib/libnetbsd -isystem $UTIL/usr/local/include" \
	usr.bin/getaddrinfo/getaddrinfo.c lib/libnetbsd/sockaddr_snprintf.c
# m4: -DEXTENDED, libopenbsd's ohash; parser.y and tokenizer.l through the
# build machine's yacc and lex, as FreeBSD's bsd.prog.mk runs them.
mkdir -p "$B/m4"
(cd "$B/m4" && yacc -d -o parser.c "$F/usr.bin/m4/parser.y" && lex -o tokenizer.c "$F/usr.bin/m4/tokenizer.l")
prog usr/bin/m4 "-include stdlib.h -DEXTENDED -I$F/usr.bin/m4 -I$B/m4 -Ilib/libopenbsd" \
	usr.bin/m4/{eval,expr,look,main,misc,gnum4,trace}.c "$B/m4/parser.c" "$B/m4/tokenizer.c" lib/libopenbsd/ohash.c
# ee: contrib/ee with the Makefile's HAS_* definitions, over ncurses
# (FreeBSD links ncursesw; NeoDarwin's libncurses is the one library).
# ree and edit are hard links, as LINKS makes them. Its message catalogs
# (NLS) aren't installed; ee's built-in English strings are its C locale's.
prog usr/bin/ee "-DHAS_NCURSES -DHAS_UNISTD -DHAS_STDARG -DHAS_STDLIB -DHAS_SYS_WAIT -isystem $NC/usr/local/include" \
	contrib/ee/ee.c -- $(lib "$NC") -lncurses
ln -f "$OUT/usr/bin/ee" "$OUT/usr/bin/ree"; ln -f "$OUT/usr/bin/ee" "$OUT/usr/bin/edit"
# etdump: makefs's cd9660_conversion.c and headers.
prog usr/bin/etdump "-Isys/fs/cd9660 -Iusr.sbin/makefs -Iusr.sbin/makefs/cd9660" \
	usr.bin/etdump/{etdump,output_shell,output_text}.c usr.sbin/makefs/cd9660/cd9660_conversion.c
# bsdiff and bspatch (LIBADD bz2); bsdiff with contrib/libdivsufsort.
DSS="-DHAVE_CONFIG_H=1 -D_FILE_OFFSET_BITS=64 -D_LARGEFILE_SOURCE -D_LARGE_FILES -D__STDC_CONSTANT_MACROS"
DSS="$DSS -D__STDC_FORMAT_MACROS -D__STDC_LIMIT_MACROS -DBUILD_DIVSUFSORT64"
prog usr/bin/bsdiff "$DSS -Icontrib/libdivsufsort/include -Iusr.bin/bsdiff/bsdiff -isystem $BZ/usr/local/include" \
	contrib/libdivsufsort/lib/{divsufsort,sssort,trsort,utils}.c usr.bin/bsdiff/bsdiff/bsdiff.c -- $(lib "$BZ") -lbz2
prog usr/bin/bspatch "-isystem $BZ/usr/local/include" usr.bin/bsdiff/bspatch/bspatch.c -- $(lib "$BZ") -lbz2

# --- usr.sbin ---
prog usr/sbin/daemon "-isystem $UTIL/usr/local/include" usr.sbin/daemon/daemon.c -- $(lib "$UTIL") -lutil
prog usr/sbin/wake "" usr.sbin/wake/wake.c
# fifolog: lib/'s libfifolog (an INTERNALLIB, with getdate.y) in each of
# fifolog_create, fifolog_reader and fifolog_writer; LIBADD z, and util
# for fifolog_create.
mkdir -p "$B/fifolog"
(cd "$B/fifolog" && yacc -o getdate.c "$F/usr.sbin/fifolog/lib/getdate.y")
FL="-Iusr.sbin/fifolog/lib -isystem $Z/usr/local/include"
FLSRC=(usr.sbin/fifolog/lib/{fifolog_int,fifolog_create,fifolog_write_poll,fifolog_reader}.c "$B/fifolog/getdate.c")
prog usr/sbin/fifolog_create "$FL -isystem $UTIL/usr/local/include" usr.sbin/fifolog/fifolog_create/fifolog_create.c \
	"${FLSRC[@]}" -- $(lib "$Z") -lz $(lib "$UTIL") -lutil
prog usr/sbin/fifolog_reader "$FL" usr.sbin/fifolog/fifolog_reader/fifolog_reader.c "${FLSRC[@]}" -- $(lib "$Z") -lz
prog usr/sbin/fifolog_writer "$FL" usr.sbin/fifolog/fifolog_writer/fifolog_writer.c "${FLSRC[@]}" -- $(lib "$Z") -lz
# nmtree: contrib/mtree with contrib/mknod's pack_dev.c, libnetbsd, libmd
# and libutil; installed as mtree with the nmtree link (LINKS). From
# FreeBSD's libc and libutil, which Darwin's lack: pwcache.c's
# uid_from_user(), gid_from_group() and pwcache_userdb() (with its
# user_from_uid() and group_from_gid(), which take the program's lookups
# for -N; compat/nd_pwcache.h), and fparseln(). NO_RMD160: libmd, as macOS's, has no RIPEMD-160,
# so the rmd160digest keyword is refused.
prog usr/sbin/mtree "-include $C/nd_pwcache.h -DNO_RMD160 -Icontrib/mknod -Ilib/libnetbsd -Icontrib/libc-pwcache -isystem $MD/usr/local/include -isystem $UTIL/usr/local/include" \
	contrib/mtree/{compare,crc,create,excludes,getid,misc,mtree,only,spec,specspec,verify}.c contrib/mknod/pack_dev.c \
	lib/libnetbsd/{efun,strsuftoll,util}.c "$C/nd_pwcache.c" lib/libutil/fparseln.c \
	-- $(lib "$MD") -lmd $(lib "$UTIL") -lutil
ln -f "$OUT/usr/sbin/mtree" "$OUT/usr/sbin/nmtree"

# nmtree's pages (MAN=mtree.5 mtree.8, MLINKS mtree.8 nmtree.8), installed
# here since man_pages would take mtree.5 for the program's. man_pages
# installs the other programs' pages.
mkdir -p "$OUT/usr/share/man/man5" "$OUT/usr/share/man/man8"
install -m 0444 usr.sbin/nmtree/mtree.5 "$OUT/usr/share/man/man5/"
install -m 0444 contrib/mtree/mtree.8 "$OUT/usr/share/man/man8/"
install -m 0444 contrib/mtree/mtree.8 "$OUT/usr/share/man/man8/nmtree.8"
