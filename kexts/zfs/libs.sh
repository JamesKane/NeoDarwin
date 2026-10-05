#!/usr/bin/env bash
# SPDX-License-Identifier: BSD-2-Clause
# The ZFS userland's static libraries (P3-01; the libraries as packages are
# P3-02), for //kexts/zfs:zfs_commands and //kexts/zfs:zfs_test_commands.
#   libs.sh OUT ZFS_SRC SYSROOT DEPROOT...
#   (DEPROOT: //base:root, //base:libcrypto_dylib, //base:libz_dylib)
# OUT receives, in build-only paths (never in an image):
#   usr/local/lib/nd_zfs/libzfs.a    libspl, libavl, libnvpair, libzfs_core,
#                                    libzutil, libefi and libzfs with zcommon
#                                    (user.sh ZFS_LIB_SRCS), as asserts off
#   usr/local/lib/nd_zfs/libzpool.a  libzpool with the ICP, zstd, zcommon
#                                    and libzdb (user.sh ZPOOL_SRCS), with
#                                    LIBZPOOL_CPPFLAGS (asserts, ZFS_DEBUG)
# A libzpool user links libzpool.a ahead of libzfs.a, so zcommon comes from
# libzpool's debug build, as a shared libzpool would provide it.
source "$(dirname "$0")/common.sh"
source "$PROJ/../../base/commands.sh"
source "$PROJ/user.sh"
OUT="$(abspath "$1")"; Z="$(abspath "$2")"; SYSROOT="$(abspath "$3")"; shift 3
DEPS=(); for d in "$@"; do DEPS+=("$(abspath "$d")"); done
CRYPTO="$(find_dep usr/lib/libcrypto.3.dylib "${DEPS[@]}")"
ZLIB="$(find_dep usr/lib/libz.1.dylib "${DEPS[@]}")"
B="$(mktemp -d)"; trap 'rm -rf "$B"' EXIT
S="$(prepare_tree "$Z" "$B/src")"
cd "$S"

user_cflags "$B/cflags" "$B" "$SYSROOT" "$CRYPTO" "$ZLIB"
zpool_cflags "$B/zpool.rsp" "$B/cflags"
printf '%s\n' -DLIB_ZPOOL_BUILD -DCLOSE_ON_UNMOUNT >> "$B/zpool.rsp"   # libzpool_la_CPPFLAGS for macOS
cp "$B/zpool.rsp" "$B/zstd.rsp"
printf '%s\n' -include module/zstd/include/zstd_compat_wrapper.h -fno-tree-vectorize -w >> "$B/zstd.rsp"

compile "$B/zfs" "$B/cflags" "${ZFS_LIB_SRCS[@]}"
ZC=(); for s in "${ZFS_LIB_SRCS[@]}"; do case "$s" in module/zcommon/*) ZC+=("$s") ;; esac; done
compile "$B/zpool" "$B/zpool.rsp" "${ZPOOL_SRCS[@]}" "${ICP_SRCS[@]}" "${ZC[@]}"
compile "$B/zpool" "$B/zstd.rsp" "${ZSTD_SRCS[@]}"

mkdir -p "$OUT/usr/local/lib/nd_zfs"
xcrun libtool -static -no_warning_for_no_symbols -o "$OUT/usr/local/lib/nd_zfs/libzfs.a" "$B"/zfs/*.o
xcrun libtool -static -no_warning_for_no_symbols -o "$OUT/usr/local/lib/nd_zfs/libzpool.a" "$B"/zpool/*.o
