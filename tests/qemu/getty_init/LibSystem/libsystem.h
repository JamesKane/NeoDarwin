// SPDX-License-Identifier: BSD-2-Clause
// NeoDarwin-Language: expressibility: a Swift module over libSystem's C headers is declared by a C header.
//
// The libSystem interfaces getty_init uses: the C headers, and the one
// function of runtime/libsystem.c.
#ifndef GETTY_INIT_LIBSYSTEM_H
#define GETTY_INIT_LIBSYSTEM_H
#include <errno.h>
#include <spawn.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

// Opens /dev/console as standard input, output and error (PID 1 starts
// without open files) and makes stdout unbuffered; false if it cannot.
bool nd_attach_console(void);
#endif
