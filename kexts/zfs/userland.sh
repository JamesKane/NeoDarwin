#!/usr/bin/env bash
# SPDX-License-Identifier: BSD-2-Clause
# The ZFS commands for NeoDarwin (P3-01; the libraries as packages, zed and
# the rest of the userland are P3-02), at FreeBSD's paths (cddl/sbin,
# cddl/usr.bin, cddl/usr.sbin):
#   /sbin/zpool, /sbin/zfs                       libzfs
#   /usr/bin/zinject                             libzfs
#   /usr/bin/zstream (and zstreamdump, a link)   libzfs and libzpool
#   /usr/sbin/zdb, /usr/sbin/zhack               libzpool
#   userland.sh OUT ZFS_SRC SYSROOT DEPROOT...
#   (DEPROOT: //base:root, //base:libcrypto_dylib, //base:libz_dylib,
#   //kexts/zfs:zfs_libs)
# The libraries are //kexts/zfs:zfs_libs' static archives (libs.sh), the
# flags user.sh's (libzpool's users with LIBZPOOL_CPPFLAGS, as
# cmd/zdb/Makefile.am and cmd/Makefile.am build them), the OS layer
# lib/*/os/neodarwin (see kext.sh, kexts/zfs/patches). Not built: zgenhostid
# (Linux-only in cmd/Makefile.am; the hostid on Darwin is kern.hostid, not
# /etc/hostid), ztest and raidz_test (stress tools), zed (P3-02).
# libcrypto is the base's OpenSSL 3.5 (key derivation for encryption); zlib
# is the base's /usr/lib/libz.1.dylib (Apple's zlib, //base:libz_dylib).
source "$(dirname "$0")/common.sh"
source "$PROJ/../../base/commands.sh"
source "$PROJ/user.sh"
OUT="$(abspath "$1")"; Z="$(abspath "$2")"; SYSROOT="$(abspath "$3")"; shift 3
DEPS=(); for d in "$@"; do DEPS+=("$(abspath "$d")"); done
ROOT="$(find_root "${DEPS[@]}")"
CRYPTO="$(find_dep usr/lib/libcrypto.3.dylib "${DEPS[@]}")"
ZLIB="$(find_dep usr/lib/libz.1.dylib "${DEPS[@]}")"
LIBS="$(find_dep usr/local/lib/nd_zfs/libzpool.a "${DEPS[@]}")/usr/local/lib/nd_zfs"
B="$(mktemp -d)"; trap 'rm -rf "$B"' EXIT
S="$(prepare_tree "$Z" "$B/src")"
cd "$S"

user_cflags "$B/cflags" "$B" "$SYSROOT" "$CRYPTO" "$ZLIB"
zpool_cflags "$B/zpool.rsp" "$B/cflags"
LINK=("$LIBS/libzfs.a" "$ZLIB/usr/lib/libz.1.dylib" "$CRYPTO/usr/lib/libcrypto.3.dylib")
ZLINK=("$LIBS/libzpool.a" "${LINK[@]}")

tool "$B" "$ROOT" "$OUT/sbin/zpool" "$B/cflags" cmd/zpool/zpool_iter.c cmd/zpool/zpool_main.c \
	cmd/zpool/zpool_util.c cmd/zpool/zpool_vdev.c cmd/zpool/os/neodarwin/zpool_vdev_os.c -- "${LINK[@]}"
tool "$B" "$ROOT" "$OUT/sbin/zfs" "$B/cflags" cmd/zfs/zfs_iter.c cmd/zfs/zfs_main.c cmd/zfs/zfs_project.c \
	-- "${LINK[@]}"
tool "$B" "$ROOT" "$OUT/usr/bin/zinject" "$B/cflags" cmd/zinject/translate.c cmd/zinject/zinject.c -- "${LINK[@]}"
tool "$B" "$ROOT" "$OUT/usr/bin/zstream" "$B/zpool.rsp" cmd/zstream/zstream.c cmd/zstream/zstream_decompress.c \
	cmd/zstream/zstream_dump.c cmd/zstream/zstream_recompress.c cmd/zstream/zstream_redup.c \
	cmd/zstream/zstream_token.c -- "${ZLINK[@]}"
ln -s zstream "$OUT/usr/bin/zstreamdump"
tool "$B" "$ROOT" "$OUT/usr/sbin/zdb" "$B/zpool.rsp" cmd/zdb/zdb.c cmd/zdb/zdb_il.c -- "${ZLINK[@]}"
tool "$B" "$ROOT" "$OUT/usr/sbin/zhack" "$B/zpool.rsp" cmd/zhack.c -- "${ZLINK[@]}"
# zpool create -o compatibility=: the feature sets of cmd/zpool/compatibility.d,
# at ZPOOL_COMPAT_DIR (PKGDATADIR/compatibility.d), as upstream installs them.
mkdir -p "$OUT/usr/share/zfs"
cp -R cmd/zpool/compatibility.d "$OUT/usr/share/zfs/"
rm -f "$OUT/usr/share/zfs/compatibility.d/Makefile.am"
