/*
 * SPDX-License-Identifier: BSD-2-Clause
 * NeoDarwin-Language: expressibility: a C header standing in for one from Apple's internal SDK.
 *
 * Stands in for the internal SDK's os/alloc_once_private.h: the slot numbers
 * of libSystem's once-allocated globals (_os_alloc_once_table, sized
 * OS_ALLOC_ONCE_KEY_MAX = 100 by libsyscall's os/alloc_once.c). A key is only
 * an index agreed between the libraries that share the table, and NeoDarwin
 * builds all of them, so these values need only be distinct and below the
 * table size. Keys are named as libplatform, libpthread and Libc use them.
 */
#ifndef __OS_ALLOC_ONCE_PRIVATE__
#define __OS_ALLOC_ONCE_PRIVATE__

#define OS_ALLOC_ONCE_KEY_LIBSYSTEM_NOTIFY        0
#define OS_ALLOC_ONCE_KEY_LIBSYSTEM_C             1
#define OS_ALLOC_ONCE_KEY_LIBSYSTEM_PTHREAD       2
#define OS_ALLOC_ONCE_KEY_OS_TRACE                3
#define OS_ALLOC_ONCE_KEY_OS_DEBUG_LOG            4
#define OS_ALLOC_ONCE_KEY_LIBSYSTEM_PLATFORM_ASL  5
#define OS_ALLOC_ONCE_KEY_LIBSYSTEM_INFO          6
#define OS_ALLOC_ONCE_KEY_LIBXPC                  7
#define OS_ALLOC_ONCE_KEY_LIBDISPATCH             8

#define __OS_ALLOC_INDIRECT__ 1
#include <os/alloc_once_impl.h>

#endif /* __OS_ALLOC_ONCE_PRIVATE__ */
