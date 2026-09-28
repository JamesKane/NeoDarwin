// SPDX-License-Identifier: BSD-2-Clause
// NeoDarwin-Language: portability: checks that nd_sched.h, a C ABI header, also compiles as C++ for C++ clients (SDL, Qt, Dawn, engines)
/*
 * nd_sched_cxx.cpp - C++20 compile check for docs/kernel/nd_sched.h
 * (SC-T-001 in scheduling-contract.md; spec-conventions.md §5). The header
 * must compile as C++20 with no warnings, with C linkage, and give the same
 * layouts as in C.
 */
#include "docs/kernel/nd_sched.h"

#include <type_traits>

static_assert(sizeof(sc_intent_info) == 56, "sc_intent_info is 56 bytes in C++ too");
static_assert(sizeof(sc_rt_request) == 64, "sc_rt_request is 64 bytes in C++ too");
static_assert(sizeof(sc_rt_refusal) == 48, "sc_rt_refusal is 48 bytes in C++ too");
static_assert(sizeof(sc_rt_status) == 96, "sc_rt_status is 96 bytes in C++ too");
static_assert(sizeof(sc_rt_notice) == 64, "sc_rt_notice is 64 bytes in C++ too");
static_assert(offsetof(sc_rt_status, threads) == 64, "sc_rt_status.threads at 64 in C++ too");
static_assert(offsetof(sc_rt_notice, first_ns) == 24, "sc_rt_notice.first_ns at 24 in C++ too");
static_assert(std::is_same_v<std::underlying_type_t<sc_error>, int32_t>, "sc_error is int32_t");
static_assert(std::is_same_v<std::underlying_type_t<sc_cap>, uint64_t>, "sc_cap is uint64_t");
static_assert(std::is_same_v<std::underlying_type_t<sc_intent>, uint32_t>, "sc_intent is uint32_t");
static_assert(std::is_standard_layout_v<sc_rt_status> && std::is_trivially_copyable_v<sc_rt_status>,
    "records are plain data");
static_assert(SC_TICKET_INDEX(SC_TICKET_MAKE(9, 4)) == 9u && SC_TICKET_GEN(SC_TICKET_MAKE(9, 4)) == 4u,
    "ticket accessors work in C++");
static_assert(SC_9P_TREADY_SIZE == 4 + 1 + 2 + 4 + 4, "Tready wire size");

// Taking the address of every function checks the declarations under C++
// rules (no definitions are linked: this is a compile check).
extern "C++" const void *nd_sched_cxx_check(void);
const void *nd_sched_cxx_check(void)
{
	static const void *const fns[] = {
		reinterpret_cast<const void *>(&nd_wait),
		reinterpret_cast<const void *>(&nd_sched_error_detail),
		reinterpret_cast<const void *>(&nd_thread_set_intent),
		reinterpret_cast<const void *>(&nd_rt_admit),
		reinterpret_cast<const void *>(&nd_rt_admit_ex),
		reinterpret_cast<const void *>(&nd_rt_set_deadline),
		reinterpret_cast<const void *>(&nd_rt_notice_take),
	};
	return fns[0];
}
