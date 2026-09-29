#!/usr/bin/env bash
# SPDX-License-Identifier: BSD-2-Clause
# libsystem_m from FreeBSD's lib/msun (docs/base/libsystem.md).
#   build.sh OUT MSUN_ROOT SYSROOT [DEPROOT...]   (none needed; see the link)
# MSUN_ROOT holds the freebsd-src files freebsd.lock pins, laid out as in
# freebsd-src (lib/msun/..., lib/libc/...). OUT receives
# usr/lib/system/libsystem_m.dylib.
#
# Why FreeBSD: Apple last published Libm in 2012 (Libm-2026), for i386,
# x86_64 and armv7 only, mostly assembly; its ARM directory already took
# several functions from FreeBSD's msun. Reuse order (repository.md §3.1)
# therefore lands on FreeBSD, pinned at the commit ndcrypto uses.
#
# How: msun's Makefile for an LDBL_PREC 53 platform. On arm64 Darwin long
# double is IEEE double, so msun's *l entry points are aliases of the double
# ones (the `LDBL_MANT_DIG == 53` __weak_reference lines), not the ld128
# code FreeBSD/aarch64 uses. The sources compile unmodified against FreeBSD's
# own <math.h> and msun's aarch64 <fenv.h> (used only inline, internally),
# with compat/ supplying the FreeBSD <sys/cdefs.h> environment and the 53-bit
# _fpmath.h. The FreeBSD libc pieces Darwin's libm exports (__fpclassify*,
# isinf) come from lib/libc/gen. Files whose results encode <math.h>
# constants build against Darwin's <math.h> (below). patches/0001 fixes an
# upstream bug arm64 exposes (sinpi/cospi parity for |x| >= 2^32).
#
# NeoDarwin glue (src/, compiled against Darwin's own <math.h>/<fenv.h>):
#   nd_fenv.c   fenv with Darwin's arm64 fenv_t {fpsr, fpcr} and FE_* values,
#               which differ from FreeBSD's (a packed uint64_t);
#   nd_darwin.c __sincos*_stret, __sincospi[f], __math_errhandling,
#               __Libm_version;
#   nd_f16.c    the _Float16 functions (__ceilf16 ...), correctly rounded
#               from float/double arithmetic;
#   nd_simd.c   the matrix_identity_* constants.
# Darwin's other names for msun functions are linker aliases (aliases.txt).
# exports.txt is Apple's arm64 export list less what isn't built; it is the
# link's export list and the //base:libsystem_m_exports_test ratchet. Not
# built: the vector entry points (__simd_*, __{sin,cos}_{d2,f4},
# __invert_*) and __exp10[f], which msun doesn't have.
#
# The dylib sits below libsystem_c and imports nothing (Apple's imports only
# libcompiler_rt's ___chkstk_darwin and complex multiply, which msun's
# complex code doesn't use), so it links no library. Nothing here comes
# from libsystem_c: no stack protector (-fno-stack-protector, as for the other
# libraries under Libc), and compat/ctype.h keeps nan(3) off the locale tables.
source "$(dirname "$0")/../../tools/base/common.sh"
OUT="$(abspath "$1")"; F="$(abspath "$2")"; SYSROOT="$(abspath "$3")"
HERE_LIBM="$(cd "$(dirname "$0")" && pwd)"
B="$(mktemp -d)"; trap 'rm -rf "$B"' EXIT
F="$(stage_src "$F" "$B/src" "$HERE_LIBM/patches")"   # patches/ applied
cd "$F"

M=lib/msun
# lib/msun/Makefile's COMMON_SRCS for LDBL_PREC 53, less:
# - fenv.c (nd_fenv.c) and s_ilogb*.c (below);
# - e_rem_pio2*.c and k_tanf.c, which the trig sources #include;
# - what Darwin's libm doesn't export: gamma, drem, finite, significand,
#   rsqrt, the float Bessel functions, asinpi/acospi/atanpi, fmaximum/fminimum;
# plus s_fabs.c and s_modf.c, which FreeBSD's libc supplies.
msun=(b_tgamma e_acos e_acosf e_acosh e_acoshf e_asin e_asinf e_atan2 e_atan2f e_atanh e_atanhf e_cosh e_coshf
	e_exp e_expf e_fmod e_fmodf e_hypot e_hypotf e_j0 e_j1 e_jn e_lgamma e_lgamma_r e_lgammaf e_lgammaf_r e_log
	e_log10 e_log10f e_log2 e_log2f e_logf e_pow e_powf e_remainder e_remainderf e_scalb e_sinh e_sinhf e_sqrt
	e_sqrtf k_cos k_cosf k_exp k_expf k_rem_pio2 k_sin k_sinf k_tan s_asinh s_asinhf s_atan s_atanf s_carg
	s_cargf s_cargl s_cbrt s_cbrtf s_ceil s_ceilf s_clog s_clogf s_copysign s_copysignf s_cos s_cosf s_csqrt
	s_csqrtf s_erf s_erff s_exp2 s_exp2f s_expm1 s_expm1f s_fabsf s_fdim s_floor s_floorf s_fma s_fmaf s_fmax
	s_fmaxf s_fmin s_fminf s_frexp s_frexpf s_isfinite s_isnan s_isnormal s_llrint s_llrintf s_llround s_llroundf
	s_llroundl s_log1p s_log1pf s_logb s_logbf s_lrint s_lrintf s_lround s_lroundf s_lroundl s_modff s_nan
	s_nearbyint s_nextafter s_nextafterf s_nexttowardf s_remquo s_remquof s_rint s_rintf s_round s_roundf
	s_scalbln s_scalbn s_scalbnf s_signbit s_signgam s_sin s_sincos s_sincosf s_sinf s_tan s_tanf s_tanh s_tanhf
	s_tgammaf s_trunc s_truncf w_cabs w_cabsf s_cospi s_cospif s_sinpi s_sinpif s_tanpi s_tanpif s_copysignl
	s_fabsl s_llrintl s_lrintl s_modfl catrig catrigf s_ccosh s_ccoshf s_cexp s_cexpf s_cimag s_cimagf s_cimagl
	s_conj s_conjf s_conjl s_cpow s_cpowf s_cpowl s_cproj s_cprojf s_creal s_crealf s_creall s_csinh s_csinhf
	s_ctanh s_ctanhf s_fabs s_modf)
