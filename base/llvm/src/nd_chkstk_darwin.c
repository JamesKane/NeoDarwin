// SPDX-License-Identifier: BSD-2-Clause
// NeoDarwin-Language: portability: a compiler support routine with the calling convention Apple's clang emits.
//
// ___chkstk_darwin for arm64. Apple's clang calls it before a frame or an
// alloca larger than a page grows the stack, with the size in bytes in x9,
// through x16. It probes the new stack a page at a time so a guard page is
// hit before anything past it is touched. libcompiler_rt exports it on macOS,
// but compiler-rt doesn't have it: Apple adds it in a part of the library it
// doesn't publish.
//
// libpthread publishes the probe itself, as thread_chkstk_darwin
// (libpthread-539 src/pthread_asm.s): it checks the size against the thread's
// stack bounds and probes page by page, preserving every register the caller
// can rely on (x9, x16, x17 and the flags aside). libcompiler_rt links
// libsystem_pthread upward on macOS, so the export here is a tail branch to it.
// The branch goes through a stub that uses only x16 and x17, which the caller
// has already given up.

__attribute__((naked, visibility("default"))) void
__chkstk_darwin(void)
{
	__asm__ volatile("b _thread_chkstk_darwin");
}
