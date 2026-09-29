/*
 * SPDX-License-Identifier: BSD-2-Clause
 * NeoDarwin-Language: expressibility: a C header standing in for one from Apple's internal SDK.
 *
 * Stands in for os/feature_private.h from libsystem_featureflags, which Apple
 * does not publish. Feature flags let Apple switch code paths per build; with
 * no flag store, every query answers with the caller's stated default, and a
 * flag with no default is off.
 */
#ifndef __OS_FEATURE_PRIVATE__
#define __OS_FEATURE_PRIVATE__

#define os_feature_enabled_simple(domain, feature, fallback) (fallback)
#define os_feature_enabled(domain, feature) (0)

#endif /* __OS_FEATURE_PRIVATE__ */
