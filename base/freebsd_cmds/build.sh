#!/usr/bin/env bash
# SPDX-License-Identifier: BSD-2-Clause
# FreeBSD's leaf utilities and heavier programs from the source drop at
# 050683bb8e13 (P4-21 checkpoints 4 and 5, docs/architecture/freebsd-parity.md
# §2.1; iasl and acpidb from @acpica, next to FREEBSD_SRC): the programs
# macOS 26's release set doesn't publish, each built as its FreeBSD
# Makefile builds it (PROG, SRCS, CFLAGS, LIBADD, BINDIR, LINKS), one
# `prog` line per Makefile.
#   build.sh OUT FREEBSD_SRC SYSROOT DEPROOT...
#     (DEPROOT: //base:root, //base:libxo_dylib, //base:libutil_dylib,
#      //base:libsbuf_dylib, //base:libmd_dylib, //base:libz_dylib,
#      //base:bzip2_commands, //base:libpam_dylib, //base:libncurses_dylib,
#      //base:bsm_audit, //base:libcrypto_dylib, //base:libipsec_dylib)
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
OUT="$(abspath "$1")"; F="$(abspath "$2")"; F0="$F"; SYSROOT="$(abspath "$3")"; shift 3
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
BSM="$(dep libbsm.0.dylib //base:bsm_audit)"; SSL="$(dep libssl.3.dylib //base:libcrypto_dylib)"
IPSEC="$(dep libipsec.A.dylib //base:libipsec_dylib)"
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
# OpenSSL's headers and its -lcrypto/-lssl links are build-only, in
# usr/local/openssl (base/openssl/build.sh).
OSSL="-isystem $SSL/usr/local/openssl/include"; ossl() { printf '%s\n' "-L$SSL/usr/local/openssl/lib"; }

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

# bmake: FreeBSD's make (contrib/bmake with usr.bin/bmake's config.h and
# Makefile.config), installed as make with the bmake link (Makefile.inc's
# PROG and LINKS). USE_FILEMON=no: there is no filemon(4), so meta mode
# records commands and their output without the files they touch.
# MACHINE_ARCH is aarch64, as hw.machine_arch says on FreeBSD/arm64 (xnu has
# no such sysctl); MACHINE is uname -m's arm64, the same as FreeBSD's.
# FreeBSD's share/mk (sys.mk, bsd.*.mk) is installed in /usr/share/mk, the
# end of DEFAULT_SYS_PATH: share/mk/Makefile's FILES (with MK_TESTS's), not
# the src.*.mk and local.*.mk files that only a source tree's build reads.
MK="contrib/bmake"
prog usr/bin/make "-DHAVE_CONFIG_H -DMAKE_NATIVE -DUSE_META -DNO_PWD_OVERRIDE -DBMAKE_PATH_MAX=1024 -DMACHINE_ARCH=\\\"aarch64\\\" -D_PATH_DEFSYSPATH=\\\".../share/mk:/usr/share/mk\\\" -DMAKE_VERSION=\\\"20260704\\\" -DMAKE_SAVE_DOLLARS_DEFAULT=\\\"no\\\" -Iusr.bin/bmake -I$MK" \
	$MK/{arch,buf,compat,cond,dir,for,hash,job,lst,main,make,make_malloc,meta,metachar,parse,str,suff,targ,trace,util,var,stresep}.c
ln -f "$OUT/usr/bin/make" "$OUT/usr/bin/bmake"
mkdir -p "$OUT/usr/share/mk"
for f in $(awk '/^FILES[+]?=/ { f = 1 } f { for (i = 1; i <= NF; i++) if ($i ~ /[.](mk|sh|py|awk)$|README$/) print $i
	if ($NF != "\\") f = 0 }' share/mk/Makefile | sed 's#^[$]{SRCTOP}/##; s#^[^/]*$#share/mk/&#'); do
	case "$f" in *.sh|*.py) m=0555 ;; *) m=0444 ;; esac
	install -m "$m" "$f" "$OUT/usr/share/mk/"
done

