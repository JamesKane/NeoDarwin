/*
 * SPDX-License-Identifier: BSD-2-Clause
 * NeoDarwin-Language: portability: a C header standing in for one from Apple's internal SDK, included by ld64's Options.cpp.
 * The crash-report annotation ld64 fills with its command line: version 4
 * of the __DATA,__crash_info record, which ReportCrash reads.
 */
#ifndef ND_LD64_CRASHREPORTERCLIENT_H
#define ND_LD64_CRASHREPORTERCLIENT_H
#include <stdint.h>
#define CRASHREPORTER_ANNOTATIONS_SECTION "__crash_info"
#define CRASHREPORTER_ANNOTATIONS_VERSION 4
struct crashreporter_annotations_t {
	uint64_t version;
	uint64_t message;
	uint64_t signature_string;
	uint64_t backtrace;
	uint64_t message2;
	uint64_t thread;
	uint64_t dialog_mode;
};
extern struct crashreporter_annotations_t gCRAnnotations;
#define CRSetCrashLogMessage(m) (gCRAnnotations.message = (uint64_t)(uintptr_t)(m))
#endif
