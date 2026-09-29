// SPDX-License-Identifier: BSD-2-Clause
// NeoDarwin-Language: portability: Darwin-only libm entry points with the C ABI Apple's <math.h> declares, built on FreeBSD's msun.
//
// The rest of Darwin's names for msun functions (__isnand, the __inline_*
// classifiers, __sinpi, ldexp, the long double complex trig functions) are
// plain aliases, made by the linker from aliases.txt.
#include <math.h>

// Darwin's libm reports errors through the floating-point flags only.
int
__math_errhandling(void)
{
	return MATH_ERREXCEPT;
}

// What(1)-style version string, as Libm's version_info.c defines it.
extern const char __Libm_version[];
const char __Libm_version[] = "@(#) NeoDarwin libsystem_m, FreeBSD msun 050683bb8e13";

// FreeBSD's sincos(3) and sinpi/cospi(3), under Darwin's struct-return forms.
void sincos(double, double *, double *);
void sincosf(float, float *, float *);
double sinpi(double);
float sinpif(float);
double cospi(double);
float cospif(float);

struct __double2
__sincos_stret(double x)
{
	struct __double2 r;
	sincos(x, &r.__sinval, &r.__cosval);
	return r;
}

struct __float2
__sincosf_stret(float x)
{
	struct __float2 r;
	sincosf(x, &r.__sinval, &r.__cosval);
	return r;
}

struct __double2
__sincospi_stret(double x)
{
	return (struct __double2){sinpi(x), cospi(x)};
}

struct __float2
__sincospif_stret(float x)
{
	return (struct __float2){sinpif(x), cospif(x)};
}

// Darwin's <math.h> has __sincospi[f] as header inlines over the _stret
// forms; the library also exports them out of line, under C names that don't
// collide with the inlines. (__sincos[f] are aliases of FreeBSD's sincos[f].)
void nd_sincospi(double, double *, double *) __asm__("___sincospi");
void nd_sincospif(float, float *, float *) __asm__("___sincospif");

void
nd_sincospi(double x, double *sinp, double *cosp)
{
	*sinp = sinpi(x);
	*cosp = cospi(x);
}

void
nd_sincospif(float x, float *sinp, float *cosp)
{
	*sinp = sinpif(x);
	*cosp = cospif(x);
}
