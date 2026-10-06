/* SPDX-License-Identifier: BSD-2-Clause */
// NeoDarwin-Language: portability: the configuration FreeBSD's libarchive test programs expect, for the base's libarchive.
/*
 * The libarchive test programs' configuration (usr.bin/bsdcat and
 * usr.bin/cpio's tests, base/freebsd_tests/build.sh, HAVE_CONFIG_H):
 * FreeBSD's lib/libarchive/config_freebsd.h less what xnu and libSystem
 * lack or spell differently. The tests only read it (test_utils and
 * cpio's cmdline.c); libarchive itself is the base's (Apple's
 * libarchive-158, base/libarchive).
 */
#include "config_freebsd.h"
/* FreeBSD's ACLs and extattr(2); macOS has neither interface. */
#undef ARCHIVE_ACL_FREEBSD
#undef ARCHIVE_ACL_FREEBSD_NFS4
#undef ARCHIVE_XATTR_FREEBSD
#undef HAVE_SYS_ACL_H
#undef HAVE_SYS_EXTATTR_H
#undef HAVE_DECL_EXTATTR_NAMESPACE_USER
#undef HAVE_EXTATTR_GET_FD
#undef HAVE_EXTATTR_GET_FILE
#undef HAVE_EXTATTR_GET_LINK
#undef HAVE_EXTATTR_LIST_FD
#undef HAVE_EXTATTR_LIST_FILE
#undef HAVE_EXTATTR_LIST_LINK
#undef HAVE_EXTATTR_SET_FD
#undef HAVE_EXTATTR_SET_LINK
/* Not in libSystem, or not with FreeBSD's signature. */
#undef HAVE_CLOSE_RANGE
#undef HAVE_CLOSEFROM
#undef HAVE_GETRESGID
#undef HAVE_GETRESUID
#undef HAVE_FUTIMESAT
#undef HAVE_GETVFSBYNAME
#undef HAVE_STRUCT_XVFSCONF
#undef HAVE_STRUCT_STAT_ST_MTIM_TV_NSEC
#undef HAVE_EFTYPE
#undef HAVE_BSDXML_H
#undef HAVE_LZMA_STREAM_ENCODER_MT
#undef HAVE_ZSTD_H
#undef HAVE_LIBZSTD
#undef HAVE_ZSTD_compressStream
#undef HAVE_ZSTD_minCLevel
#define HAVE_SYS_XATTR_H 1
