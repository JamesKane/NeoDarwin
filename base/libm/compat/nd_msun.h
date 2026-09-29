// SPDX-License-Identifier: BSD-2-Clause
// NeoDarwin-Language: portability: the FreeBSD <sys/cdefs.h> environment FreeBSD's msun assumes, supplied so its sources compile unmodified against Darwin's headers.
//
// Force-included (-include) ahead of every FreeBSD source libsystem_m builds
// (base/libm/build.sh). Darwin's <sys/cdefs.h> already has __BEGIN_DECLS,
// __pure2, __dead2 and __FBSDID; this adds what it lacks.
#ifndef ND_MSUN_H
#define ND_MSUN_H
#include <sys/cdefs.h>
#include <stdint.h>  // FreeBSD's <sys/types.h> brings in the C99 fixed-width types

// FreeBSD's __CONCAT expands its arguments first (math_private.h pastes
// LDBL_MANT_DIG into a hex-float exponent); Darwin's pastes them unexpanded.
#undef __CONCAT
#define __CONCAT1(x, y) x ## y
#define __CONCAT(x, y) __CONCAT1(x, y)

// Everything visible, as in FreeBSD's own build of libm.
#define __ISO_C_VISIBLE 2023
#define __POSIX_VISIBLE 200809
#define __XSI_VISIBLE 800
#define __BSD_VISIBLE 1

#define __always_inline __inline __attribute__((__always_inline__))

// FreeBSD's <sys/cdefs.h> spells symbol aliases as ELF .weak/.equ directives
// or the alias attribute, neither of which Mach-O has. An assembler .set
// gives the alias the target's address, and the export list (exports.txt)
// decides whether it is visible. Darwin's libm exports its aliases as
// ordinary (not weak) definitions, so both forms become the same.
#define __nd_msun_alias(sym, alias) __asm__(".globl _" #alias "\n\t.set _" #alias ", _" #sym)
#define __weak_reference(sym, alias) __nd_msun_alias(sym, alias)
#define __strong_reference(sym, alias) __nd_msun_alias(sym, alias)

// FreeBSD's <machine/_limits.h> and <machine/_types.h>, for FreeBSD's
// <math.h> (FP_ILOGB0, double_t). arm64 evaluates float in float
// (FLT_EVAL_METHOD 0), as Darwin's <math.h> also defines double_t.
#define __INT_MAX 0x7fffffff
typedef double __double_t;
typedef float __float_t;
#endif
