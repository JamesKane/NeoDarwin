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

// -- P2-02: the store, activation and the solver ------------------------------------

// Files and directories. Each returns 0 or an errno.
int nd_pkg_mkdir(const char *_Nonnull path, int mode);           // EEXIST if it exists
int nd_pkg_mkdirs(const char *_Nonnull path, int mode);          // with parents; 0 if it exists
int nd_pkg_write_file(const char *_Nonnull path, const uint8_t *_Nullable bytes, size_t len, int mode); // new file, fsync'd
int nd_pkg_symlink(const char *_Nonnull target, const char *_Nonnull path);
int nd_pkg_rename(const char *_Nonnull from, const char *_Nonnull to);
int nd_pkg_unlink(const char *_Nonnull path);
int nd_pkg_rmdir(const char *_Nonnull path);
int nd_pkg_chmod(const char *_Nonnull path, int mode);
int nd_pkg_fsync_dir(const char *_Nonnull path);
// Removes a tree (making its directories writable first); 0 if it's gone.
int nd_pkg_remove_tree(const char *_Nonnull path);
// lstat: 0 nothing there, 1 regular file, 2 directory, 3 symbolic link, 4 other.
int nd_pkg_kind(const char *_Nonnull path);
// A symbolic link's target, malloc'd; NULL if it isn't one.
char *_Nullable nd_pkg_readlink(const char *_Nonnull path);
// A directory's entries but . and .., each followed by a newline, malloc'd;
// NULL with errno set.
char *_Nullable nd_pkg_list_dir(const char *_Nonnull path);
// An exclusive lock on `path` (created), held until exit; 0 or an errno
// (EWOULDBLOCK if another ndpkg holds it).
int nd_pkg_lock(const char *_Nonnull path);
int nd_pkg_pid(void);
bool nd_pkg_is_root(void);

// XNU's nullfs (bsd/miscfs/nullfs, App Translocation's): `lower` mounted
// read-only and nosuid at `mountpoint`, as mountpoint/d/<lower's last
// component>. Needs root and the entitlement com.apple.private.nullfs_allow.
// 0 or an errno.
int nd_pkg_nullfs_mount(const char *_Nonnull lower, const char *_Nonnull mountpoint);
int nd_pkg_unmount(const char *_Nonnull mountpoint);
// Whether a file system is mounted on `path` (its device differs from its
// parent's).
bool nd_pkg_is_mountpoint(const char *_Nonnull path);

// libsolv (src/nd_solve.c). `input` is lines of tab-separated fields:
//     arch ARCH
//     repo installed|available          (the packages that follow)
//     pkg NAME VERSION ARCH KEY         (ARCH any: any architecture)
//     dep provides|requires|conflicts DEP   (of the last pkg)
//     job install|remove|upgrade DEP    (upgrade *: everything)
// where DEP is `name` or `name OP version` (OP = == != < <= > >=). The
// output, malloc'd, is the transaction in order, one step per line:
//     install KEY | upgrade OLDKEY NEWKEY | downgrade OLDKEY NEWKEY | remove KEY
// or `nothing`; or, if it returns nonzero, lines `problem TEXT [DETAIL]`.
int nd_pkg_solve(const char *_Nonnull input, char *_Nullable *_Nonnull output);

#endif
