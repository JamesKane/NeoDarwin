// SPDX-License-Identifier: BSD-2-Clause
// NeoDarwin-Language: portability: a stand-in with the C interface of a closed Apple library.
//
// Stand-in for libsystem_trace.dylib (os_log), which Apple does not publish.
// NeoDarwin has no log store yet, so a log message goes to standard error.
// Libc uses three things (base/sdk/usr/local/include/os/log_private.h has the
// private declarations):
//  - _os_log_default, the object behind OS_LOG_DEFAULT;
//  - _os_log_send_and_compose_impl(), behind os_log_send_and_compose();
//  - os_log_pack_send_and_compose(), for a log call captured as a "pack".
//    Decoding a pack's arguments needs libsystem_trace's buffer format, which
//    is not published; the stand-in composes the pack's format string alone.
// os_log format strings may carry privacy annotations after the '%'
// ("%{public}s"), which printf does not understand; they are removed before
// formatting. More of libsystem_trace is added as NeoDarwin libraries come to
// need it (docs/base/libsystem.md).

#include <os/log.h>
#include <os/log_private.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

struct os_log_s {
	int nd_unused;
};
struct os_log_s _os_log_default;

// Copies fmt into out (size bytes) without os_log's "{...}" annotations.
static void
nd_strip_annotations(const char *fmt, char *out, size_t size)
{
	size_t o = 0;
	for (const char *p = fmt; *p != '\0' && o + 1 < size; p++) {
		out[o++] = *p;
		if (*p == '%' && p[1] == '{') {
			const char *close = strchr(p + 2, '}');
			if (close != NULL) {
				p = close;
			}
		}
	}
	out[o] = '\0';
}

static void
nd_emit(const char *msg)
{
	(void)write(STDERR_FILENO, msg, strlen(msg));
	(void)write(STDERR_FILENO, "\n", 1);
}

char *
_os_log_send_and_compose_impl(uint32_t flags, const char **fmt_out, char *buf, size_t bufsz,
    os_log_t log, os_log_type_t type, const char *fmt, ...)
{
	(void)log;
	(void)type;
	char plain[1024];
	nd_strip_annotations(fmt, plain, sizeof(plain));
	if (fmt_out != NULL) {
		*fmt_out = fmt;
	}
	char local[1024];
	char *out = (flags & OS_LOG_F_COMPOSE) && buf != NULL && bufsz > 0 ? buf : local;
	size_t outsz = out == buf ? bufsz : sizeof(local);
	va_list ap;
	va_start(ap, fmt);
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wformat-nonliteral"
	vsnprintf(out, outsz, plain, ap);
#pragma clang diagnostic pop
	va_end(ap);
	if (flags & OS_LOG_F_SEND) {
		nd_emit(out);
	}
	return out == buf ? buf : NULL;
}

char *
os_log_pack_send_and_compose(os_log_pack_t pack, os_log_t log, os_log_type_t type, char *buf, size_t size)
{
	(void)log;
	(void)type;
	if (pack == NULL || pack->olp_format == NULL) {
		return NULL;
	}
	char *out = buf;
	char local[1024];
	if (out == NULL || size == 0) {
		out = local;
		size = sizeof(local);
	}
	nd_strip_annotations(pack->olp_format, out, size);
	nd_emit(out);
	return out == buf ? buf : NULL;
}
