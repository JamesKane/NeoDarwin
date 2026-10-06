/*
 * SPDX-License-Identifier: BSD-2-Clause
 * NeoDarwin-Language: portability: a C header standing in for one from Apple's internal SDK, included by ld64's ld.cpp and OutputFile.cpp.
 * The os_lock_unfair spelling ld64 uses, over the public os_unfair_lock.
 */
#ifndef ND_LD64_OS_LOCK_PRIVATE_H
#define ND_LD64_OS_LOCK_PRIVATE_H
#include <os/lock.h>
typedef os_unfair_lock os_lock_unfair_s;
#define OS_LOCK_UNFAIR_INIT OS_UNFAIR_LOCK_INIT
static inline void os_lock_lock(os_lock_unfair_s *l) { os_unfair_lock_lock(l); }
static inline void os_lock_unlock(os_lock_unfair_s *l) { os_unfair_lock_unlock(l); }
#endif
