/*
 * SPDX-License-Identifier: BSD-2-Clause
 * NeoDarwin-Language: expressibility: a C header standing in for one Apple publishes empty.
 *
 * xnu's libsyscall publishes os/thread_self_restrict.h with its contents
 * removed. It is the interface to Apple silicon's per-thread write/execute
 * permission switching for JIT regions, which generic Arm does not have.
 * libpthread asks whether the feature exists before using it; here it never
 * does, and the switches do nothing.
 */
#ifndef OS_THREAD_SELF_RESTRICT_H
#define OS_THREAD_SELF_RESTRICT_H

#include <stdbool.h>

static inline bool os_thread_self_restrict_rwx_is_supported(void) { return false; }
static inline void os_thread_self_restrict_rwx_to_rw(void) {}
static inline void os_thread_self_restrict_rwx_to_rx(void) {}

#endif /* OS_THREAD_SELF_RESTRICT_H */
