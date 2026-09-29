// SPDX-License-Identifier: BSD-2-Clause
// NeoDarwin-Language: expressibility: open(2) is variadic and stdout is a macro; Embedded Swift can call neither.
#include <fcntl.h>
#include <stdio.h>
#include <unistd.h>

#include "libsystem.h"

bool
nd_attach_console(void)
{
	int console = open("/dev/console", O_RDWR);
	if (console < 0) {
		return false;
	}
	for (int fd = 0; fd <= 2; fd++) {
		if (fd != console && dup2(console, fd) < 0) {
			return false;
		}
	}
	setvbuf(stdout, NULL, _IONBF, 0);
	return true;
}
