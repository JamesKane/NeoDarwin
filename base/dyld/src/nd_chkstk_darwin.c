// SPDX-License-Identifier: BSD-2-Clause
// NeoDarwin-Language: portability: a compiler support routine with the calling convention Apple's clang emits.
//
// ___chkstk_darwin for dyld's static link. Apple's clang calls it before a
// frame or an alloca larger than a page grows the stack, with the size in
// bytes in x9, through x16. Apple's dyld takes it from the toolchain's closed
// libclang_rt.osx.a (-fapple-link-rtlib), whose source isn't published;
// compiler-rt's own __chkstk (lib/builtins/aarch64/chkstk.S) is the Windows
// routine, with another size convention.
//
// This is a plain probe: it reads one word in every page of the new stack,
// top down, so the guard page faults before anything past it is touched. It
// clobbers only x16, x17 and the flags. libpthread's thread_chkstk_darwin,
// which libcompiler_rt.dylib uses (base/llvm/src/nd_chkstk_darwin.c), also
// checks the thread's recorded stack bounds; dyld runs before libpthread has
// recorded any, which is why Libc's and libpthread's dyld archives build
// without stack checks. The probe step is 4 KiB, which also covers 16 KiB pages.

__attribute__((naked, visibility("hidden"))) void
__chkstk_darwin(void)
{
	__asm__ volatile(
		"	mov	x16, x9\n"
		"	mov	x17, sp\n"
		"1:	cmp	x16, #0x1000\n"
		"	b.lo	2f\n"
		"	sub	x17, x17, #0x1000\n"
		"	ldr	xzr, [x17]\n"
		"	sub	x16, x16, #0x1000\n"
		"	b	1b\n"
		"2:	sub	x17, x17, x16\n"
		"	ldr	xzr, [x17]\n"
		"	ret\n");
}
