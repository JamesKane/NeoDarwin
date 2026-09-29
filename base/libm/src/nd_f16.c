// SPDX-License-Identifier: BSD-2-Clause
// NeoDarwin-Language: portability: Darwin's _Float16 libm entry points with the C ABI Apple's <math.h> declares.
//
// Each is computed in a wider format and rounded once to half precision,
// which gives the correctly rounded result: the operations are exact in
// float or double (ceil through fmin, and the squares in hypot), sqrt and
// hypot take a correctly rounded double sqrt, and 53 >= 2*11 + 2 makes the
// second rounding harmless (Figueroa, 1995). fma rounds the exact product
// plus the addend to odd first, which makes the second rounding exact too.
#include <fenv.h>
#include <math.h>
#include <stdint.h>
#include <string.h>

_Float16 __fabsf16(_Float16 x) { return (_Float16)fabsf((float)x); }
_Float16 __ceilf16(_Float16 x) { return (_Float16)ceilf((float)x); }
_Float16 __floorf16(_Float16 x) { return (_Float16)floorf((float)x); }
_Float16 __truncf16(_Float16 x) { return (_Float16)truncf((float)x); }
_Float16 __roundf16(_Float16 x) { return (_Float16)roundf((float)x); }
_Float16 __rintf16(_Float16 x) { return (_Float16)rintf((float)x); }
_Float16 __sqrtf16(_Float16 x) { return (_Float16)sqrt((double)x); }
_Float16 __copysignf16(_Float16 x, _Float16 y) { return (_Float16)copysignf((float)x, (float)y); }
_Float16 __fmaxf16(_Float16 x, _Float16 y) { return (_Float16)fmaxf((float)x, (float)y); }
_Float16 __fminf16(_Float16 x, _Float16 y) { return (_Float16)fminf((float)x, (float)y); }

_Float16
__hypotf16(_Float16 x, _Float16 y)
{
	if (isinf(x) || isinf(y))
		return (_Float16)INFINITY;
	double a = x, b = y;
	return (_Float16)sqrt(a * a + b * b);
}

_Float16
__nextafterf16(_Float16 x, _Float16 y)
{
	if (isnan(x) || isnan(y))
		return x + y;
	if (x == y)
		return y;
	uint16_t u;
	memcpy(&u, &x, sizeof u);
	if (x == 0)
		u = (uint16_t)((y < 0 ? 0x8000 : 0) | 1);
	else if ((x < y) == (x > 0))
		u++;
	else
		u--;
	_Float16 r;
	memcpy(&r, &u, sizeof r);
	// C11 F.10.8.3: overflow for an infinite result, underflow for a
	// subnormal or zero one.
	if (isinf(r))
		feraiseexcept(FE_OVERFLOW | FE_INEXACT);
	else if ((u & 0x7c00) == 0)
		feraiseexcept(FE_UNDERFLOW | FE_INEXACT);
	return r;
}

_Float16
__fmaf16(_Float16 x, _Float16 y, _Float16 z)
{
	double p = (double)x * (double)y;  // exact: 22 significant bits
	double c = z;
	double s = p + c;
	if (fegetround() != FE_TONEAREST || !isfinite(s))
		return (_Float16)s;  // directed roundings compose exactly
	// TwoSum: s + e == p + c exactly. If s is inexact and even, step to the
	// neighbour on e's side, which is odd: round-to-odd in 53 bits.
	double bp = s - c, bc = s - bp;
	double e = (p - bp) + (c - bc);
	uint64_t u;
	memcpy(&u, &s, sizeof u);
	if (e != 0 && (u & 1) == 0) {
		if ((e > 0) == (s > 0))
			u++;
		else
			u--;
		memcpy(&s, &u, sizeof s);
	}
	return (_Float16)s;
}
