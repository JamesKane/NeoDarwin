// SPDX-License-Identifier: BSD-2-Clause
// NeoDarwin-Language: portability: a stand-in with the C interface of a closed Apple library.
//
// Stand-in for libsystem_featureflags.dylib, which Apple does not publish:
// the store of feature flags that switch code paths per build and per
// device. libmalloc links it (upward), and libSystem reexports it, so the
// image must exist for dyld to load them. NeoDarwin's libraries compile
// their flag queries against base/sdk's os/feature_private.h, which answers
// each with its default and calls nothing here. The two entry points Apple's
// header expands to are provided for code built against Apple's: with no flag
// store, a flag is off, and a query with a fallback answers the fallback.
// More of the interface is added as NeoDarwin libraries come to need it
// (docs/base/libsystem.md).

#include <stdbool.h>

// Apple's os/feature_private.h declares these; base/sdk's shim does not.
bool _os_feature_enabled_impl(const char *domain, const char *feature);
bool _os_feature_enabled_simple_impl(const char *domain, const char *feature, bool fallback);

bool
_os_feature_enabled_impl(const char *domain, const char *feature)
{
	(void)domain;
	(void)feature;
	return false;
}

bool
_os_feature_enabled_simple_impl(const char *domain, const char *feature, bool fallback)
{
	(void)domain;
	(void)feature;
	return fallback;
}
