/*
 * SPDX-License-Identifier: BSD-2-Clause
 * NeoDarwin-Language: expressibility: a C header standing in for one from Apple's internal SDK.
 *
 * Stands in for the internal SDK's CrashReporterClient.h. Apple's header is
 * header-only: the CR* calls are macros over gCRAnnotations, a hidden
 * per-image structure in the __DATA,__crash_info section that the crash
 * reporter reads out of a dead process's images. The structure itself comes
 * from libCrashReporterClient.a, a static archive every client links, so each
 * image carries its own copy. Here the header also supplies that copy, as a
 * weak hidden definition the linker coalesces to one per image, so clients
 * need no archive. Libc, libplatform (os/crashlog_private.h), libnotify and
 * objc4 record their crash messages through it.
 *
 * The layout is version 5 of the structure, as Apple's crash reporter reads it.
 */
#ifndef _CRASHREPORTERCLIENT_H_
#define _CRASHREPORTERCLIENT_H_

#include <stdint.h>
#include <sys/cdefs.h>

#define CRASHREPORTER_ANNOTATIONS_SECTION "__crash_info"
#define CRASHREPORTER_ANNOTATIONS_VERSION 5
#define CRASH_REPORTER_CLIENT_HIDDEN __attribute__((visibility("hidden")))

__BEGIN_DECLS

struct crashreporter_annotations_t {
	uint64_t version;          // CRASHREPORTER_ANNOTATIONS_VERSION
	uint64_t message;          // const char *
	uint64_t signature_string; // const char *
	uint64_t backtrace;        // const char *
	uint64_t message2;         // const char *
	uint64_t thread;           // uint64_t
	uint64_t dialog_mode;      // unsigned int
	uint64_t abort_cause;      // unsigned int
};

CRASH_REPORTER_CLIENT_HIDDEN __attribute__((weak, section("__DATA," CRASHREPORTER_ANNOTATIONS_SECTION)))
struct crashreporter_annotations_t gCRAnnotations = { CRASHREPORTER_ANNOTATIONS_VERSION, 0, 0, 0, 0, 0, 0, 0 };

__END_DECLS

#define _crc_make_getter(attr) ((const char *)(unsigned long)gCRAnnotations.attr)
#define _crc_make_setter(attr, arg) (gCRAnnotations.attr = (uint64_t)(unsigned long)(arg))

#define CRGetCrashLogMessage() _crc_make_getter(message)
#define CRSetCrashLogMessage(m) _crc_make_setter(message, m)
#define CRGetCrashLogMessage2() _crc_make_getter(message2)
#define CRSetCrashLogMessage2(m) _crc_make_setter(message2, m)
#define CRGetCrashLogSignature() _crc_make_getter(signature_string)
#define CRSetCrashLogSignature(m) _crc_make_setter(signature_string, m)
#define CRSetCrashLogBacktrace(m) _crc_make_setter(backtrace, m)
#define CRSetCrashLogThread(t) (gCRAnnotations.thread = (uint64_t)(t))
#define CRSetCrashLogAbortCause(c) (gCRAnnotations.abort_cause = (uint64_t)(c))

#endif /* _CRASHREPORTERCLIENT_H_ */
