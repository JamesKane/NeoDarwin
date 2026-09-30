// SPDX-License-Identifier: BSD-2-Clause
// NeoDarwin-Language: expressibility: system registers, cache maintenance and the EL2-to-EL1 switch have no Swift spelling.
//
// The loader's last act: with boot services gone and interrupts masked, turn
// the MMU and caches off and enter the kernel at EL1, the state iBoot leaves
// it in (docs/kernel/arm64-sbsa-bringup.md §2.1). start.s reads boot_args and
// builds its own page tables with the MMU off, so everything it reads must
// already be cleaned to the point of coherency.
#include "uefi.h"

uint64_t nd_current_el(void) {
	uint64_t v;
	__asm__ volatile("mrs %0, CurrentEL" : "=r"(v));
	return (v >> 2) & 3;
}

uint64_t nd_cntfrq(void) {
	uint64_t v;
	__asm__ volatile("mrs %0, cntfrq_el0" : "=r"(v));
	return v;
}

uint64_t nd_cntpct(void) {
	uint64_t v;
	__asm__ volatile("isb; mrs %0, cntpct_el0" : "=r"(v));
	return v;
}

uint64_t nd_cntvct(void) {
	uint64_t v;
	__asm__ volatile("isb; mrs %0, cntvct_el0" : "=r"(v));
	return v;
}

// ID_AA64PFR0_EL1.EL3 (bits 15:12): nonzero when the CPU implements EL3,
// where PSCI firmware such as TF-A lives. Readable at EL1 and EL2.
uint64_t nd_el3_implemented(void) {
	uint64_t v;
	__asm__ volatile("mrs %0, id_aa64pfr0_el1" : "=r"(v));
	return (v >> 12) & 0xf;
}

uint64_t nd_mpidr(void) {
	uint64_t v;
	__asm__ volatile("mrs %0, mpidr_el1" : "=r"(v));
	return v;
}

// One 32-bit access to a device register (Swift has no volatile loads).
uint32_t nd_mmio_read32(uint64_t address) {
	uint32_t v = *(volatile uint32_t *)(uintptr_t)address;
	__asm__ volatile("dsb ld" : : : "memory");
	return v;
}

void nd_dcache_clean_poc(uint64_t start, uint64_t length) {
	uint64_t ctr;
	__asm__ volatile("mrs %0, ctr_el0" : "=r"(ctr));
	const uint64_t line = 4ull << ((ctr >> 16) & 0xf);  // CTR_EL0.DminLine, in words
	for (uint64_t a = start & ~(line - 1); a < start + length; a += line) {
		__asm__ volatile("dc cvac, %0" : : "r"(a) : "memory");
	}
	__asm__ volatile("dsb sy" : : : "memory");
}

[[noreturn]] void nd_enter_kernel(uint64_t entry, uint64_t boot_args) {
	__asm__ volatile(
	    "msr daifset, #0xf\n"
	    "mov x19, %0\n"
	    "mov x20, %1\n"
	    "mrs x9, CurrentEL\n"
	    "lsr x9, x9, #2\n"
	    "cmp x9, #2\n"
	    "b.eq 2f\n"
	    // EL1: the firmware's identity map covers this code, so turning the
	    // MMU off continues at the same (physical) address.
	    "mrs x9, sctlr_el1\n"
	    "bic x9, x9, #(1 << 0)\n"   // M
	    "bic x9, x9, #(1 << 2)\n"   // C
	    "bic x9, x9, #(1 << 12)\n"  // I
	    "dsb sy\n"
	    "msr sctlr_el1, x9\n"
	    "isb\n"
	    "ic iallu\n"
	    "tlbi vmalle1\n"
	    "dsb sy\n"
	    "isb\n"
	    "mov x0, x20\n"
	    "br x19\n"
	    // EL2: make EL1 AArch64 with the timers and FP untrapped, then return
	    // into the kernel at EL1h with interrupts masked and the MMU off.
	    "2:\n"
	    "mov x9, #(1 << 31)\n"      // HCR_EL2.RW; TSC=0 so SMC (PSCI) reaches EL3
	    "msr hcr_el2, x9\n"
	    "mov x9, #3\n"              // CNTHCTL_EL2.EL1PCTEN | EL1PCEN
	    "msr cnthctl_el2, x9\n"
	    "msr cntvoff_el2, xzr\n"
	    "mov x9, #0x33ff\n"         // CPTR_EL2: no FP/SIMD traps
	    "msr cptr_el2, x9\n"
	    "msr vttbr_el2, xzr\n"
	    // EL1 reads VPIDR_EL2 and VMPIDR_EL2 as MIDR and MPIDR; they reset
	    // to UNKNOWN values and the firmware at EL2 needn't have set them.
	    "mrs x9, midr_el1\n"
	    "msr vpidr_el2, x9\n"
	    "mrs x9, mpidr_el1\n"
	    "msr vmpidr_el2, x9\n"
	    // EL1 uses the GIC's system registers: ICC_SRE_EL2.SRE | Enable.
	    "mrs x9, icc_sre_el2\n"
	    "orr x9, x9, #0x1\n"
	    "orr x9, x9, #0x8\n"
	    "msr icc_sre_el2, x9\n"
	    "movz x9, #0x0800\n"       // SCTLR_EL1 = 0x30d00800: RES1 bits only,
	    "movk x9, #0x30d0, lsl #16\n"  // MMU, caches and alignment checks off
	    "msr sctlr_el1, x9\n"
	    "mov x9, #0x3c5\n"          // SPSR_EL2: EL1h, DAIF masked
	    "msr spsr_el2, x9\n"
	    "msr elr_el2, x19\n"
	    "ic iallu\n"
	    "tlbi vmalle1\n"
	    "dsb sy\n"
	    "isb\n"
	    "mov x0, x20\n"
	    "eret\n"
	    :
	    : "r"(entry), "r"(boot_args)
	    : "x0", "x9", "x19", "x20", "memory");
	__builtin_unreachable();
}
