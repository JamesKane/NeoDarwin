// SPDX-License-Identifier: BSD-2-Clause
// NeoDarwin-Language: portability: the identity-matrix constants Apple's <simd/matrix.h> declares as libsystem_m data.
#include <simd/matrix.h>

const simd_half2x2 matrix_identity_half2x2 = {{{1, 0}, {0, 1}}};
const simd_half3x3 matrix_identity_half3x3 = {{{1, 0, 0}, {0, 1, 0}, {0, 0, 1}}};
const simd_half4x4 matrix_identity_half4x4 = {{{1, 0, 0, 0}, {0, 1, 0, 0}, {0, 0, 1, 0}, {0, 0, 0, 1}}};
const simd_float2x2 matrix_identity_float2x2 = {{{1, 0}, {0, 1}}};
const simd_float3x3 matrix_identity_float3x3 = {{{1, 0, 0}, {0, 1, 0}, {0, 0, 1}}};
const simd_float4x4 matrix_identity_float4x4 = {{{1, 0, 0, 0}, {0, 1, 0, 0}, {0, 0, 1, 0}, {0, 0, 0, 1}}};
const simd_double2x2 matrix_identity_double2x2 = {{{1, 0}, {0, 1}}};
const simd_double3x3 matrix_identity_double3x3 = {{{1, 0, 0}, {0, 1, 0}, {0, 0, 1}}};
const simd_double4x4 matrix_identity_double4x4 = {{{1, 0, 0, 0}, {0, 1, 0, 0}, {0, 0, 1, 0}, {0, 0, 0, 1}}};
