// SPDX-License-Identifier: BSD-2-Clause
// NeoDarwin-Language: portability: libc++'s overridable abort hook, defined under its C++ symbol name from C.
//
// std::__libcpp_verbose_abort for libdyld. Without exceptions, libc++'s
// headers report a failed check (an out-of-range at(), for one) by calling
// it; libc++ documents it as a function a program may define itself. dyld
// defines its own (dyld/glue.c), and libdyld's Debug build defines one in
// libdyld/threadLocalHelpers.s; Apple's Release libdyld needs none, and
// NeoDarwin's libc++ 19 headers do. Defined here, hidden, it keeps libdyld
// from linking libc++, which Apple's doesn't. It reports as dyld's does, through
// Libc's abort_report_np().

#include <libc_private.h>
#include <stdarg.h>
#include <stdio.h>

__attribute__((noreturn, visibility("hidden"))) void
nd_libcpp_verbose_abort(const char *format, ...) __asm__("__ZNSt3__122__libcpp_verbose_abortEPKcz");

void
nd_libcpp_verbose_abort(const char *format, ...)
{
	char message[256];
	va_list ap;
	va_start(ap, format);
	vsnprintf(message, sizeof(message), format, ap);
	va_end(ap);
	abort_report_np("%s", message);
}
