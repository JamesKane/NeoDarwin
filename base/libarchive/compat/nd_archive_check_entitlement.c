// SPDX-License-Identifier: BSD-2-Clause
// NeoDarwin-Language: portability: libarchive-158's entitlement checks, without Security.framework and CoreFoundation.
//
// Apple's archive_check_entitlement.c lets a process restrict the formats
// and filters libarchive reads to those listed in its
// com.apple.libarchive.formats and com.apple.libarchive.filters
// entitlements, read with SecTaskCopyValueForEntitlement(). A process
// without the entitlement may use every format and filter. NeoDarwin has
// neither Security.framework nor CoreFoundation, and its processes carry no
// such entitlements, so every format and filter is allowed: the answer
// Apple's code gives a process without them.

#include <stdbool.h>

#include "archive_check_entitlement.h"

bool
archive_allow_entitlement_format(const char *format)
{
	(void)format;
	return true;
}

bool
archive_allow_entitlement_filter(const char *filter)
{
	(void)filter;
	return true;
}

void
archive_entitlement_cleanup(void)
{
}
