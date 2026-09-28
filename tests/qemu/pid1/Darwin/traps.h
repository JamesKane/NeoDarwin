// SPDX-License-Identifier: BSD-2-Clause
// NeoDarwin-Language: expressibility: the Darwin arm64 trap ABI (svc #0x80, x16 selects the call) has no Swift spelling.
//
// The two kinds of kernel entry a program without libSystem needs
// (xnu osfmk/arm64/sleh.c, handle_svc): BSD system calls take a positive
// number in x16 and report failure with the carry flag and the errno in x0;
// Mach traps take a negative number and return a kern_return_t or a port
// name in x0.
#ifndef PID1_TRAPS_H
#define PID1_TRAPS_H
#include <stdint.h>

/// A BSD system call: the result, or -errno.
int64_t nd_syscall(int64_t number, uint64_t a0, uint64_t a1, uint64_t a2, uint64_t a3, uint64_t a4, uint64_t a5);

/// A Mach trap, by its (positive) number in osfmk/kern/syscall_sw.c.
uint64_t nd_mach_trap(int64_t number, uint64_t a0, uint64_t a1, uint64_t a2, uint64_t a3,
                      uint64_t a4, uint64_t a5, uint64_t a6, uint64_t a7);

#endif
