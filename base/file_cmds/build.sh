#!/usr/bin/env bash
# SPDX-License-Identifier: BSD-2-Clause
# The first session's file commands from file_cmds-475 (docs/base/libsystem.md):
# replays file_cmds.xcodeproj's ls, cp, mv, rm, mkdir, ln, chmod and df targets
# (project settings: __FBSDID=__RCSID, _DARWIN_USE_64_BIT_INODE,
# DEAD_CODE_STRIPPING; each target installs in /bin).
#   build.sh OUT FILE_CMDS_SRC SYSROOT DEPROOT...   (DEPROOT: //base:root, //base:libutil, //base:libxo,
#                                                    //base:libmd_dylib, //base:libz_dylib,
#                                                    //base:bzip2_commands, //base:xz_commands)
# OUT receives bin/{ls,cp,mv,rm,mkdir,ln,chmod,df,dd,rmdir},
# usr/bin/{du,touch,stat,readlink,truncate,cksum,sum,mkfifo,chgrp,compress,
# uncompress} and usr/sbin/chown; and for P4-21, bin/{chflags,pax},
# sbin/mknod and usr/bin/{ipcrm,ipcs,pathchk,install}. Not mtree, whose
# target needs CoreFoundation, CommonCrypto and APFS's private
# <apfs/apfs_fsctl.h>, all closed. P4-21's second checkpoint adds gzip
# (usr/bin/gzip with install_scripts.sh's links gunzip, gzcat, zcat, zcmp
# and zless, and its gzexe, zdiff, zforce, zmore and znew scripts), linking
# the base's libz, libbz2 and liblzma.
# ls links libutil for humanize_number(3). Apple builds it with COLORLS,
# which links libcurses for termcap; NeoDarwin has no ncurses yet, so ls
# builds without colour (ls -G is accepted and ignored).
source "$(dirname "$0")/../../tools/base/common.sh"
source "$(dirname "$0")/../commands.sh"
OUT="$(abspath "$1")"; F="$(abspath "$2")"; SYSROOT="$(abspath "$3")"; shift 3
DEPS=(); for d in "$@"; do DEPS+=("$(abspath "$d")"); done
ROOT="$(find_root "${DEPS[@]}")"
UTIL=""; for d in "${DEPS[@]}"; do [ -f "$d/usr/lib/libutil.dylib" ] && UTIL="$d"; done
[ -n "$UTIL" ] || { echo "file_cmds: no DEPROOT holds usr/lib/libutil.dylib (pass //base:libutil)" >&2; exit 1; }
MD=""; for d in "${DEPS[@]}"; do [ -f "$d/usr/lib/libmd.dylib" ] && MD="$d"; done
[ -n "$MD" ] || { echo "file_cmds: no DEPROOT holds usr/lib/libmd.dylib (pass //base:libmd_dylib)" >&2; exit 1; }
ZL=""; BZ=""; LZ=""
for d in "${DEPS[@]}"; do
	[ -f "$d/usr/lib/libz.1.dylib" ] && ZL="$d"; [ -f "$d/usr/lib/libbz2.1.0.dylib" ] && BZ="$d"
	[ -f "$d/usr/lib/liblzma.5.dylib" ] && LZ="$d"
done
[ -n "$ZL" ] && [ -n "$BZ" ] && [ -n "$LZ" ] ||
	{ echo "file_cmds: gzip needs //base:libz_dylib, //base:bzip2_commands and //base:xz_commands" >&2; exit 1; }
XO=""; for d in "${DEPS[@]}"; do [ -f "$d/usr/lib/libxo.dylib" ] && XO="$d"; done
[ -n "$XO" ] || { echo "file_cmds: no DEPROOT holds usr/lib/libxo.dylib (pass //base:libxo)" >&2; exit 1; }
PROJ="$(cd "$(dirname "$0")" && pwd)"
B="$(mktemp -d)"; trap 'rm -rf "$B"' EXIT
F="$(stage_src "$F" "$B/src" "$PROJ/patches")"   # patches/ applied
cd "$F"
D="$B/derived"; mkdir -p "$D"

# The project's Release settings; WARNING_CFLAGS (-Wall -Werror ...) change
# no interface and are left out. VERSIONING_SYSTEM = apple-generic, 475.
base=("${TARGET_FLAGS[@]}" -Os -fno-common -D__FBSDID=__RCSID -D_DARWIN_USE_64_BIT_INODE)
write_rsp "$B/cflags" "${base[@]}" $(cmd_sysroot_flags "$SYSROOT")
vers() { write_vers "$D/${1}_vers.c" "$1" file_cmds 475; printf '%s' "$D/${1}_vers.c"; }

