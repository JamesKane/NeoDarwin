/* SPDX-License-Identifier: CDDL-1.0 */
/* NeoDarwin-Language: portability: the C header OpenZFS's configure generates, which every C source includes first. */
/*
 * zfs_config.h for NeoDarwin (P3-01), the kext's and the userland's: what
 * OpenZFS's configure (zfs-macOS-2.4.1p1, --with-config=all) finds on macOS
 * against NeoDarwin's Kernel.framework, less what NeoDarwin's userland lacks.
 * Its two CoreFoundation answers are gone (no CoreFoundation in the base),
 * and LIBFETCH_* are off: libzfs doesn't dlopen() libcurl for keylocation=
 * https:// (the base has no libcurl). HAVE_ZLIB stays: zlib is the
 * base's /usr/lib/libz.1.dylib (Apple's zlib-100.120.1, //base:libz_dylib).
 * Every Linux kernel probe is undefined, as on macOS.
 */
#define	HAVE_BACKTRACE 1
#define	HAVE_DLFCN_H 1
#define	HAVE_FORMAT_OVERFLOW 1
#define	HAVE_IMPLICIT_FALLTHROUGH 1
#define	HAVE_INFINITE_RECURSION 1
#define	HAVE_INTTYPES_H 1
#define	HAVE_ISSETUGID 1
#define	HAVE_KERNEL_INFINITE_RECURSION 1
#define	HAVE_LIBCRYPTO 1
#define	HAVE_MLOCKALL 1
#define	HAVE_STDINT_H 1
#define	HAVE_STDIO_H 1
#define	HAVE_STDLIB_H 1
#define	HAVE_STRINGS_H 1
#define	HAVE_STRING_H 1
#define	HAVE_STRLCAT 1
#define	HAVE_STRLCPY 1
#define	HAVE_SYS_STAT_H 1
#define	HAVE_SYS_TYPES_H 1
#define	HAVE_UNISTD_H 1
#define	HAVE_ZLIB 1
#define	KERNEL_MODPREFIX "/System/Library/Extensions"
#define	LIBFETCH_DYNAMIC 0
#define	LIBFETCH_IS_FETCH 0
#define	LIBFETCH_IS_LIBCURL 0
#define	SIZEOF_OFF_T 8
#define	SPL_META_ALIAS ZFS_META_ALIAS
#define	SPL_META_RELEASE ZFS_META_RELEASE
#define	SPL_META_VERSION ZFS_META_VERSION
#define	SYSTEM_MACOS 1
#define	ZFS_META_ALIAS "zfs-2.4.1-p1"
#define	ZFS_META_AUTHOR "OpenZFS"
#define	ZFS_META_KVER_MAX "6.18"
#define	ZFS_META_KVER_MIN "4.18"
#define	ZFS_META_LICENSE "CDDL"
#define	ZFS_META_NAME "zfs"
#define	ZFS_META_RELEASE "p1"
#define	ZFS_META_VERSION "2.4.1"