# dtc: FreeBSD's BSD-licensed device tree compiler (PROG_CXX), C++ with
# -fno-rtti -fno-exceptions, linked with libc++ (headers from the sysroot).
write_rsp "$B/dtc.rsp" "${TARGET_FLAGS[@]}" -Os -std=gnu++17 -fno-rtti -fno-exceptions -w -nostdinc++ \
	-isystem "$SYSROOT/usr/include/c++/v1" -D__FBSDID=__RCSID -I"$C" $(cmd_sysroot_flags "$SYSROOT")
tool "$B" "$ROOT" "$OUT/usr/bin/dtc" "$B/dtc.rsp" usr.bin/dtc/{dtc,input_buffer,string,dtb,fdt,checking}.cc -- -lc++
# mkimg: every format and scheme of the Makefile; LIBADD util (expand_number).
prog usr/bin/mkimg "-DMKIMG_VERSION=20161016 -DSPARSE_WRITE -Isys/sys/disk -isystem $UTIL/usr/local/include" \
	usr.bin/mkimg/{format,image,mkimg,scheme,uuid,qcow,raw,vhd,vhdx,vmdk,apm,bsd,ebr,gpt,mbr}.c "$C/nd_uuid.c" -- $(lib "$UTIL") -lutil

# fetch with lib/libfetch compiled in (its Makefile's -DINET6 -DWITH_SSL
# -DFTP_COMBINE_CWDS, LIBADD ssl crypto, over the base's OpenSSL 3.5), with
# ftperr.h and httperr.h made from the .errors tables as its Makefile makes
# them. FreeBSD's <netinet/in.h> has IPPORT_MAX and its <poll.h> INFTIM;
# its <termios.h> reaches ioctl(2). FreeBSD also installs libfetch.so.6; nothing in NeoDarwin's base
# links it, so no libfetch.dylib is installed yet.
mkdir -p "$B/fetch"
for e in ftp:FTP http:HTTP; do
	{ echo "static struct fetcherr ${e%%:*}_errlist[] = {"
	  grep -v '^#' "lib/libfetch/${e%%:*}.errors" | sort | while read -r NUM CAT STRING; do
		echo "    { ${NUM}, FETCH_${CAT}, \"${STRING}\" },"; done
	  echo "    { -1, FETCH_UNKNOWN, \"Unknown ${e#*:} error\" }"; echo "};"; } > "$B/fetch/${e%%:*}err.h"
done
prog usr/bin/fetch "-include sys/ioctl.h -DIPPORT_MAX=65535 -DINFTIM=-1 -DINET6 -DWITH_SSL -DOPENSSL_API_COMPAT=0x10100000L -DFTP_COMBINE_CWDS -I$B/fetch -Ilib/libfetch $OSSL" \
	lib/libfetch/{fetch,common,ftp,http,file}.c usr.bin/fetch/fetch.c -- $(ossl) -lssl -lcrypto

# drill with lib/libldns (a PRIVATELIB, compiled in) from contrib/ldns and
# its FreeBSD-configured ldns/config.h; LIBADD ssl crypto. The trust anchor
# path is the Makefile's (/etc/unbound/root.key; there is no unbound here,
# so -S/-k need a key file named on the command line).
LD=contrib/ldns
prog usr/bin/drill "-DOPENSSL_API_COMPAT=0x10100000L -DLDNS_TRUST_ANCHOR_FILE=\\\"/etc/unbound/root.key\\\" -I$LD -I$LD/ldns $OSSL" \
	$LD/{buffer,dane,dname,dnssec,dnssec_sign,dnssec_verify,dnssec_zone,duration,edns,error,higher,host2str,host2wire,keys,net,packet,parse,radix,rbtree,rdata,resolver,rr,rr_functions,sha1,sha2,str2host,tsig,update,util,wire2host,zone}.c \
	$LD/compat/b64_{ntop,pton}.c $LD/drill/{drill,drill_util,error,root,work,chasetrace,dnssec,securetrace}.c -- $(ossl) -lssl -lcrypto

