/*
 * SPDX-License-Identifier: BSD-2-Clause
 * NeoDarwin-Language: expressibility: a C header standing in for one hfs includes from Apple's internal SDK.
 *
 * Stands in for AppleKeyStore's AppleKeyStoreFSServices.h, which is not
 * published. hfs's hfs_iokit.h includes it for the key types in its
 * content-protection helpers (hfs_unwrap_key() and friends, core/hfs_iokit.cpp).
 * macOS HFS+ builds without CONFIG_PROTECT (core/kext-config.h), so nothing
 * calls those helpers, and NeoDarwin has no AppleKeyStore to answer them:
 * the platform function below is never registered, and the helpers return
 * ENXIO. Written from hfs's use of the names.
 */
#ifndef _NEODARWIN_APPLEKEYSTORE_FSSERVICES_H
#define _NEODARWIN_APPLEKEYSTORE_FSSERVICES_H

#include <stdint.h>
#include <sys/cprotect.h>

struct aks_cred_s;
struct aks_wrapped_key_s;
struct aks_raw_key_s;
typedef struct aks_cred_s *aks_cred_t;
typedef struct aks_wrapped_key_s *aks_wrapped_key_t;
typedef struct aks_raw_key_s *aks_raw_key_t;

typedef struct {
	int (*unwrap_key)(aks_cred_t access, const aks_wrapped_key_t wrapped_key_in, aks_raw_key_t key_out);
	int (*rewrap_key)(aks_cred_t access, cp_key_class_t dp_class, const aks_wrapped_key_t wrapped_key_in,
	    aks_wrapped_key_t wrapped_key_out);
	int (*new_key)(aks_cred_t access, cp_key_class_t dp_class, aks_raw_key_t key_out,
	    aks_wrapped_key_t wrapped_key_out);
	int (*backup_key)(aks_cred_t access, const aks_wrapped_key_t wrapped_key_in, aks_wrapped_key_t wrapped_key_out);
} aks_file_system_key_services_t;

/* The platform function that would hand hfs the table; nothing registers it. */
#define kAKSFileSystemKeyServices "neodarwin-no-aks-file-system-key-services"

#endif /* _NEODARWIN_APPLEKEYSTORE_FSSERVICES_H */
