// SPDX-License-Identifier: BSD-2-Clause
// NeoDarwin-Language: portability: the interface between ndamfi's C and its libkern C++ is a C header.
//
// ndamfi's kernel-internal interfaces: the OSEntitlements objects of
// nd_entitlements_os.cpp, the code-signing policy of nd_amfi_policy.c and
// the run-time trust caches of nd_amfi_grant.c.

#ifndef ND_AMFI_INTERNAL_H
#define ND_AMFI_INTERNAL_H

#include <stdbool.h>
#include <mach/kern_return.h>
#include <kern/cs_blobs.h>

struct cs_blob;
struct proc;

// OSEntitlements objects (nd_entitlements_os.cpp). nd_osent_create returns
// an empty object (retained); nd_osent_adopt fills it from a signature whose
// storage is final, and returns how many entitlements it grants.
void *nd_osent_create(void);
void nd_osent_release(void *osent);
kern_return_t nd_osent_adopt(void *osent, struct cs_blob *blob, unsigned *count);
void nd_osent_invalidate(void *osent);
void *nd_osent_as_dict(void *osent);
bool nd_osent_get_xml(void *osent, CS_GenericBlob **blob);
kern_return_t nd_osent_query_bool(const void *osent, const char *name);
kern_return_t nd_osent_query_bool_proc(struct proc *proc, const char *name);
kern_return_t nd_osent_query_string(const void *osent, const char *name, const char *value);
kern_return_t nd_osent_query_string_proc(struct proc *proc, const char *name, const char *value);
kern_return_t nd_osent_copy_object(const void *osent, const char *name, void **object);
kern_return_t nd_osent_copy_object_proc(struct proc *proc, const char *name, void **object);

// The code-signing policy (nd_amfi_policy.c): decides enforcement and
// registers the MAC policy that marks trust-cached binaries as platform
// binaries. Called once from ndamfi's startup.
void nd_amfi_policy_init(void);

// Run-time trust caches (nd_amfi_grant.c, P2-01): reads the package roots
// (nd_pkg_root=) and registers the ndsign grant verifier. Called once from
// ndamfi's startup.
void nd_amfi_grant_init(void);

#endif
