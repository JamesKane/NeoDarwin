// SPDX-License-Identifier: BSD-2-Clause
// NeoDarwin-Language: portability: a stand-in with the C interface of a closed Apple library.
//
// Stand-in for libsystem_trace.dylib (os_log and os_activity), which Apple
// does not publish. NeoDarwin has no log store yet, so a log message is
// formatted and written to standard error, one line per message. What the
// open libraries use (base/sdk/usr/local/include/os/log_private.h declares
// the private calls):
//  - log objects: _os_log_default (OS_LOG_DEFAULT) and os_log_create();
//    os_log_type_enabled() reports info and debug messages as off and the
//    other types as on, os_log's defaults;
//  - the os_log() calls, _os_log_impl() and _os_log_{debug,error,fault}_impl(),
//    and the log "pack" (a captured os_log() call,
//    os_log_pack_send_and_compose()). Both
//    carry their arguments in the buffer __builtin_os_log_format() encodes,
//    whose layout clang defines (clang/include/clang/AST/OSLog.h); the
//    stand-in decodes it and formats the message as os_log would;
//  - _os_log_send_and_compose_impl(), which base/sdk's os_log_send_and_compose()
//    calls with its arguments as C varargs;
//  - the syslog shim: os_log_shim_enabled() routes every syslog(3) and asl(3)
//    message to os_log_with_args_4syslog(), as on macOS;
//  - _os_trace_basesystem_storage_available(), which libsystem_darwin asks
//    whether a base system (the installer environment) can keep full logs:
//    standard error is always available;
//  - os_activity: _os_activity_current (OS_ACTIVITY_CURRENT) and
//    os_activity_get_identifier(). There are no activities, so every
//    identifier is 0, os_activity's "none";
//  - libSystem's initializer and fork hook (_libtrace_init(),
//    _libtrace_fork_child()). Apple's initializer reads the process's logging
//    preferences and connects to logd; the stand-in's settings are fixed and
//    it writes to standard error with no lock or connection, so both do
//    nothing.
// Privacy annotations ("%{private}s") are honoured by the log store, which
// NeoDarwin lacks; arguments are always shown. More of libsystem_trace is
// added as NeoDarwin libraries come to need it (docs/base/libsystem.md).

#include <errno.h>
#include <os/activity.h>
#include <os/log.h>
#include <os/log_private.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

struct os_log_s {
	const char *subsystem;
	const char *category;
};
struct os_log_s _os_log_default;

struct os_activity_s {
	int nd_unused;
};
struct os_activity_s _os_activity_current;

// The line length a message is cut to.
enum { ND_LINE = 1024 };

#pragma mark - The os_log argument buffer

// Item kinds and the buffer layout, from clang's OSLogBufferLayout: a summary
// byte and an item count, then per item a descriptor byte (kind << 4 | privacy
// flags), a size byte and the data.
enum nd_kind {
	ND_SCALAR = 0,
	ND_COUNT = 1,
	ND_STRING = 2,
	ND_POINTER = 3,
	ND_OBJC = 4,
	ND_WIDE_STRING = 5,
	ND_ERRNO = 6,
	ND_MASK = 7,
};

struct nd_items {
	const uint8_t *p;
	const uint8_t *end;
	unsigned left;
};

struct nd_item {
	enum nd_kind kind;
	uint8_t size;
	const uint8_t *data;
};

static bool
nd_next(struct nd_items *it, struct nd_item *out)
{
	if (it->left == 0 || it->end - it->p < 2) {
		return false;
	}
	out->kind = (enum nd_kind)(it->p[0] >> 4);
	out->size = it->p[1];
	out->data = it->p + 2;
	if (it->end - out->data < out->size) {
		return false;
	}
	it->p = out->data + out->size;
	it->left--;
	return true;
}

// An integer item's value: 1, 2, 4 or 8 bytes, sign-extended when is_signed.
static int64_t
nd_int(const struct nd_item *item, bool is_signed)
{
	switch (item->size) {
	case 1: { uint8_t v; memcpy(&v, item->data, 1); return is_signed ? (int8_t)v : v; }
	case 2: { uint16_t v; memcpy(&v, item->data, 2); return is_signed ? (int16_t)v : v; }
	case 4: { uint32_t v; memcpy(&v, item->data, 4); return is_signed ? (int32_t)v : v; }
	case 8: { int64_t v; memcpy(&v, item->data, 8); return v; }
	default: return 0;
	}
}

