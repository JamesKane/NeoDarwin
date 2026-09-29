// SPDX-License-Identifier: BSD-2-Clause
// NeoDarwin-Language: expressibility: a compat header for Apple's C file_cmds, which includes it.
//
// Libc's private get_compat.h: COMPAT_MODE(func, mode) asks whether a
// command runs in a conformance mode, which on macOS is UNIX2003 unless
// COMMAND_MODE=legacy. NeoDarwin has only the UNIX2003 behaviour.
#ifndef ND_GET_COMPAT_H
#define ND_GET_COMPAT_H
#define COMPAT_MODE(func, mode) 1
#endif
