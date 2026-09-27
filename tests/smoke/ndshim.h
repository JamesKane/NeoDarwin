// SPDX-License-Identifier: BSD-2-Clause
// NeoDarwin-Language: expressibility: smoke-test fixture proving the C ABI boundary that language-policy.md §3 makes the only cross-language interface.
#ifndef ND_SMOKE_NDSHIM_H
#define ND_SMOKE_NDSHIM_H

#include <stdint.h>

// A C23 function Swift calls through the generated module map.
uint32_t nd_shim_answer(uint32_t seed);

#endif
