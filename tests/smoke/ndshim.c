// SPDX-License-Identifier: BSD-2-Clause
// NeoDarwin-Language: expressibility: smoke-test fixture proving the C ABI boundary that language-policy.md §3 makes the only cross-language interface.
#include "ndshim.h"

uint32_t nd_shim_answer(uint32_t seed)
{
	constexpr uint32_t base = 40;   // C23 constexpr: fails loudly if -std=c23 is not in effect
	return base + seed;
}