static const void *
nd_ptr(const struct nd_item *item)
{
	const void *v = NULL;
	if (item->size == sizeof(v)) {
		memcpy(&v, item->data, sizeof(v));
	}
	return v;
}

struct nd_out {
	char *buf;
	size_t size;
	size_t len;
};

static void
nd_put(struct nd_out *o, const char *s, size_t n)
{
	for (size_t i = 0; i < n && o->len + 1 < o->size; i++) {
		o->buf[o->len++] = s[i];
	}
	o->buf[o->len] = '\0';
}

#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wformat-nonliteral"
static void
nd_putf(struct nd_out *o, const char *spec, ...)
{
	if (o->len + 1 >= o->size) {
		return;
	}
	va_list ap;
	va_start(ap, spec);
	int n = vsnprintf(o->buf + o->len, o->size - o->len, spec, ap);
	va_end(ap);
	if (n > 0) {
		o->len += (size_t)n < o->size - o->len ? (size_t)n : o->size - o->len - 1;
	}
}
#pragma clang diagnostic pop

// Formats fmt with the arguments encoded in buf (size bytes) into out. A
// conversion whose item is missing or of the wrong kind ends the decoding;
// the rest of fmt is copied as it is.
static void
nd_compose(char *out, size_t outsz, const char *fmt, const uint8_t *buf, size_t size)
{
	struct nd_out o = { out, outsz, 0 };
	struct nd_items it = { buf != NULL && size >= 2 ? buf + 2 : NULL, buf != NULL ? buf + size : NULL,
		buf != NULL && size >= 2 ? buf[1] : 0 };
	bool decoding = true;
	int saved_errno = errno;
	out[0] = '\0';

	for (const char *p = fmt; *p != '\0';) {
		if (*p != '%' || !decoding) {
			nd_put(&o, p++, 1);
			continue;
		}
		const char *start = p++;
		if (*p == '%') {
			nd_put(&o, "%", 1);
			p++;
			continue;
		}
		bool mask = false;
		if (*p == '{') {   // "{public}", "{private, mask.hash}", "{bool}", ...
			const char *close = strchr(p, '}');
			if (close == NULL) {
				decoding = false;
				p = start;
				continue;
			}
			for (const char *q = p; q + 5 <= close; q++) {
				mask = mask || strncmp(q, "mask.", 5) == 0;
			}
			p = close + 1;
		}
		char flags[8] = "";
		size_t nflags = 0;
		while (strchr("-+ #0'", *p) != NULL && *p != '\0' && nflags + 1 < sizeof(flags)) {
			flags[nflags++] = *p++;
		}
		bool width_arg = false, prec_arg = false, has_prec = false;
		int width = -1, prec = -1;
		if (*p == '*') {
			width_arg = true;
			p++;
		} else if (*p >= '0' && *p <= '9') {
			width = (int)strtol(p, (char **)&p, 10);
		}
		if (*p == '.') {
			has_prec = true;
			p++;
			if (*p == '*') {
				prec_arg = true;
				p++;
			} else {
				prec = (int)strtol(p, (char **)&p, 10);
			}
		}
		char length[3] = "";
		size_t nlength = 0;
		while (strchr("hlqjztL", *p) != NULL && *p != '\0' && nlength + 1 < sizeof(length)) {
			length[nlength++] = *p++;
		}
		char conv = *p;
		if (conv == '\0') {
			break;
		}
		p++;

		// The items this conversion consumes, in clang's order: mask,
		// field width, precision (or a string's count), then the value.
		struct nd_item item;
		bool ok = true;
		if (mask) {
			ok = nd_next(&it, &item) && item.kind == ND_MASK;
		}
		if (ok && width_arg) {
			ok = nd_next(&it, &item) && item.kind == ND_SCALAR;
			width = ok ? (int)nd_int(&item, true) : -1;
		}
		bool counted = conv == 's' || conv == 'S' || conv == 'P';
		if (ok && counted && has_prec) {
			ok = nd_next(&it, &item) && item.kind == ND_COUNT;
			prec = ok ? (int)nd_int(&item, true) : -1;
		} else if (ok && prec_arg) {
			ok = nd_next(&it, &item) && item.kind == ND_SCALAR;
			prec = ok ? (int)nd_int(&item, true) : -1;
		}
		if (ok && conv != 'm') {
			ok = nd_next(&it, &item);
		} else if (ok) {
			ok = nd_next(&it, &item) && item.kind == ND_ERRNO;
		}
		if (!ok) {
			decoding = false;
			p = start;
			continue;
		}

		// Rebuild a printf conversion with the width and precision resolved.
		char spec[48];
		int n = snprintf(spec, sizeof(spec), "%%%s", flags);
		if (width >= 0) {
			n += snprintf(spec + n, sizeof(spec) - (size_t)n, "%d", width);
		}
		if (prec >= 0) {
			n += snprintf(spec + n, sizeof(spec) - (size_t)n, ".%d", prec);
		}
		switch (conv) {
		case 'd': case 'i': case 'o': case 'u': case 'x': case 'X': case 'c': {
			bool is_signed = conv == 'd' || conv == 'i';
			int64_t v = nd_int(&item, is_signed);
			if (conv == 'c' || strcmp(length, "h") == 0 || strcmp(length, "hh") == 0) {
				snprintf(spec + n, sizeof(spec) - (size_t)n, "%s%c", conv == 'c' ? "" : length, conv);
				nd_putf(&o, spec, (int)v);
			} else {
				snprintf(spec + n, sizeof(spec) - (size_t)n, "ll%c", conv);
				nd_putf(&o, spec, (long long)v);
			}
			break;
		}
		case 'e': case 'E': case 'f': case 'F': case 'g': case 'G': case 'a': case 'A': {
			double v = 0;
			if (item.size == sizeof(v)) {
				memcpy(&v, item.data, sizeof(v));
			}
			snprintf(spec + n, sizeof(spec) - (size_t)n, "%c", conv);
			nd_putf(&o, spec, v);
			break;
		}
		case 'p':
			snprintf(spec + n, sizeof(spec) - (size_t)n, "p");
			nd_putf(&o, spec, nd_ptr(&item));
			break;
		case 's': case 'S': {
			const void *s = nd_ptr(&item);
			snprintf(spec + n, sizeof(spec) - (size_t)n, conv == 's' ? "s" : "ls");
			nd_putf(&o, spec, s != NULL ? s : (conv == 's' ? (const void *)"(null)" : (const void *)L"(null)"));
			break;
		}
		case 'P': {   // prec bytes of raw data, in hex
			const uint8_t *d = nd_ptr(&item);
			for (int i = 0; d != NULL && i < prec; i++) {
				nd_putf(&o, "%02x", d[i]);
			}
			break;
		}
		case '@':
			nd_putf(&o, "<object %p>", nd_ptr(&item));
			break;
		case 'm':
			nd_putf(&o, "%s", strerror(saved_errno));
			break;
		default:   // %C, %n and the like: skip the value
			break;
		}
	}
}

