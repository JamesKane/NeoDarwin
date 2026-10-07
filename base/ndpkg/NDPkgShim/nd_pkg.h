// SPDX-License-Identifier: BSD-2-Clause
// NeoDarwin-Language: portability: ndpkg checks signatures with ndamfi's C verifier (the kernel's code) and reaches libzstd, libmd and sysctl, which Embedded Swift can't import, through plain C.
//
// ndpkg's C shim (base/ndpkg, P2-01): the package key chain checked by the
// same C the kernel runs (kernel/neodarwin/amfi/nd_ndsign.c), SHA-2 from
// libmd, zstd from libzstd (both bound at run time with dlopen, since an
// Embedded Swift executable links libSystem alone), and ndamfi's sysctls:
// the package roots the kernel trusts and the run-time trust-cache load.
#ifndef ND_PKG_H
#define ND_PKG_H
#include <errno.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void nd_pkg_warn(const char *_Nonnull message);
int nd_pkg_errno(void);

// The whole file, malloc'd (free with nd_pkg_free); NULL with errno set.
uint8_t *_Nullable nd_pkg_read_file(const char *_Nonnull path, size_t *_Nonnull len);
void nd_pkg_free(void *_Nullable p);

// One zstd frame with its content size recorded, as .ndpkg archives are
// written; NULL and a message in `error` (`size` bytes) on failure.
uint8_t *_Nullable nd_pkg_zstd_decompress(const uint8_t *_Nonnull in, size_t len, size_t *_Nonnull out_len,
    char *_Nonnull error, size_t size);

// SHA-256 (libmd); false if libmd can't be loaded.
bool nd_pkg_sha256(const uint8_t *_Nullable in, size_t len, uint8_t *_Nonnull out);

// The package roots the kernel trusts (sysctl
// security.codesigning.neodarwin.pkg_roots, from the boot-arg nd_pkg_root):
// up to `max` 32-byte keys into `roots`; returns how many, 0 for none.
size_t nd_pkg_kernel_roots(uint8_t *_Nonnull roots, size_t max);

// Verifies an ndsign bundle whose statement is of `kind` against `roots` at
// the current time; on success copies the statement's body (its fields,
// without the signature) into `statement`, NUL-terminated. Returns 0 or an
// ndsign error, which nd_pkg_error_name() names.
int nd_pkg_verify_bundle(const uint8_t *_Nonnull bundle, size_t len, const uint8_t *_Nonnull roots, size_t nroots,
    const char *_Nonnull kind, char *_Nonnull statement, size_t size);
const char *_Nonnull nd_pkg_error_name(int error);

// Hands a trust-cache module and its grant to the kernel
// (security.codesigning.neodarwin.load_trust_cache), which checks the grant
// itself. Returns 0 or an errno: EPERM (not root or not entitled), EAUTH
// (refused), EEXIST (already loaded), EINVAL.
int nd_pkg_load_trust_cache(const uint8_t *_Nonnull module, size_t module_len, const uint8_t *_Nonnull grant,
    size_t grant_len);

#endif
