// SPDX-License-Identifier: BSD-2-Clause
// NeoDarwin-Language: expressibility: the process entry point and the svc #0x80 traps are assembly; Swift cannot spell either.
#include "traps.h"

int64_t nd_syscall(int64_t number, uint64_t a0, uint64_t a1, uint64_t a2, uint64_t a3, uint64_t a4, uint64_t a5) {
	register uint64_t x0 __asm__("x0") = a0, x1 __asm__("x1") = a1, x2 __asm__("x2") = a2;
	register uint64_t x3 __asm__("x3") = a3, x4 __asm__("x4") = a4, x5 __asm__("x5") = a5;
	register int64_t x16 __asm__("x16") = number;
	__asm__ volatile("svc #0x80\n\t"
	                 "b.cc 1f\n\t"
	                 "neg x0, x0\n"
	                 "1:"
	                 : "+r"(x0), "+r"(x1)
	                 : "r"(x2), "r"(x3), "r"(x4), "r"(x5), "r"(x16)
	                 : "memory", "cc");
	return (int64_t)x0;
}

uint64_t nd_mach_trap(int64_t number, uint64_t a0, uint64_t a1, uint64_t a2, uint64_t a3,
                      uint64_t a4, uint64_t a5, uint64_t a6, uint64_t a7) {
	register uint64_t x0 __asm__("x0") = a0, x1 __asm__("x1") = a1, x2 __asm__("x2") = a2, x3 __asm__("x3") = a3;
	register uint64_t x4 __asm__("x4") = a4, x5 __asm__("x5") = a5, x6 __asm__("x6") = a6, x7 __asm__("x7") = a7;
	register int64_t x16 __asm__("x16") = -number;
	__asm__ volatile("svc #0x80"
	                 : "+r"(x0), "+r"(x1)
	                 : "r"(x2), "r"(x3), "r"(x4), "r"(x5), "r"(x6), "r"(x7), "r"(x16)
	                 : "memory", "cc");
	return x0;
}

// LC_UNIXTHREAD entry: the kernel starts the thread here with sp at argc
// (bsd/kern/kern_exec.c, exec_add_user_string layout); nothing below main
// is needed yet, so clear the frame chain and call into Swift.
void pid1_main(void) __attribute__((noreturn));

__attribute__((naked, noreturn)) void start(void) {
	__asm__("mov x29, #0\n\t"
	        "mov x30, #0\n\t"
	        "mov x9, sp\n\t"
	        "and x9, x9, #~15\n\t"
	        "mov sp, x9\n\t"
	        "bl _pid1_main\n\t"
	        "brk #1");
}
