#!/usr/bin/env bash
# SPDX-License-Identifier: BSD-2-Clause
# The ZFS Test Suite's helper programs (tests/zfs-tests/cmd) for NeoDarwin
# (P3-01 checkpoint 2), at usr/share/zfs/zfs-tests/bin, where
# tests/zfs-tests/cmd/Makefile.am installs them ($(datadir)/zfs/zfs-tests/bin).
#   test_commands.sh OUT ZFS_SRC SYSROOT DEPROOT...
#   (DEPROOT: //base:root, //base:libcrypto_dylib, //base:libz_dylib,
#   //kexts/zfs:zfs_libs; ZFS_SRC is @openzfs//:tests, the tree with tests/)
# Each as Makefile.am builds it, with user.sh's flags, linked against
# //kexts/zfs:zfs_libs' archives: libzfs.a (libzfs, libzfs_core, libnvpair,
# libspl...) for the libzfs users, libzpool.a ahead of it, compiled with
# LIBZPOOL_CPPFLAGS, for the libzpool users.
#
# Built: every program Makefile.am builds outside its BUILD_LINUX and
# WANT_* blocks, except file_fadvise (!BUILD_MACOS: posix_fadvise):
#   plain:   chg_usr_exec clonefile clone_mmap_cached clone_mmap_write cp_files
#            ctime dir_rd_update dosmode_readonly_write get_diff rename_dir
#            suid_write_to_file truncate_test zfs_diff-socket mkbusy mkfile
#            mkfiles mktree mmap_exec mmap_ftruncate mmap_seek mmap_sync
#            mmapwrite readmmap mmap_write_sync rm_lnkcnt_zero_file stride_dd
#            threadsappend manipulate_user_buffer file_append file_check
#            file_trunc file_write largest_file randwritecomp
#   libzfs:  badsend libzfs_input_check nvlist_to_lua send_doall ereports
#   libzpool: btree_test crypto_test draid (and zlib's gz*) sha2_test
#            skein_test edonr_test blake3_test
# Not built: the BUILD_LINUX ones (getversion: <linux/fs.h>; randfree_file:
# <linux/falloc.h>; user_ns_exec, renameat2, statx, xattrtest,
# zed_fd_spill-zedlet, idmap_util, read/write_dos_attributes: Linux
# interfaces), devname2devid and mmap_libaio (libudev, libaio), and the
# BUILD_MACOS librt shim (an empty library for test-runner.py's
# LD_PRELOAD-style lookup; clock_gettime is in libSystem).
source "$(dirname "$0")/common.sh"
source "$PROJ/../../base/commands.sh"
source "$PROJ/user.sh"
OUT="$(abspath "$1")"; Z="$(abspath "$2")"; SYSROOT="$(abspath "$3")"; shift 3
DEPS=(); for d in "$@"; do DEPS+=("$(abspath "$d")"); done
ROOT="$(find_root "${DEPS[@]}")"
CRYPTO="$(find_dep usr/lib/libcrypto.3.dylib "${DEPS[@]}")"
ZLIB="$(find_dep usr/lib/libz.1.dylib "${DEPS[@]}")"
LIBS="$(find_dep usr/local/lib/nd_zfs/libzpool.a "${DEPS[@]}")/usr/local/lib/nd_zfs"
[ -d "$Z/tests/zfs-tests/cmd" ] || { echo "test_commands.sh: no tests/zfs-tests/cmd in $Z (pass @openzfs//:tests)" >&2; exit 1; }
B="$(mktemp -d)"; trap 'rm -rf "$B"' EXIT
S="$(prepare_tree "$Z" "$B/src")"
cd "$S"

user_cflags "$B/cflags" "$B" "$SYSROOT" "$CRYPTO" "$ZLIB"
zpool_cflags "$B/zpool.rsp" "$B/cflags"
LINK=("$LIBS/libzfs.a" "$ZLIB/usr/lib/libz.1.dylib" "$CRYPTO/usr/lib/libcrypto.3.dylib")
BIN="$OUT/usr/share/zfs/zfs-tests/bin"
C=tests/zfs-tests/cmd

for n in chg_usr_exec clonefile clone_mmap_cached clone_mmap_write cp_files ctime dir_rd_update \
	dosmode_readonly_write get_diff rename_dir suid_write_to_file truncate_test zfs_diff-socket mkbusy mkfile \
	mkfiles mktree mmap_exec mmap_ftruncate mmap_seek mmap_sync mmapwrite readmmap mmap_write_sync \
	rm_lnkcnt_zero_file stride_dd threadsappend manipulate_user_buffer \
	badsend libzfs_input_check nvlist_to_lua send_doall ereports; do
	tool "$B" "$ROOT" "$BIN/$n" "$B/cflags" "$C/$n.c" -- "${LINK[@]}"
done
for n in file_append file_check file_trunc file_write largest_file randwritecomp; do
	tool "$B" "$ROOT" "$BIN/$n" "$B/cflags" "$C/file/$n.c" -- "${LINK[@]}"
done
for n in btree_test crypto_test draid checksum/sha2_test checksum/skein_test checksum/edonr_test \
	checksum/blake3_test; do
	tool "$B" "$ROOT" "$BIN/${n#checksum/}" "$B/zpool.rsp" "$C/$n.c" -- "$LIBS/libzpool.a" "${LINK[@]}"
done
