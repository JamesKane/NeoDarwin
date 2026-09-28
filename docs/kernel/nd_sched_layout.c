// SPDX-License-Identifier: BSD-2-Clause
// NeoDarwin-Language: portability: compile-checks the C ABI records of nd_sched.h (spec-conventions.md §5)
/*
 * nd_sched_layout.c - layout check for docs/kernel/nd_sched.h (scheduling
 * contract, API version 1). Every record that crosses the user/kernel
 * boundary has its size and the offset of every field asserted here, and
 * main() exercises the ticket accessors, so `bazel test //docs/kernel/...`
 * runs them. Conformance test SC-T-001 in scheduling-contract.md.
 */
#include "docs/kernel/nd_sched.h"

#define OFF(T, f, n) static_assert(offsetof(T, f) == (n), #T "." #f " must be at offset " #n)
#define SIZE(T, n)   static_assert(sizeof(T) == (n), #T " must be " #n " bytes")

static_assert(SC_API_VERSION == 1u, "API version 1");

/* enums have the declared fixed width */
static_assert(sizeof(enum sc_error) == 4, "sc_error is 32-bit");
static_assert(sizeof(enum sc_cap) == 8, "sc_cap is 64-bit");
static_assert(sizeof(enum sc_intent) == 4, "sc_intent is 32-bit");
static_assert(sizeof(enum sc_rt_reason) == 4, "sc_rt_reason is 32-bit");
static_assert(sizeof(sc_ticket) == 8, "tickets are u64 handles");

SIZE(sc_intent_info, 56);
OFF(sc_intent_info, size, 0); OFF(sc_intent_info, intent, 4); OFF(sc_intent_info, qos, 8);
OFF(sc_intent_info, cluster, 12); OFF(sc_intent_info, timer_slop_ns, 16);
OFF(sc_intent_info, rt_ticket, 24); OFF(sc_intent_info, rt_state, 32);
OFF(sc_intent_info, reserved0, 36); OFF(sc_intent_info, reserved, 40);

SIZE(sc_rt_request, 64);
OFF(sc_rt_request, size, 0); OFF(sc_rt_request, flags, 4); OFF(sc_rt_request, period_ns, 8);
OFF(sc_rt_request, computation_ns, 16); OFF(sc_rt_request, constraint_ns, 24);
OFF(sc_rt_request, udata, 32); OFF(sc_rt_request, reserved, 40);

SIZE(sc_rt_refusal, 48);
OFF(sc_rt_refusal, size, 0); OFF(sc_rt_refusal, reason, 4); OFF(sc_rt_refusal, max_computation_ns, 8);
OFF(sc_rt_refusal, available_ppm, 16); OFF(sc_rt_refusal, budget_ppm, 20); OFF(sc_rt_refusal, reserved, 24);

SIZE(sc_rt_status, 96);
OFF(sc_rt_status, size, 0); OFF(sc_rt_status, state, 4); OFF(sc_rt_status, period_ns, 8);
OFF(sc_rt_status, computation_ns, 16); OFF(sc_rt_status, constraint_ns, 24);
OFF(sc_rt_status, releases, 32); OFF(sc_rt_status, overruns, 40); OFF(sc_rt_status, misses, 48);
OFF(sc_rt_status, max_used_ns, 56); OFF(sc_rt_status, threads, 64); OFF(sc_rt_status, density_ppm, 68);
OFF(sc_rt_status, reserved, 72);

SIZE(sc_rt_notice, 64);
OFF(sc_rt_notice, size, 0); OFF(sc_rt_notice, kind, 4); OFF(sc_rt_notice, ticket, 8);
OFF(sc_rt_notice, udata, 16); OFF(sc_rt_notice, first_ns, 24); OFF(sc_rt_notice, count, 32);
OFF(sc_rt_notice, reserved, 40);

/* every record is 8-byte aligned so arrays of them need no padding rules */
static_assert(alignof(sc_rt_status) == 8, "8-byte records");
static_assert(alignof(sc_rt_notice) == 8, "8-byte records");

/* ticket accessors (charter P5) */
static_assert(SC_TICKET_INDEX(SC_TICKET_MAKE(5, 7)) == 5u, "ticket index");
static_assert(SC_TICKET_GEN(SC_TICKET_MAKE(5, 7)) == 7u, "ticket generation");
static_assert(!SC_TICKET_VALID(SC_TICKET_NONE), "ticket 0 is never valid");
static_assert(SC_TICKET_VALID(SC_TICKET_MAKE(0, 1)), "index 0, generation 1 is valid");
static_assert(SC_TICKET_MAKE(0xffffffffu, 0xffffffffu) == UINT64_MAX, "full range");

/* flags and capability bits are distinct */
static_assert((SC_RT_F_SYSTEM & SC_RT_F_RESERVE_ONLY) == 0, "distinct request flags");
static_assert(SC_CAP_RT_DEADLINE == (1ull << 9), "version 1 defines capability bits 0-9");

/* the contract timer flags */
static_assert(SC_TIMER_FFLAGS == 0x118u, "NOTE_ABSOLUTE|NOTE_MACHTIME|NOTE_LEEWAY");
static_assert(SC_WAKE_IDENT != SC_DEADLINE_IDENT, "distinct reserved idents");

/* policy defaults are self-consistent */
static_assert(SC_RT_MIN_COMPUTATION_NS < SC_RT_MAX_COMPUTATION_NS, "C range");
static_assert(SC_RT_MAX_COMPUTATION_NS <= SC_RT_MAX_PERIOD_NS, "C <= T possible");
static_assert(SC_RT_SYSTEM_SHARE_PPM < SC_RT_DOMAIN_CAP_PPM, "system share inside the cap");
static_assert(SC_RT_DEMOTE_OVERRUNS <= SC_RT_DEMOTE_WINDOW, "demotion window");

/* On a Darwin host, cross-check the repeated XNU values against the SDK. */
#if __has_include(<sys/event.h>) && defined(__MACH__)
#include <sys/event.h>
static_assert(SC_NOTE_ABSOLUTE == NOTE_ABSOLUTE, "NOTE_ABSOLUTE");
static_assert(SC_NOTE_LEEWAY == NOTE_LEEWAY, "NOTE_LEEWAY");
static_assert(SC_NOTE_CRITICAL == NOTE_CRITICAL, "NOTE_CRITICAL");
static_assert(SC_NOTE_BACKGROUND == NOTE_BACKGROUND, "NOTE_BACKGROUND");
static_assert(SC_NOTE_MACH_CONTINUOUS_TIME == NOTE_MACH_CONTINUOUS_TIME, "NOTE_MACH_CONTINUOUS_TIME");
static_assert(SC_NOTE_MACHTIME == NOTE_MACHTIME, "NOTE_MACHTIME");
static_assert(offsetof(struct kevent64_s, ext) == 32, "kevent64_s.ext[0] carries the fire time");
#endif

int main(void)
{
	/* runtime round trip of the accessors, so the test is not vacuous */
	volatile uint32_t idx = 12345u, gen = 3u;
	sc_ticket t = SC_TICKET_MAKE(idx, gen);
	if (SC_TICKET_INDEX(t) != idx || SC_TICKET_GEN(t) != gen || !SC_TICKET_VALID(t))
		return 1;
	sc_rt_request r = { .size = sizeof r, .period_ns = 2666667, .computation_ns = 500000 };
	if (r.constraint_ns != 0 || r.reserved[2] != 0)
		return 2;
	return 0;
}
