/*
 * SPDX-License-Identifier: BSD-2-Clause
 * NeoDarwin-Language: expressibility: a C header completing one from Apple's internal SDK.
 *
 * xnu publishes sys/work_interval.h without the work interval instance
 * interface (work_interval_instance_*, struct work_interval_data), which
 * libsystem_kernel exports on Apple's systems (see the SDK's
 * libsystem_kernel.tbd) from sources Apple does not publish. libdispatch's
 * os_workgroup_interval (src/workgroup.c) is built on it. This header adds
 * the declarations to xnu's; the signatures follow libdispatch's calls and
 * xnu's tests/work_interval_data_test.c. The implementation is libsystem_kernel's
 * (docs/base/libsystem.md), which must agree with the layout below.
 */
#ifndef _ND_SYS_WORK_INTERVAL_H
#define _ND_SYS_WORK_INTERVAL_H

#include_next <sys/work_interval.h>

#ifndef KERNEL
#include <stddef.h>
#include <stdint.h>

/* Per-instance telemetry, filled in when the work interval was created with
 * WORK_INTERVAL_FLAG_ENABLE_TELEMETRY_DATA. */
struct work_interval_data {
	uint32_t wid_external_wakeups;
	uint32_t wid_total_wakeups;
	uint64_t wid_user_time_mach;
	uint64_t wid_system_time_mach;
	uint64_t wid_cycles;
	uint64_t wid_instructions;
};

__BEGIN_DECLS
/* An instance is one start/update/finish cycle of a work interval; the
 * setters stage values that the next start, update or finish sends. */
work_interval_instance_t work_interval_instance_alloc(work_interval_t work_interval);
void work_interval_instance_free(work_interval_instance_t instance);
void work_interval_instance_clear(work_interval_instance_t instance);
void work_interval_instance_set_start(work_interval_instance_t instance, uint64_t start);
void work_interval_instance_set_deadline(work_interval_instance_t instance, uint64_t deadline);
void work_interval_instance_set_finish(work_interval_instance_t instance, uint64_t finish);
void work_interval_instance_set_complexity(work_interval_instance_t instance, uint64_t complexity);
int work_interval_instance_start(work_interval_instance_t instance);
int work_interval_instance_update(work_interval_instance_t instance);
int work_interval_instance_finish(work_interval_instance_t instance);
void work_interval_instance_get_telemetry_data(work_interval_instance_t instance,
    work_interval_data_t data, size_t size);
__END_DECLS
#endif /* !KERNEL */

#endif /* _ND_SYS_WORK_INTERVAL_H */
