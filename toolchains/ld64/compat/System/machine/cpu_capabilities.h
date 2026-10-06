/*
 * SPDX-License-Identifier: BSD-2-Clause
 * NeoDarwin-Language: portability: a C header standing in for one from Apple's internal SDK, included by ld64's OutputFile.cpp.
 * The one commpage field ld64 reads, on x86-64 only (whether it runs under
 * Rosetta), with the values of xnu's osfmk/i386/cpu_capabilities.h
 * (xnu-12377.1.9). The arm64 build doesn't read it.
 */
#ifndef ND_LD64_CPU_CAPABILITIES_H
#define ND_LD64_CPU_CAPABILITIES_H
#if defined(__x86_64__)
#define _COMM_PAGE_START_ADDRESS      (0x00007fffffe00000ULL)
#define _COMM_PAGE_CPU_CAPABILITIES64 (_COMM_PAGE_START_ADDRESS + 0x010)
#define kIsTranslated                 0x4000000000000000ULL
#endif
#endif