#pragma mark - Output

static void
nd_emit(os_log_t log, const char *msg)
{
	char line[ND_LINE + 128];
	int n;
	if (log != NULL && log->subsystem != NULL) {
		n = snprintf(line, sizeof(line), "[%s:%s] %s\n", log->subsystem,
		    log->category != NULL ? log->category : "", msg);
	} else {
		n = snprintf(line, sizeof(line), "%s\n", msg);
	}
	if (n > 0) {
		(void)write(STDERR_FILENO, line, (size_t)n < sizeof(line) ? (size_t)n : sizeof(line) - 1);
	}
}

// Copies fmt into out (size bytes) without os_log's "{...}" annotations,
// replacing %m with the error string for err, for formatting with printf.
static void
nd_plain_format(const char *fmt, char *out, size_t size, int err)
{
	size_t o = 0;
	for (const char *p = fmt; *p != '\0' && o + 1 < size; p++) {
		if (p[0] == '%' && p[1] == 'm') {
			for (const char *e = strerror(err); *e != '\0' && o + 2 < size; e++) {
				if (*e == '%') {
					out[o++] = '%';
				}
				out[o++] = *e;
			}
			p++;
			continue;
		}
		out[o++] = *p;
		if (p[0] == '%' && p[1] == '%') {
			if (o + 1 < size) {
				out[o++] = '%';
			}
			p++;
		} else if (p[0] == '%' && p[1] == '{') {
			const char *close = strchr(p + 2, '}');
			if (close != NULL) {
				p = close;
			}
		}
	}
	out[o] = '\0';
}

#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wformat-nonliteral"
static void
nd_vformat(char *out, size_t size, const char *fmt, va_list ap)
{
	char plain[ND_LINE];
	nd_plain_format(fmt, plain, sizeof(plain), errno);
	vsnprintf(out, size, plain, ap);
}
#pragma clang diagnostic pop

