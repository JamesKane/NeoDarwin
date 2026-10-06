/*
 * SPDX-License-Identifier: BSD-2-Clause
 * NeoDarwin-Language: portability: a C header standing in for one from Apple's internal SDK, included by ld64's Options.cpp and libunwind/AddressSpace.hpp.
 * The one dyld SPI ld64's in-process unwinder names, as dyld-1323.3's
 * include/mach-o/dyld_priv.h declares it; libdyld exports it.
 */
#ifndef ND_LD64_DYLD_PRIV_H
#define ND_LD64_DYLD_PRIV_H
#include <stdbool.h>
#include <stdint.h>
#include <mach-o/dyld.h>
#ifdef __cplusplus
extern "C" {
#endif
struct dyld_unwind_sections {
	const struct mach_header *mh;
	const void *dwarf_section;
	uintptr_t dwarf_section_length;
	const void *compact_unwind_section;
	uintptr_t compact_unwind_section_length;
};
extern bool _dyld_find_unwind_sections(void *, struct dyld_unwind_sections *);
#ifdef __cplusplus
}
#endif
#endif
