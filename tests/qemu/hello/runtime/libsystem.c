// SPDX-License-Identifier: BSD-2-Clause
// NeoDarwin-Language: expressibility: open(2) is variadic, stdout is a macro, and libdispatch's C API is marked unavailable to Swift in favour of the Dispatch overlay; Embedded Swift can call none of them.
#include <dispatch/dispatch.h>
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
	return true;
}

void
nd_unbuffer_stdout(void)
{
	setvbuf(stdout, NULL, _IONBF, 0);
}

static void
signal_semaphore(void *context)
{
	dispatch_semaphore_signal((dispatch_semaphore_t)context);
}

long
nd_dispatch_roundtrip(void)
{
	dispatch_semaphore_t done = dispatch_semaphore_create(0);
	dispatch_async_f(dispatch_get_global_queue(QOS_CLASS_DEFAULT, 0), done, signal_semaphore);
	long result = dispatch_semaphore_wait(done, dispatch_time(DISPATCH_TIME_NOW, 5 * NSEC_PER_SEC));
	dispatch_release(done);
	return result;
}