srcs=(lib/libc/gen/isinf.c)
for s in "${msun[@]}"; do
	if [ -e "$M/src/$s.c" ]; then srcs+=("$M/src/$s.c"); else srcs+=("$M/bsdsrc/$s.c"); fi
done
# These return <math.h> constants whose values differ between FreeBSD and
# Darwin (FP_NAN 2 vs 1, FP_ILOGB0 -INT_MAX vs INT_MIN, FP_ILOGBNAN INT_MAX
# vs INT_MIN), so they build against Darwin's <math.h>; nothing in msun uses
# those constants itself. They're copied out of lib/msun/src first, since a
# quoted "math.h" finds the includer's directory before any search path;
# msun's private headers come after the system's (-idirafter).
mkdir -p darwin_math
cp lib/libc/gen/fpclassify.c $M/src/s_ilogb.c $M/src/s_ilogbf.c $M/src/s_ilogbl.c darwin_math/
darwin_math=(darwin_math/fpclassify.c darwin_math/s_ilogb.c darwin_math/s_ilogbf.c darwin_math/s_ilogbl.c)

# lib/msun/Makefile and aarch64/Makefile.inc (LDBL_PREC and the -I order for
# ld128 aside).
fbsd=("${TARGET_FLAGS[@]}" -O2 -fno-math-errno -fno-stack-protector -w
	-DUSE_BUILTIN_FMAF -DUSE_BUILTIN_FMA -DUSE_BUILTIN_FMAXF -DUSE_BUILTIN_FMAX -DUSE_BUILTIN_FMINF
	-DUSE_BUILTIN_FMIN -DUSE_BUILTIN_SQRTF -DUSE_BUILTIN_SQRT
	-include "$HERE_LIBM/compat/nd_msun.h" -I"$HERE_LIBM/compat")
write_rsp "$B/msun_flags" "${fbsd[@]}" -I"$F/$M/aarch64" -I"$F/$M/src" -I"$F/lib/libc/include" \
	$(sysroot_flags "$SYSROOT")
write_rsp "$B/darwin_math_flags" "${fbsd[@]}" $(sysroot_flags "$SYSROOT") -idirafter "$F/$M/src" \
	-idirafter "$F/lib/libc/include"
compile "$B/obj" "$B/msun_flags" "${srcs[@]}"
compile "$B/obj" "$B/darwin_math_flags" "${darwin_math[@]}"

# NeoDarwin's glue, against Darwin's <math.h>, <fenv.h> and <simd/matrix.h>.
write_rsp "$B/nd_flags" "${TARGET_FLAGS[@]}" -O2 -fno-stack-protector -std=c23 -Wall -Wextra -Werror $(sysroot_flags "$SYSROOT")
compile "$B/obj" "$B/nd_flags" "$HERE_LIBM"/src/*.c

# Apple's export list less what isn't built (exports.txt); Darwin's names for
# msun functions as linker aliases (aliases.txt). The image imports nothing:
# no library is linked, so a new import fails the link instead of adding a
# dependency below libsystem_c unnoticed. The version is Apple's current
# libsystem_m's (the SDK's .tbd); there is no Apple project version to use.
mkdir -p "$OUT/usr/lib/system"
xcrun clang "${TARGET_FLAGS[@]}" -dynamiclib -nostdlib -install_name /usr/lib/system/libsystem_m.dylib \
	-current_version 3326.0.1 -compatibility_version 1 -Wl,-umbrella,System "$B"/obj/*.o \
	-Wl,-exported_symbols_list,"$HERE_LIBM/exports.txt" -Wl,-alias_list,"$HERE_LIBM/aliases.txt" \
	-Wl,-dead_strip -o "$OUT/usr/lib/system/libsystem_m.dylib"
