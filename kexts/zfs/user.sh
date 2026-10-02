# SPDX-License-Identifier: BSD-2-Clause
# The ZFS userland's compiler flags and library sources, shared by
# kexts/zfs/libs.sh (the static libraries), userland.sh (the commands) and
# test_commands.sh (the test suite's helpers). Sourced after common.sh and
# base/commands.sh, with the tree prepared (prepare_tree) as the cwd.
#
# Flags as the Makefiles' (config/Rules.am AM_CPPFLAGS: the library include
# roots, _GNU_SOURCE, _FILE_OFFSET_BITS=64), with FreeBSD's paths (/sbin,
# /etc, /var/run) and no NLS. NeoDarwin supplies what the macOS build takes
# from frameworks and Homebrew: kexts/zfs/compat (libintl.h, libdiskmgt) and
# zlib (//kexts/zfs:zlib, a private static zlib until P3-02).

# user_cflags OUT_RSP B SYSROOT CRYPTO ZLIB [EXTRA...]: the userland's flags
# (NDEBUG, as libzfs and the commands build); EXTRA is appended.
user_cflags() {
	local out="$1" b="$2" sysroot="$3" crypto="$4" zlib="$5"; shift 5
	write_rsp "$out" "${TARGET_FLAGS[@]}" -mcpu=cortex-a76 -O2 -std=gnu99 -fno-strict-aliasing -fno-common \
		-D__NEODARWIN__=1 -include "$PROJ/include/zfs_config.h" \
		-Iinclude -Ilib/libspl/include -Ilib/libspl/include/os/neodarwin -Ilib/libzpool/include \
		-Ilib/libzfs -Ilib/libzfs_core -Ilib/libzutil -Icmd/zpool -Icmd/zfs -I"$PROJ/include" \
		-D_GNU_SOURCE -D_REENTRANT -D_FILE_OFFSET_BITS=64 -D_LARGEFILE64_SOURCE \
		'-DLIBEXECDIR=\"/usr/libexec\"' '-DZFSEXECDIR=\"/usr/libexec/zfs\"' '-DRUNSTATEDIR=\"/var/run\"' \
		'-DSBINDIR=\"/sbin\"' '-DSYSCONFDIR=\"/etc\"' '-DPKGDATADIR=\"/usr/share/zfs\"' \
		-UDEBUG -DNDEBUG '-DTEXT_DOMAIN=\"zfs-neodarwin-user\"' \
		-I"$PROJ/compat" -I"$zlib/usr/local/include/nd_zfs" -I"$crypto/usr/local/openssl/include" \
		$(cmd_sysroot_flags "$sysroot") \
		-ffile-prefix-map="$b/"= -ffile-prefix-map="$PROJ/"=kexts/zfs/ "$@"
}

# zpool_cflags OUT_RSP USER_RSP: libzpool's and its users' flags
# (LIBZPOOL_CPPFLAGS: asserts and ZFS_DEBUG on, which change shared
# structures, so every libzpool user compiles with them), plus the
# ICP's include root.
zpool_cflags() {
	grep -v -x -e '-DNDEBUG' -e '-UDEBUG' "$2" > "$1"
	printf '%s\n' -DDEBUG -UNDEBUG -DZFS_DEBUG -Imodule/icp/include >> "$1"
}

# libspl, libavl, libnvpair, libzfs_core, libzutil, libefi, libzfs with
# zcommon (lib/*/Makefile.am for macOS), and compat's libdiskmgt.
ZFS_LIB_SRCS=(
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
	"$PROJ/compat/nd_libdiskmgt.c"
)

# libzpool (lib/libzpool/Makefile.am for macOS): ZFS itself in userland over
# libspl's kernel emulation, with the ICP (lib/libicp: C, and the AES,
# GHASH and SHA-2 Armv8 assembly, each selected at run time from the CPU's
# features; BLAKE3's is unused on Apple arm64, as in the kext) and zstd
# (lib/libzstd). Fletcher-4
# and RAID-Z build their scalar paths only, as the kext does; the NEON
# sources compile to nothing without their HAVE_ macros. zdb's libzdb too.
ZPOOL_SRCS=(
	lib/libzpool/abd_os.c lib/libzpool/arc_os.c lib/libzpool/kernel.c lib/libzpool/util.c
	lib/libzpool/vdev_label_os.c lib/libzpool/zfs_racct.c lib/libzpool/zfs_debug.c
	module/os/linux/zfs/zio_crypt.c module/os/neodarwin/zfs/vdev_file_os.c
	module/zcommon/simd_stat.c
	lib/libzdb/libzdb.c
)
for f in lapi lauxlib lbaselib lcode lcompat lcorolib lctype ldebug ldo lfunc lgc llex lmem lobject lopcodes \
	lparser lstate lstring lstrlib ltable ltablib ltm lvm lzio; do ZPOOL_SRCS+=("module/lua/$f.c"); done