# ls: libutil's private header from its install tree; libutil linked from
# there (-L resolves under -syslibroot's root first, then as given).
write_rsp "$B/ls.rsp" "${base[@]}" -I"$UTIL/usr/local/include" $(cmd_sysroot_flags "$SYSROOT")
tool "$B" "$ROOT" "$OUT/bin/ls" "$B/ls.rsp" ls/cmp.c ls/ls.c ls/print.c ls/util.c "$(vers ls)" \
	-- -L"$UTIL/usr/lib" -lutil
for t in cp:cp/utils.c,cp/cp.c mv:mv/mv.c rm:rm/rm.c mkdir:mkdir/mkdir.c ln:ln/ln.c chmod:chmod/chmod_acl.c,chmod/chmod.c; do
	IFS=, read -r -a srcs <<< "${t#*:}"
	tool "$B" "$ROOT" "$OUT/bin/${t%%:*}" "$B/cflags" "${srcs[@]}" "$(vers "${t%%:*}")"
done

# df (P1-10: the disk root's device and space): libutil for
# humanize_number(3), libxo for its output, and a compat get_compat.h
# (Libc's private header; UNIX2003 mode, as macOS by default).
write_rsp "$B/df.rsp" "${base[@]}" -I"$UTIL/usr/local/include" -I"$XO/usr/local/include" -I"$PROJ/compat" \
	$(cmd_sysroot_flags "$SYSROOT")
tool "$B" "$ROOT" "$OUT/bin/df" "$B/df.rsp" df/df.c "$(vers df)" -- -L"$UTIL/usr/lib" -lutil -L"$XO/usr/lib" -lxo

# The ZFS test suite's commands (docs/architecture/filesystems.md §7), as
# their targets build them: INSTALL_PATH /usr/bin unless the target says
# /bin (dd, rmdir) or /usr/sbin (chown). dd, du and truncate link libutil
# (expand_number(3), humanize_number(3)); dd's entitlements are for
# platforms other than macOS. du, mkfifo and chown include get_compat.h.
write_rsp "$B/util.rsp" "${base[@]}" -I"$UTIL/usr/local/include" -I"$PROJ/compat" $(cmd_sysroot_flags "$SYSROOT")
tool "$B" "$ROOT" "$OUT/bin/dd" "$B/util.rsp" dd/args.c dd/conv.c dd/conv_tab.c dd/dd.c dd/misc.c dd/position.c \
	"$(vers dd)" -- -L"$UTIL/usr/lib" -lutil
tool "$B" "$ROOT" "$OUT/usr/bin/du" "$B/util.rsp" du/du.c "$(vers du)" -- -L"$UTIL/usr/lib" -lutil
tool "$B" "$ROOT" "$OUT/usr/bin/truncate" "$B/util.rsp" truncate/truncate.c "$(vers truncate)" -- -L"$UTIL/usr/lib" -lutil
tool "$B" "$ROOT" "$OUT/bin/rmdir" "$B/cflags" rmdir/rmdir.c "$(vers rmdir)"
tool "$B" "$ROOT" "$OUT/usr/bin/touch" "$B/cflags" touch/touch.c "$(vers touch)"
tool "$B" "$ROOT" "$OUT/usr/bin/mkfifo" "$B/util.rsp" mkfifo/mkfifo.c "$(vers mkfifo)"
tool "$B" "$ROOT" "$OUT/usr/sbin/chown" "$B/util.rsp" chown/chown.c "$(vers chown)"
# stat: its target adds HAVE_CONFIG_H=0.
{ cat "$B/cflags"; printf '%s\n' -DHAVE_CONFIG_H=0; } > "$B/stat.rsp"
tool "$B" "$ROOT" "$OUT/usr/bin/stat" "$B/stat.rsp" stat/stat.c "$(vers stat)"
tool "$B" "$ROOT" "$OUT/usr/bin/cksum" "$B/cflags" cksum/cksum.c cksum/crc.c cksum/crc32.c cksum/print.c \
	cksum/sum1.c cksum/sum2.c "$(vers cksum)"
tool "$B" "$ROOT" "$OUT/usr/bin/compress" "$B/cflags" compress/compress.c compress/zopen.c "$(vers compress)"
# The readlink, sum, chgrp and uncompress targets' hardlink.sh phases
# (copies here): each command tells by its name which it is.
cp "$OUT/usr/bin/stat" "$OUT/usr/bin/readlink"; cp "$OUT/usr/bin/cksum" "$OUT/usr/bin/sum"
cp "$OUT/usr/sbin/chown" "$OUT/usr/bin/chgrp"; cp "$OUT/usr/bin/compress" "$OUT/usr/bin/uncompress"

