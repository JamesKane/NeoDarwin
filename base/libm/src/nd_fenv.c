// SPDX-License-Identifier: BSD-2-Clause
// NeoDarwin-Language: expressibility: C99 <fenv.h> for arm64 with Darwin's fenv_t layout, reading and writing FPCR/FPSR through ACLE's system-register intrinsics.
//
// Darwin's arm64 fenv_t is {fpsr, fpcr} as two 64-bit words, its FE_ rounding
// values are the FPCR RMode bits in place (FE_UPWARD 0x00400000), and it adds
// FE_FLUSHTOZERO (FPSR.IDC). FreeBSD's msun/aarch64/fenv.c packs both
// registers into one uint64_t and numbers rounding modes 0-3, so it can't
// serve Darwin's ABI; these are written to Darwin's <fenv.h> instead, with
// the semantics of FreeBSD's (flags are raised by setting FPSR bits).
#include <arm_acle.h>
#include <fenv.h>
#include <stdint.h>

#define FPCR_RMODE_MASK 0x00C00000ULL
#define FPCR_TRAP_MASK (__fpcr_trap_invalid | __fpcr_trap_divbyzero | __fpcr_trap_overflow | \
			__fpcr_trap_underflow | __fpcr_trap_inexact | __fpcr_trap_denormal)

static inline uint64_t get_fpsr(void) { return __arm_rsr64("fpsr"); }
static inline void set_fpsr(uint64_t v) { __arm_wsr64("fpsr", v); }
static inline uint64_t get_fpcr(void) { return __arm_rsr64("fpcr"); }
static inline void set_fpcr(uint64_t v) { __arm_wsr64("fpcr", v); }

const fenv_t _FE_DFL_ENV = {0, 0};
const fenv_t _FE_DFL_DISABLE_DENORMS_ENV = {0, __fpcr_flush_to_zero};

int
feclearexcept(int excepts)
{
	set_fpsr(get_fpsr() & ~(uint64_t)(excepts & FE_ALL_EXCEPT));
	return 0;
}

int
fegetexceptflag(fexcept_t *flagp, int excepts)
{
	*flagp = (fexcept_t)(get_fpsr() & (uint64_t)(excepts & FE_ALL_EXCEPT));
	return 0;
}

int
fesetexceptflag(const fexcept_t *flagp, int excepts)
{
	uint64_t m = (uint64_t)(excepts & FE_ALL_EXCEPT);
	set_fpsr((get_fpsr() & ~m) | (*flagp & m));
	return 0;
}

int
feraiseexcept(int excepts)
{
	set_fpsr(get_fpsr() | (uint64_t)(excepts & FE_ALL_EXCEPT));
	return 0;
}

int
fetestexcept(int excepts)
{
	return (int)(get_fpsr() & (uint64_t)(excepts & FE_ALL_EXCEPT));
}

int
fegetround(void)
{
	return (int)(get_fpcr() & FPCR_RMODE_MASK);
}

int
fesetround(int round)
{
	if ((uint64_t)(unsigned)round & ~FPCR_RMODE_MASK)
		return -1;
	set_fpcr((get_fpcr() & ~FPCR_RMODE_MASK) | (uint64_t)(unsigned)round);
	return 0;
}

int
fegetenv(fenv_t *envp)
{
	envp->__fpsr = get_fpsr();
	envp->__fpcr = get_fpcr();
	return 0;
}

// Save the environment, clear the flags, and turn off every trap.
int
feholdexcept(fenv_t *envp)
{
	fegetenv(envp);
	set_fpsr(envp->__fpsr & ~(uint64_t)FE_ALL_EXCEPT);
	set_fpcr(envp->__fpcr & ~(uint64_t)FPCR_TRAP_MASK);
	return 0;
}

int
fesetenv(const fenv_t *envp)
{
	set_fpsr(envp->__fpsr);
	set_fpcr(envp->__fpcr);
	return 0;
}

// Install the environment, then raise the flags that were set before.
int
feupdateenv(const fenv_t *envp)
{
	uint64_t raised = get_fpsr() & FE_ALL_EXCEPT;
	fesetenv(envp);
	set_fpsr(get_fpsr() | raised);
	return 0;
}

// FLT_ROUNDS's encoding of the current rounding mode (C99 5.2.4.2.2).
int __fegetfltrounds(void);
int
__fegetfltrounds(void)
{
	switch (fegetround()) {
	case FE_TOWARDZERO: return 0;
	case FE_TONEAREST: return 1;
	case FE_UPWARD: return 2;
	case FE_DOWNWARD: return 3;
	default: return -1;
	}
}
