// NeoDarwin-Language: portability: Apple's trust-cache lookup, carried in its original C under the APSL.
/*
 * Copyright (c) 2011-2018 Apple Inc. All rights reserved.
 *
 * @APPLE_OSREFERENCE_LICENSE_HEADER_START@
 *
 * This file contains Original Code and/or Modifications of Original Code
 * as defined in and that are subject to the Apple Public Source License
 * Version 2.0 (the 'License'). You may not use this file except in
 * compliance with the License. The rights granted to you under the License
 * may not be used to create, or enable the creation or redistribution of,
 * unlawful or unlicensed copies of an Apple operating system, or to
 * circumvent, violate, or enable the circumvention or violation of, any
 * terms of an Apple operating system software license agreement.
 *
 * Please obtain a copy of the License at
 * http://www.opensource.apple.com/apsl/ and read it before using this file.
 *
 * The Original Code and all software distributed under the License are
 * distributed on an 'AS IS' basis, WITHOUT WARRANTY OF ANY KIND, EITHER
 * EXPRESS OR IMPLIED, AND APPLE HEREBY DISCLAIMS ALL SUCH WARRANTIES,
 * INCLUDING WITHOUT LIMITATION, ANY WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE, QUIET ENJOYMENT OR NON-INFRINGEMENT.
 * Please see the License for the specific language governing rights and
 * limitations under the License.
 *
 * @APPLE_OSREFERENCE_LICENSE_HEADER_END@
 */
/*
 * lookup_in_trust_cache_module() from xnu-8019.80.24 osfmk/arm/trustcache.c,
 * unchanged apart from the name (nd_tc_lookup_module) and the includes: the
 * rest of that file bound it to pmap and device-tree state later xnu
 * releases no longer have. The module format it searches is xnu's own
 * <kern/trustcache.h> (struct trust_cache_module1), shipped in the current
 * release. NeoDarwin's changes are under the same licence.
 */

#include <stdbool.h>
#include <stddef.h>
#include <string.h>
#include <kern/trustcache.h>

#include "nd_trustcache.h"

// Lookup cdhash in a trust cache module.
// Suitable for all kinds of trust caches (but loadable ones are currently different).
bool
nd_tc_lookup_module(
	struct trust_cache_module1 const * const module,
	uint8_t const   cdhash[CS_CDHASH_LEN],
	uint8_t * const hash_type,
	uint8_t * const flags)
{
	size_t lim;
	struct trust_cache_entry1 const *base = &module->entries[0];

	struct trust_cache_entry1 const *entry = NULL;

	bool found = false;

	/* Initialization already (redundantly) verified the size of the module for us. */
	for (lim = module->num_entries; lim != 0; lim >>= 1) {
		entry = base + (lim >> 1);
		int cmp = memcmp(cdhash, entry->cdhash, CS_CDHASH_LEN);
		if (cmp == 0) {
			found = true;
			break;
		}
		if (cmp > 0) {  /* key > p: move right */
			base = entry + 1;
			lim--;
		}               /* else move left */
	}

	if (found) {
		*hash_type = entry->hash_type;
		*flags = entry->flags;
		return true;
	}

	return false;
}