# --- usr.sbin ---
prog usr/sbin/daemon "-isystem $UTIL/usr/local/include" usr.sbin/daemon/daemon.c -- $(lib "$UTIL") -lutil
prog usr/sbin/wake "" usr.sbin/wake/wake.c
# setaudit: LIBADD bsm (OpenBSM, base/openbsm).
prog usr/sbin/setaudit "-isystem $BSM/usr/local/include" usr.sbin/setaudit/setaudit.c -- $(lib "$BSM") -lbsm
# manctl: SCRIPTS, installed without its .sh as bsd.prog.mk does.
install -m 0555 usr.sbin/manctl/manctl.sh "$OUT/usr/sbin/manctl"
# uefisign: LIBADD crypto. Its child runs in capability mode (cap_enter),
# a no-op here (compat/sys/capsicum.h).
prog usr/sbin/uefisign "$OSSL" usr.sbin/uefisign/{uefisign,child,pe}.c -- $(ossl) -lcrypto
# mtest: -DINET -DINET6, as MK_INET6_SUPPORT builds it; s6_addr32 is
# kernel-only in Darwin's <netinet6/in6.h>. patches/0004: promiscuous mode.
prog usr/sbin/mtest "-DINET -DINET6 -Ds6_addr32=__u6_addr.__u6_addr32" usr.sbin/mtest/mtest.c
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

# makefs: ffs, cd9660 and msdos (the Makefile's formats less zfs, MK_ZFS:
# its zfs.c builds from the boot loader's ZFS code, which NeoDarwin's
# OpenZFS port doesn't carry). contrib/mtree's spec reader and mknod's
# pack_dev as for nmtree; FreeBSD's <ufs/...> and <fs/msdosfs/...> from
# the pinned sys/ through $B/makefs, with the kernel headers msdosfs's
# structures need (sys/_lock.h, _lockmgr.h, _task.h, _callout.h; FreeBSD's
# tools/build installs the same ones to build makefs on macOS); LIBADD
# netbsd util sbuf. Darwin names struct stat's st_atim, st_mtim and st_ctim
# st_atimespec, st_mtimespec and st_ctimespec.
mkdir -p "$B/makefs/sys"; ln -s "$F/sys/ufs" "$B/makefs/ufs"; ln -s "$F/sys/fs" "$B/makefs/fs"; ln -s "$F/sys/sys/disk" "$B/makefs/sys/disk"
for h in _callout _lock _lockmgr _task; do ln -s "$F/sys/sys/$h.h" "$B/makefs/sys/"; done
MF=usr.sbin/makefs
prog usr/sbin/makefs "-Dst_atim=st_atimespec -Dst_mtim=st_mtimespec -Dst_ctim=st_ctimespec -I$F/$MF -I$B/makefs -DHAVE_STRUCT_STAT_ST_FLAGS=1 -DMAKEFS -D_WANT_MSDOSFS_INTERNALS -Isys/fs/cd9660 -Isys/fs/msdosfs -Isbin/newfs_msdos -include $C/nd_pwcache.h -Icontrib/mtree -Icontrib/mknod -Ilib/libnetbsd -Icontrib/libc-pwcache -isystem $UTIL/usr/local/include -isystem $SBUF/usr/local/include" \
	$MF/{cd9660,ffs,makefs,msdos,mtree,walk}.c $MF/cd9660/{cd9660_strings,cd9660_debug,cd9660_eltorito,cd9660_write,cd9660_conversion,iso9660_rrip}.c \
	$MF/ffs/{ffs_alloc,ffs_balloc,ffs_bswap,ffs_subr,ufs_bmap,buf,mkfs}.c sys/ufs/ffs/ffs_tables.c \
	sbin/newfs_msdos/mkfs_msdos.c $MF/msdos/{msdosfs_conv,msdosfs_denode,msdosfs_fat,msdosfs_lookup,msdosfs_vnops,msdosfs_vfsops}.c \
	contrib/mtree/{getid,misc,spec}.c contrib/mknod/pack_dev.c lib/libnetbsd/{efun,strsuftoll,util}.c "$C/nd_pwcache.c" \
	-- $(lib "$UTIL") -lutil $(lib "$SBUF") -lsbuf

