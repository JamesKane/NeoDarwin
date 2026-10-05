// SPDX-License-Identifier: BSD-2-Clause
// NeoDarwin-Language: portability: libarchive's configuration header, Apple's less what NeoDarwin's base lacks.
//
// libarchive-158 builds with PLATFORM_CONFIG_H naming its config.h, the
// answers of a configure run against macOS. base/libarchive/build.sh points
// PLATFORM_CONFIG_H here instead: Apple's config.h (copied next to this
// header as apple_config.h), then what NeoDarwin's base changes.
#ifndef ND_LIBARCHIVE_CONFIG_H
#define ND_LIBARCHIVE_CONFIG_H

#include "apple_config.h"

// No libiconv yet (P4-21 checkpoint 3): libarchive converts between UTF-8,
// the current locale's charset and UTF-16 with its own code, and
// hdrcharset= options naming other charsets fail.
#undef HAVE_ICONV
#undef HAVE_ICONV_H
#undef HAVE_LOCALCHARSET_H

// No libxml2: the xar format (which needs an XML reader and writer) reports
// itself unsupported.
#undef HAVE_LIBXML2
#undef HAVE_LIBXML_XMLREADER_H
#undef HAVE_LIBXML_XMLWRITER_H

// No CommonCrypto (libSystem's libcommonCrypto is closed): the digests
// mtree, xar and warc record come from the base's libmd (FreeBSD's, as
// FreeBSD's libarchive takes them). libmd has no SHA-384 or RIPEMD-160, so
// those two are unsupported, as RIPEMD-160 already is on macOS.
#undef ARCHIVE_CRYPTO_MD5_LIBSYSTEM
#undef ARCHIVE_CRYPTO_SHA1_LIBSYSTEM
#undef ARCHIVE_CRYPTO_SHA256_LIBSYSTEM
#undef ARCHIVE_CRYPTO_SHA384_LIBSYSTEM
#undef ARCHIVE_CRYPTO_SHA512_LIBSYSTEM
#define ARCHIVE_CRYPTO_MD5_LIBMD 1
#define ARCHIVE_CRYPTO_SHA1_LIBMD 1
#define ARCHIVE_CRYPTO_SHA256_LIBMD 1
#define ARCHIVE_CRYPTO_SHA512_LIBMD 1

// Patch 0001: no libquarantine, and the cryptor and HMAC without
// CommonCrypto.
#define ND_NO_QUARANTINE 1
#define ND_NO_COMMONCRYPTO 1

#endif // ND_LIBARCHIVE_CONFIG_H