# P4-21's targets. chflags, ipcrm and pathchk: one source each, /usr/bin
# unless the target says /bin. pax: INSTALL_PATH /bin. mknod: /sbin, with
# OTHER_CFLAGS -DHAVE_NBTOOL_CONFIG_H=0.
tool "$B" "$ROOT" "$OUT/bin/chflags" "$B/cflags" chflags/chflags.c "$(vers chflags)"
tool "$B" "$ROOT" "$OUT/usr/bin/ipcrm" "$B/cflags" ipcrm/ipcrm.c "$(vers ipcrm)"
tool "$B" "$ROOT" "$OUT/usr/bin/pathchk" "$B/cflags" pathchk/pathchk.c "$(vers pathchk)"
tool "$B" "$ROOT" "$OUT/bin/pax" "$B/cflags" pax/ar_io.c pax/ar_subs.c pax/buf_subs.c pax/cache.c pax/cpio.c \
	pax/file_subs.c pax/ftree.c pax/gen_subs.c pax/getoldopt.c pax/options.c pax/pat_rep.c pax/pax.c \
	pax/pax_format.c pax/sel_subs.c pax/tables.c pax/tar.c pax/tty_subs.c "$(vers pax)"
{ cat "$B/cflags"; printf '%s\n' -DHAVE_NBTOOL_CONFIG_H=0; } > "$B/mknod.rsp"
tool "$B" "$ROOT" "$OUT/sbin/mknod" "$B/mknod.rsp" mknod/pack_dev.c mknod/mknod.c "$(vers mknod)"
# ipcs: OTHER_CFLAGS -iquote the SDK's Kernel.framework and
# System.framework PrivateHeaders (xnu's sysv IPC structures).
{ cat "$B/cflags"; printf '%s\n' -iquote "$SYSROOT/System/Library/Frameworks/Kernel.framework/PrivateHeaders" \
	-iquote "$SYSROOT/System/Library/Frameworks/System.framework/PrivateHeaders"; } > "$B/ipcs.rsp"
tool "$B" "$ROOT" "$OUT/usr/bin/ipcs" "$B/ipcs.rsp" ipcs/ipcs.c "$(vers ipcs)"
# install (xinstall.c): libmd.tbd in its Frameworks phase (base/libmd,
# FreeBSD's; its headers want libmd_cdefs.h first).
write_rsp "$B/install.rsp" "${base[@]}" -I"$MD/usr/local/include" -include libmd_cdefs.h -iquote mtree \
	$(cmd_sysroot_flags "$SYSROOT")
tool "$B" "$ROOT" "$OUT/usr/bin/install" "$B/install.rsp" install/xinstall.c "$(vers install)" -- -L"$MD/usr/lib" -lmd
# gzip: gzip.xcconfig (GZIP_PREFIX /usr on macOS; its
# GCC_PREPROCESSOR_DEFINITIONS, GZIP_APPLE_VERSION alone, replace the
# project's) and its Frameworks phase's libbz2, liblzma and libz;
# install_scripts.sh installs GZIP_SCRIPTS and makes GZIP_LINKS' hard links
# (copies here).
write_rsp "$B/gzip.rsp" "${TARGET_FLAGS[@]}" -Os -fno-common '-DGZIP_APPLE_VERSION=\"475\"' -I"$ZL/usr/local/include" \
	-I"$BZ/usr/local/include" -I"$LZ/usr/local/include" $(cmd_sysroot_flags "$SYSROOT")
tool "$B" "$ROOT" "$OUT/usr/bin/gzip" "$B/gzip.rsp" gzip/futimens.c gzip/gzip.c "$(vers gzip)" \
	-- -L"$BZ/usr/lib" -lbz2 -L"$LZ/usr/lib" -llzma -L"$ZL/usr/lib" -lz
for s in gzexe zdiff zforce zmore znew; do install -m 0755 "gzip/$s" "$OUT/usr/bin/$s"; done
set -- gzip gunzip gzip gzcat gzip zcat zdiff zcmp zmore zless
while [ $# -ge 2 ]; do cp "$OUT/usr/bin/$1" "$OUT/usr/bin/$2"; shift 2; done

# xattr (P4-21 checkpoint 6, docs/architecture/freebsd-parity.md §2.1): the
# C xattr of file_cmds-475, one source with the project's flags; the
# target sets no INSTALL_PATH, and macOS installs it as /usr/bin/xattr.
# FreeBSD's runat and extattr rows are its equivalents.
tool "$B" "$ROOT" "$OUT/usr/bin/xattr" "$B/cflags" xattr/xattr.c "$(vers xattr)"
