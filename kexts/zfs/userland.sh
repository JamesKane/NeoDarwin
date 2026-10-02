#!/usr/bin/env bash
# SPDX-License-Identifier: BSD-2-Clause
# zpool and zfs for NeoDarwin (P3-01; the libraries as packages, zed and the
# rest of the userland are P3-02).
#   userland.sh OUT ZFS_SRC SYSROOT DEPROOT...   (DEPROOT: //base:root, //base:libcrypto_dylib)
# OUT receives sbin/zpool and sbin/zfs, where FreeBSD installs them.
#
# The libraries are those of lib/*/Makefile.am for macOS (libspl, libavl,
# libnvpair, libzfs_core, libzutil, libefi, libzfs with zcommon), linked
# into each command statically; the OS layer is lib/*/os/neodarwin (see
# kext.sh, kexts/zfs/patches). Flags as the Makefiles' (AM_CPPFLAGS: the
# library include roots, _GNU_SOURCE, _FILE_OFFSET_BITS=64), with FreeBSD's
# paths (/sbin, /etc, /var/run) and no NLS. NeoDarwin supplies what the
# macOS build takes from frameworks and Homebrew (kexts/zfs/compat):
# libintl.h (identity), libdiskmgt (in use = mounted) and zlib's crc32().
# libcrypto is the base's OpenSSL 3.5 (key derivation for encryption).
source "$(dirname "$0")/common.sh"
source "$PROJ/../../base/commands.sh"
OUT="$(abspath "$1")"; Z="$(abspath "$2")"; SYSROOT="$(abspath "$3")"; shift 3
DEPS=(); for d in "$@"; do DEPS+=("$(abspath "$d")"); done
ROOT="$(find_root "${DEPS[@]}")"
CRYPTO=""; for d in "${DEPS[@]}"; do [ -f "$d/usr/lib/libcrypto.3.dylib" ] && CRYPTO="$d"; done
[ -n "$CRYPTO" ] || { echo "userland.sh: no DEPROOT holds usr/lib/libcrypto.3.dylib (pass //base:libcrypto_dylib)" >&2; exit 1; }
B="$(mktemp -d)"; trap 'rm -rf "$B"' EXIT
S="$(prepare_tree "$Z" "$B/src")"
cd "$S"

write_rsp "$B/cflags" "${TARGET_FLAGS[@]}" -mcpu=cortex-a76 -O2 -std=gnu99 -fno-strict-aliasing -fno-common \
	-D__NEODARWIN__=1 -include "$PROJ/include/zfs_config.h" \
	-Iinclude -Ilib/libspl/include -Ilib/libspl/include/os/neodarwin -Ilib/libzpool/include \
	-Ilib/libzfs -Ilib/libzfs_core -Ilib/libzutil -Icmd/zpool -Icmd/zfs -I"$PROJ/include" \
	-D_GNU_SOURCE -D_REENTRANT -D_FILE_OFFSET_BITS=64 -D_LARGEFILE64_SOURCE \
	'-DLIBEXECDIR=\"/usr/libexec\"' '-DZFSEXECDIR=\"/usr/libexec/zfs\"' '-DRUNSTATEDIR=\"/var/run\"' \
	'-DSBINDIR=\"/sbin\"' '-DSYSCONFDIR=\"/etc\"' '-DPKGDATADIR=\"/usr/share/zfs\"' \
	-UDEBUG -DNDEBUG '-DTEXT_DOMAIN=\"zfs-neodarwin-user\"' \
	-I"$PROJ/compat" -I"$CRYPTO/usr/local/openssl/include" $(cmd_sysroot_flags "$SYSROOT") \
	-ffile-prefix-map="$B/"= -ffile-prefix-map="$PROJ/"=kexts/zfs/