for f in abd aggsum arc blake3_zfs blkptr bplist bpobj bptree bqueue btree brt dbuf dbuf_stats ddt ddt_log \
	ddt_stats ddt_zap dmu dmu_diff dmu_direct dmu_object dmu_objset dmu_recv dmu_redact dmu_send dmu_traverse \
	dmu_tx dmu_zfetch dnode dnode_sync dsl_bookmark dsl_crypt dsl_dataset dsl_deadlist dsl_deleg dsl_destroy \
	dsl_dir dsl_pool dsl_prop dsl_scan dsl_synctask dsl_userhold edonr_zfs fm gzip hkdf lz4 lz4_zfs lzjb \
	metaslab mmp multilist objlist pathname range_tree refcount rrwlock sa sha2_zfs skein_zfs spa \
	spa_checkpoint spa_config spa_errlog spa_history spa_log_spacemap spa_misc spa_stats space_map \
	space_reftree txg u8_textprep uberblock unique vdev vdev_draid vdev_draid_rand vdev_file vdev_indirect \
	vdev_indirect_births vdev_indirect_mapping vdev_initialize vdev_label vdev_mirror vdev_missing vdev_queue \
	vdev_raidz vdev_raidz_math vdev_raidz_math_scalar vdev_rebuild vdev_removal vdev_root vdev_trim zap \
	zap_leaf zap_micro zcp zcp_get zcp_global zcp_iter zcp_set zcp_synctask zfeature zfs_byteswap zfs_chksum \
	zfs_debug_common zfs_crrd zfs_fm zfs_fuid zfs_impl zfs_ratelimit zfs_rlock zfs_sa zfs_znode zil zio \
	zio_checksum zio_compress zio_inject zle zrlock zthr; do ZPOOL_SRCS+=("module/zfs/$f.c"); done
ICP_SRCS=(
	module/icp/spi/kcf_spi.c module/icp/api/kcf_ctxops.c module/icp/api/kcf_cipher.c module/icp/api/kcf_mac.c
	module/icp/algs/aes/aes_impl_generic.c module/icp/algs/aes/aes_impl.c module/icp/algs/aes/aes_modes.c
	module/icp/algs/blake3/blake3.c module/icp/algs/blake3/blake3_generic.c module/icp/algs/blake3/blake3_impl.c
	module/icp/algs/edonr/edonr.c module/icp/algs/modes/modes.c module/icp/algs/modes/gcm_generic.c
	module/icp/algs/modes/gcm.c module/icp/algs/modes/ccm.c module/icp/algs/sha2/sha2_generic.c
	module/icp/algs/sha2/sha256_impl.c module/icp/algs/sha2/sha512_impl.c module/icp/algs/skein/skein.c
	module/icp/algs/skein/skein_block.c module/icp/algs/skein/skein_iv.c module/icp/illumos-crypto.c
	module/icp/io/aes.c module/icp/io/sha2_mod.c module/icp/core/kcf_sched.c module/icp/core/kcf_prov_lib.c
	module/icp/core/kcf_callprov.c module/icp/core/kcf_mech_tabs.c module/icp/core/kcf_prov_tabs.c
	module/icp/algs/aes/aes_impl_aesv8.c module/icp/asm-aarch64/aes/aesv8-armx.S
	module/icp/asm-aarch64/aes/ghashv8-armx.S
	module/icp/asm-aarch64/sha2/sha256-armv8.S module/icp/asm-aarch64/sha2/sha512-armv8.S
)
ZSTD_SRCS=(module/zstd/zfs_zstd.c)
for f in common/entropy_common common/error_private common/fse_decompress common/pool common/zstd_common \
	compress/fse_compress compress/hist compress/huf_compress compress/zstd_compress_literals \
	compress/zstd_compress_sequences compress/zstd_compress_superblock compress/zstd_compress \
	compress/zstd_double_fast compress/zstd_fast compress/zstd_lazy compress/zstd_ldm compress/zstd_opt \
	compress/zstd_preSplit decompress/huf_decompress decompress/zstd_ddict decompress/zstd_decompress \
	decompress/zstd_decompress_block; do ZSTD_SRCS+=("module/zstd/lib/$f.c"); done

# find_dep FILE DEPROOT...: prints the DEPROOT holding FILE.
find_dep() {
	local f="$1" d; shift
	for d in "$@"; do [ -e "$d/$f" ] && { printf '%s' "$d"; return 0; }; done
	echo "kexts/zfs: no DEPROOT holds $f" >&2; return 1
}
