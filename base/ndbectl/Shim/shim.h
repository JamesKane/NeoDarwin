// SPDX-License-Identifier: BSD-2-Clause
// NeoDarwin-Language: expressibility: a Swift module over libSystem's C headers is declared by a C header.
// The libSystem interfaces ndbectl uses, for Embedded Swift (language
// policy T3: NeoDarwin has no Swift runtime yet, so there is no Darwin
// overlay to import). Only system headers; no C of our own.
#ifndef NDBECTL_SHIM_H
#define NDBECTL_SHIM_H
#include <errno.h>
#include <spawn.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mount.h>
#include <sys/param.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>
#endif
