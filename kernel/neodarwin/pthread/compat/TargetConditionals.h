// SPDX-License-Identifier: BSD-2-Clause
// NeoDarwin-Language: portability: included by Apple's C sources (libpthread kern/).
//
// The TARGET_ conditionals libpthread's kern/ reads. xnu's build has no
// TargetConditionals.h on the kernel search path (xnu includes it only
// outside XNU_KERNEL_PRIVATE), and pthread.kext gets the SDK's. NeoDarwin's
// kernel is a macOS-family arm64 kernel: libpthread then takes its macOS
// paths (JIT write protection without lockdown, no simulator TSD layout).

#ifndef _ND_TARGETCONDITIONALS_H_
#define _ND_TARGETCONDITIONALS_H_

#define TARGET_OS_MAC           1
#define TARGET_OS_OSX           1
#define TARGET_OS_IPHONE        0
#define TARGET_OS_IOS           0
#define TARGET_OS_TV            0
#define TARGET_OS_WATCH         0
#define TARGET_OS_VISION        0
#define TARGET_OS_DRIVERKIT     0
#define TARGET_OS_SIMULATOR     0
#define TARGET_OS_EMBEDDED      0

#if defined(__arm64__)
#define TARGET_CPU_ARM64        1
#define TARGET_CPU_X86_64       0
#elif defined(__x86_64__)
#define TARGET_CPU_ARM64        0
#define TARGET_CPU_X86_64       1
#else
#error "NeoDarwin TargetConditionals.h: unsupported architecture"
#endif

#endif /* _ND_TARGETCONDITIONALS_H_ */