#pragma mark - Log objects

os_log_t
os_log_create(const char *subsystem, const char *category)
{
	// Log objects live for the life of the process, as os_log's do.
	struct os_log_s *log = malloc(sizeof(*log));
	if (log == NULL) {
		return &_os_log_default;
	}
	log->subsystem = strdup(subsystem);
	log->category = strdup(category);
	return log;
}

bool
os_log_type_enabled(os_log_t log, os_log_type_t type)
{
	(void)log;
	return type != OS_LOG_TYPE_INFO && type != OS_LOG_TYPE_DEBUG;
}

#pragma mark - Logging

static void
nd_log(os_log_t log, os_log_type_t type, const char *format, const uint8_t *buf, uint32_t size)
{
	if (!os_log_type_enabled(log, type)) {
		return;
	}
	char msg[ND_LINE];
	nd_compose(msg, sizeof(msg), format, buf, size);
	nd_emit(log, msg);
}

void
_os_log_impl(void *dso, os_log_t log, os_log_type_t type, const char *format, uint8_t *buf, uint32_t size)
{
	(void)dso;
	nd_log(log, type, format, buf, size);
}

void
_os_log_debug_impl(void *dso, os_log_t log, os_log_type_t type, const char *format, uint8_t *buf, uint32_t size)
{
	(void)dso;
	nd_log(log, type, format, buf, size);
}

void
_os_log_error_impl(void *dso, os_log_t log, os_log_type_t type, const char *format, uint8_t *buf, uint32_t size)
{
	(void)dso;
	nd_log(log, type, format, buf, size);
}

void
_os_log_fault_impl(void *dso, os_log_t log, os_log_type_t type, const char *format, uint8_t *buf, uint32_t size)
{
	(void)dso;
	nd_log(log, type, format, buf, size);
}

char *
_os_log_send_and_compose_impl(uint32_t flags, const char **fmt_out, char *buf, size_t bufsz,
    os_log_t log, os_log_type_t type, const char *fmt, ...)
{
	(void)type;
	if (fmt_out != NULL) {
		*fmt_out = fmt;
	}
	char local[ND_LINE];
	char *out = (flags & OS_LOG_F_COMPOSE) && buf != NULL && bufsz > 0 ? buf : local;
	size_t outsz = out == buf ? bufsz : sizeof(local);
	va_list ap;
	va_start(ap, fmt);
	nd_vformat(out, outsz, fmt, ap);
	va_end(ap);
	if (flags & OS_LOG_F_SEND) {
		nd_emit(log, out);
	}
	return out == buf ? buf : NULL;
}

char *
os_log_pack_send_and_compose(os_log_pack_t pack, os_log_t log, os_log_type_t type, char *buf, size_t size)
{
	(void)type;
	if (pack == NULL || pack->olp_format == NULL) {
		return NULL;
	}
	char *out = buf;
	char local[ND_LINE];
	if (out == NULL || size == 0) {
		out = local;
		size = sizeof(local);
	}
	// A pack's buffer carries no length; its item count bounds the decoding.
	nd_compose(out, size, pack->olp_format, pack->olp_data, SIZE_MAX / 2);
	nd_emit(log, out);
	return out == buf ? buf : NULL;
}

#pragma mark - The syslog shim

bool
os_log_shim_enabled(void *addr)
{
	(void)addr;
	return true;
}

void
os_log_with_args_4syslog(os_log_t log, os_log_type_t type, const char *fmt, va_list args, void *ret_addr)
{
	(void)ret_addr;
	if (!os_log_type_enabled(log, type)) {
		return;
	}
	char msg[ND_LINE];
	nd_vformat(msg, sizeof(msg), fmt, args);
	nd_emit(log, msg);
}

// Whether a base system can store full logs (libsystem_darwin's os_variant).
bool _os_trace_basesystem_storage_available(void);

bool
_os_trace_basesystem_storage_available(void)
{
	return true;
}

#pragma mark - Activities

os_activity_id_t
os_activity_get_identifier(os_activity_t activity, os_activity_id_t *parent_id)
{
	(void)activity;
	if (parent_id != NULL) {
		*parent_id = 0;
	}
	return 0;
}

#pragma mark - libSystem's initializer and fork hook

// Declared by Libsystem's init.c, which calls them; no header publishes them.
void _libtrace_init(void);
void _libtrace_fork_child(void);

void
_libtrace_init(void)
{
}

void
_libtrace_fork_child(void)
{
}