LIBS=(
	lib/libspl/assert.c lib/libspl/atomic.c lib/libspl/backtrace.c lib/libspl/condvar.c
	lib/libspl/cred.c lib/libspl/getexecname.c lib/libspl/kmem.c lib/libspl/kstat.c
	lib/libspl/libspl.c lib/libspl/list.c lib/libspl/mkdirp.c lib/libspl/mutex.c lib/libspl/page.c
	lib/libspl/procfs_list.c lib/libspl/random.c lib/libspl/rwlock.c lib/libspl/sid.c
	lib/libspl/strlcat.c lib/libspl/strlcpy.c lib/libspl/taskq.c lib/libspl/thread.c
	lib/libspl/timestamp.c lib/libspl/tunables.c
	lib/libspl/os/neodarwin/getexecname.c lib/libspl/os/neodarwin/gethostid.c lib/libspl/os/neodarwin/zone.c
	module/avl/avl.c
	lib/libnvpair/libnvpair.c lib/libnvpair/libnvpair_json.c lib/libnvpair/nvpair_alloc_system.c
	module/nvpair/nvpair_alloc_fixed.c module/nvpair/nvpair.c module/nvpair/fnvpair.c
	lib/libzfs_core/libzfs_core.c lib/libzfs_core/os/neodarwin/libzfs_core_ioctl.c
	lib/libzutil/zutil_device_path.c lib/libzutil/zutil_import.c lib/libzutil/zutil_nicenum.c
	lib/libzutil/zutil_pool.c lib/libzutil/os/neodarwin/zutil_device_path_os.c
	lib/libzutil/os/neodarwin/zutil_import_os.c
	lib/libefi/rdwr_efi_macos.c
	lib/libzfs/libzfs_changelist.c lib/libzfs/libzfs_config.c lib/libzfs/libzfs_crypto.c
	lib/libzfs/libzfs_dataset.c lib/libzfs/libzfs_diff.c lib/libzfs/libzfs_import.c
	lib/libzfs/libzfs_iter.c lib/libzfs/libzfs_mount.c lib/libzfs/libzfs_pool.c
	lib/libzfs/libzfs_share.c lib/libzfs/libzfs_share_nfs.c lib/libzfs/libzfs_sendrecv.c
	lib/libzfs/libzfs_status.c lib/libzfs/libzfs_util.c
	lib/libzfs/os/neodarwin/libzfs_dataset_os.c lib/libzfs/os/neodarwin/libzfs_getmntany.c
	lib/libzfs/os/neodarwin/libzfs_mount_os.c lib/libzfs/os/neodarwin/libzfs_pool_os.c
	lib/libzfs/os/neodarwin/libzfs_share_nfs.c lib/libzfs/os/neodarwin/libzfs_share_smb.c
	lib/libzfs/os/neodarwin/libzfs_util_os.c
	module/zcommon/cityhash.c module/zcommon/zfeature_common.c module/zcommon/zfs_comutil.c
	module/zcommon/zfs_deleg.c module/zcommon/zfs_fletcher.c module/zcommon/zfs_fletcher_superscalar.c
	module/zcommon/zfs_fletcher_superscalar4.c module/zcommon/zfs_namecheck.c module/zcommon/zfs_prop.c
	module/zcommon/zfs_valstr.c module/zcommon/zpool_prop.c module/zcommon/zprop_common.c
	"$PROJ/compat/nd_libdiskmgt.c" "$PROJ/compat/nd_zlib.c"
)
compile "$B/lib" "$B/cflags" "${LIBS[@]}"

LINK=("$B"/lib/*.o "$CRYPTO/usr/lib/libcrypto.3.dylib")
tool "$B" "$ROOT" "$OUT/sbin/zpool" "$B/cflags" cmd/zpool/zpool_iter.c cmd/zpool/zpool_main.c \
	cmd/zpool/zpool_util.c cmd/zpool/zpool_vdev.c cmd/zpool/os/neodarwin/zpool_vdev_os.c -- "${LINK[@]}"
tool "$B" "$ROOT" "$OUT/sbin/zfs" "$B/cflags" cmd/zfs/zfs_iter.c cmd/zfs/zfs_main.c cmd/zfs/zfs_project.c \
	-- "${LINK[@]}"
