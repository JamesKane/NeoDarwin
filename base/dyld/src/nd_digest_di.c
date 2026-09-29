// SPDX-License-Identifier: BSD-2-Clause
// NeoDarwin-Language: portability: corecrypto's digest selectors are C functions dyld calls through Apple's published C interface.
//
// corecrypto's digest selectors for dyld's static link (docs/base/libsystem.md,
// checkpoint 3). dyld hashes code directories with SHA-1, SHA-256 and SHA-384
// (common/MachOFile.cpp, common/MachOLoaded.cpp); Apple's dyld takes the
// digests from the SDK's closed libcorecrypto_static.a. Here the same sources
// as the kernel's crypto provider (docs/kernel/crypto-provider.md) serve:
// xnu's own corecrypto subset has the ccdigest framework and ccsha256_di(),
// and ndcrypto's nd_digest.c has SHA-1 and SHA-384 over FreeBSD's block
// functions. This file supplies the two selectors xnu doesn't have.

#include <corecrypto/ccsha1.h>
#include <corecrypto/ccsha2.h>

extern const struct ccdigest_info nd_sha1_di;
extern const struct ccdigest_info nd_sha384_di;

const struct ccdigest_info *
ccsha1_di(void)
{
	return &nd_sha1_di;
}

const struct ccdigest_info *
ccsha384_di(void)
{
	return &nd_sha384_di;
}