# The IPv6 tools, over xnu's private <netinet6/in6_var.h> and <netinet6/nd6.h>
# (System.framework's PrivateHeaders, as network_cmds' ndp and rtadvd are
# built) and RFC 3542's socket options (__APPLE_USE_RFC_3542).
V6="-D__APPLE_USE_RFC_3542=1 -isystem $SYSROOT/System/Library/Frameworks/System.framework/Versions/B/PrivateHeaders"
prog usr/sbin/rip6query "$V6 -Iusr.sbin/route6d" usr.sbin/rip6query/rip6query.c
prog usr/sbin/mld6query "$V6 -DIPSEC -isystem $IPSEC/usr/local/include" usr.sbin/mld6query/mld6.c -- $(lib "$IPSEC") -lipsec
# ip6addrctl: without JAIL (no jails; §5).
prog usr/sbin/ip6addrctl "$V6" usr.sbin/ip6addrctl/ip6addrctl.c
# rrenumd: parser.y and lexer.l through the build machine's yacc and lex.
mkdir -p "$B/rrenumd"
(cd "$B/rrenumd" && yacc -d -o parser.c "$F/usr.sbin/rrenumd/parser.y" && ln -f parser.h y.tab.h && lex -o lexer.c "$F/usr.sbin/rrenumd/lexer.l")
prog usr/sbin/rrenumd "-include stdlib.h $V6 -DIPSEC -I$B/rrenumd -Iusr.sbin/rrenumd -isystem $IPSEC/usr/local/include" \
	usr.sbin/rrenumd/rrenumd.c "$B/rrenumd/parser.c" "$B/rrenumd/lexer.c" -- $(lib "$IPSEC") -lipsec
mkdir -p "$OUT/usr/share/man/man5"; install -m 0444 usr.sbin/rrenumd/rrenumd.conf.5 "$OUT/usr/share/man/man5/"

