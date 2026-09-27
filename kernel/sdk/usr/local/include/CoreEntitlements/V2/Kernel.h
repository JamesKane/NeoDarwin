// SPDX-License-Identifier: BSD-2-Clause
// NeoDarwin-Language: expressibility: C header included by XNU's libkern/libkern/amfi/amfi.h; Apple's CoreEntitlements V2 headers are not published.
//
// XNU includes this header but compiles only against the CoreEntitlements V1
// types it ships in EXTERNAL_HEADERS/CoreEntitlements. NeoDarwin supplies no V2
// interface; entitlements policy is NeoDarwin's own (namespaces design §5).
#ifndef ND_COREENTITLEMENTS_V2_Kernel_H
#define ND_COREENTITLEMENTS_V2_Kernel_H

/* The CoreEntitlements implementation a policy module registers with the
 * kernel (amfi_core_entitlements_register). XNU holds it only by pointer. */
typedef struct CEKernelAPI CEKernelAPI_t;
#endif
