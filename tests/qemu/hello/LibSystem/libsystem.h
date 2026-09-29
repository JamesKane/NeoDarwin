// SPDX-License-Identifier: BSD-2-Clause
// NeoDarwin-Language: expressibility: a Swift module over libSystem's C headers is declared by a C header.
//
// The libSystem interfaces the dynamic hello world uses: the C headers, and
// the three functions of runtime/libsystem.c, for what Swift cannot call.
#ifndef HELLO_LIBSYSTEM_H
#define HELLO_LIBSYSTEM_H
#include <dlfcn.h>
#include <stdbool.h>
#include <mach-o/dyld.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

// Opens /dev/console as standard input, output and error (PID 1 starts
// without open files); false if it cannot.
bool nd_attach_console(void);

// Makes stdout unbuffered, so each line reaches the console as it is printed.
void nd_unbuffer_stdout(void);

// Runs one function on a libdispatch global queue and waits for it for up to
// five seconds; returns 0 when it ran, as dispatch_semaphore_wait() does.
long nd_dispatch_roundtrip(void);
#endif
