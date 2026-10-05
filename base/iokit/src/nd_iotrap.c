// SPDX-License-Identifier: BSD-2-Clause
// NeoDarwin-Language: portability: a Mach trap entry point with the C ABI IOKitLib's C sources call.
// iokit_user_client_trap, Mach trap -100 (docs/base/corefoundation.md,
// IOKitLib). IOKitUser's IOTrap.s builds it with the kernel_trap macro of
// <mach/machine/syscall_sw.h>, which xnu publishes for 32-bit Arm and i386
// only; Libsyscall's arm64 traps are the same three instructions: the
// negative trap number in x16, svc #0x80, return. The eight arguments
// stay in x0-x7.
#include <mach/mach_types.h>

__attribute__((naked)) kern_return_t iokit_user_client_trap(mach_port_t connect __unused, unsigned int index __unused,
    uintptr_t p1 __unused, uintptr_t p2 __unused, uintptr_t p3 __unused, uintptr_t p4 __unused, uintptr_t p5 __unused,
    uintptr_t p6 __unused) {
	__asm__ volatile("mov x16, #-100\n\tsvc #0x80\n\tret");
}