# certctl (LIBADD crypto) and FreeBSD's trust store, secure/caroot:
# /usr/share/certs/trusted and untrusted, as its Makefiles install them.
# certctl rehash, run here as FreeBSD's installworld runs it on DESTDIR,
# makes /etc/ssl/certs (hash links) and /etc/ssl/cert.pem, the default
# verify paths of the base's OpenSSL (OPENSSLDIR /private/etc/ssl). It is
# the target's own binary, run on the build machine with the base's
# libcrypto (it links only libSystem and libcrypto), as stage_root.sh runs
# the root's makewhatis.
# -DBOOTSTRAPPING, the Makefile's option for a libc without fdscandir(3)
# (Darwin's), scans a directory by path with scandir(3).
prog usr/sbin/certctl "-DBOOTSTRAPPING $OSSL" usr.sbin/certctl/certctl.c -- $(ossl) -lcrypto
mkdir -p "$OUT/usr/share/certs/trusted" "$OUT/usr/share/certs/untrusted"
install -m 0444 secure/caroot/trusted/*.pem "$OUT/usr/share/certs/trusted/"
install -m 0444 secure/caroot/untrusted/*.pem "$OUT/usr/share/certs/untrusted/"
DYLD_LIBRARY_PATH="$SSL/usr/lib" "$OUT/usr/sbin/certctl" -D "$OUT" rehash
mkdir -p "$OUT/private"; mv "$OUT/etc" "$OUT/private/etc"

# iasl and acpidb (usr.sbin/acpi): FreeBSD's Makefiles' SRCS, from ACPICA
# 20260408 (@acpica, third_party/acpica), the release the kernel builds and
# the one FreeBSD's sys/contrib/dev/acpica holds at the pin. The release's
# source/ is FreeBSD's sys/contrib/dev/acpica, linked at that path for
# acpidb.c's <contrib/dev/acpica/include/...> includes (Makefile.inc's
# -I${SRCTOP}/sys). iasl's parsers go through m4 and the build machine's
# yacc and lex, as its Makefile runs them.
find_tree() {
	local d; for d in "$(dirname "$F0")"/*/; do [ -f "$d$1" ] && { printf '%s' "${d%/}"; return 0; }; done
	echo "freebsd_cmds: no repository next to $F0 holds $1 (pass @acpica//:tools_srcs)" >&2; return 1
}
# acpica_patches/0001 is FreeBSD's own change to its copy (acpidb defines
# both ACPI_DB_APP and ACPI_EXEC_APP, and has no acpiexec init file).
AC="$(stage_src "$(find_tree source/compiler/aslmain.c)/source" "$B/acpica" "$PROJ/acpica_patches")"
mkdir -p "$B/acpi/contrib/dev" "$B/iasl"; ln -s "$AC" "$B/acpi/contrib/dev/acpica"
ACDIRS=("$AC" "$AC/common" "$AC/compiler" "$AC/os_specific/service_layers")
for d in "$AC"/components/*/; do ACDIRS+=("${d%/}"); done
# acpi_srcs MAKEFILE: its SRCS' .c files, found in its directory, among the
# generated ones in $B/iasl, or on Makefile.inc's .PATH.
acpi_srcs() {
	local f d
	for f in $(awk '/^SRCS[+]?=/ { f = 1 } f { for (i = 1; i <= NF; i++) if ($i ~ /[.]c$/) print $i
		if ($NF != "\\") f = 0 }' "$1"); do
		for d in "$(dirname "$1")" "$B/iasl" "${ACDIRS[@]}"; do [ -f "$d/$f" ] && { printf '%s\n' "$d/$f"; continue 2; }; done
		echo "freebsd_cmds: $1 names $f, which isn't in ACPICA" >&2; return 1
	done
}
(cd "$B/iasl" &&
	m4 -P -I"$AC/compiler" "$AC/compiler/aslparser.y" > aslcompiler.y &&
	yacc -d -pAslCompiler -oaslcompilerparse.c aslcompiler.y && ln -f aslcompilerparse.h aslcompiler.y.h &&
	lex -i -s -PAslCompiler -oaslcompilerlex.c "$AC/compiler/aslcompiler.l" &&
	for p in DtCompilerParser:dtcompilerparser DtParser:dtparser PrParser:prparser; do
		yacc -d -p"${p%%:*}" -o"${p#*:}parse.c" "$AC/compiler/${p#*:}.y" && ln -f "${p#*:}parse.h" "${p#*:}.y.h"
	done &&
	lex -i -PDtCompilerParser -odtcompilerparserlex.c "$AC/compiler/dtcompilerparser.l" &&
	lex -i -PDtParser -odtparserlex.c "$AC/compiler/dtparser.l" &&
	lex -i -s -PPrParser -oprparserlex.c "$AC/compiler/prparser.l")
ACF="-I$B/acpi -I$AC/include -fno-strict-aliasing"
acpi_srcs usr.sbin/acpi/iasl/Makefile > "$B/iasl.srcs"; acpi_srcs usr.sbin/acpi/acpidb/Makefile > "$B/acpidb.srcs"
prog usr/sbin/iasl "$ACF -DACPI_ASL_COMPILER -I$AC/compiler -I$B/iasl" $(cat "$B/iasl.srcs")
# acpidb's dump tables (ACPI_EXDUMP_INFO, ACPI_RSDUMP_INFO) are packed
# structures holding pointers, which chained fixups can't bind: it is
# linked with the classic rebase and bind opcodes instead.
prog usr/sbin/acpidb "$ACF -DACPI_DB_APP -DACPI_EXEC_APP" $(cat "$B/acpidb.srcs") -- -no_fixup_chains

# nmtree's pages (MAN=mtree.5 mtree.8, MLINKS mtree.8 nmtree.8), installed
# here since man_pages would take mtree.5 for the program's. man_pages
# installs the other programs' pages.
mkdir -p "$OUT/usr/share/man/man5" "$OUT/usr/share/man/man8"
install -m 0444 usr.sbin/nmtree/mtree.5 "$OUT/usr/share/man/man5/"
install -m 0444 contrib/mtree/mtree.8 "$OUT/usr/share/man/man8/"
install -m 0444 contrib/mtree/mtree.8 "$OUT/usr/share/man/man8/nmtree.8"
