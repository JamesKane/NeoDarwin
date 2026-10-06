// SPDX-License-Identifier: BSD-2-Clause
// NeoDarwin-Language: expressibility: the C declaration of the Swift entry point the kext's C++ IOService calls (a C header is the boundary, language policy §3)
#ifndef NDSWIFT_TRIAL_H
#define NDSWIFT_TRIAL_H
#include <stddef.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
// Trial.swift: walks a record table, calling report(tag, value) for each
// result; 0 when it walks cleanly, -1 if truncated, -2 on a bad checksum.
int32_t ndswift_trial_run(const uint8_t *base, ptrdiff_t count,
    void (*report)(const uint8_t *tag, uint64_t value));
// Trial.swift: adds 41 to *counter with OSAddAtomic, IOSleeps 1 ms, reads
// the system clock; returns the counter's new value (-1 if the clock is 0).
int32_t ndswift_trial_kpi(int32_t *counter);
#ifdef __cplusplus
}
#endif
#endif
