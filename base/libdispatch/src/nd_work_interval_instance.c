// SPDX-License-Identifier: BSD-2-Clause
// NeoDarwin-Language: portability: C entry points with the C ABI libsystem_kernel exports.
//
// The work interval instance interface (sdk/sys/work_interval.h), which
// libsystem_kernel exports on Apple's systems from sources Apple does not
// publish, and which libdispatch's os_workgroup_interval uses. It belongs in
// libsystem_kernel; until NeoDarwin's libsystem_kernel has it, libdispatch
// carries it privately (hidden, not exported).
//
// An instance stages the start, deadline, finish and complexity of one
// interval. xnu-12377 takes an interval's timing through one published
// operation, WORK_INTERVAL_OPERATION_NOTIFY (work_interval_notify(), made
// once the interval has finished), so start and update only stage values,
// finish sends them, and complexity and per-instance telemetry are not
// reported (telemetry reads as zero).

#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <sys/work_interval.h>

#define ND_HIDDEN __attribute__((visibility("hidden")))

struct work_interval_instance {
	work_interval_t wi;
	uint64_t start, deadline, finish, complexity;
};

ND_HIDDEN work_interval_instance_t
work_interval_instance_alloc(work_interval_t work_interval)
{
	work_interval_instance_t instance = calloc(1, sizeof(*instance));
	if (instance != NULL) {
		instance->wi = work_interval;
	}
	return instance;
}

ND_HIDDEN void
work_interval_instance_free(work_interval_instance_t instance)
{
	free(instance);
}

ND_HIDDEN void
work_interval_instance_clear(work_interval_instance_t instance)
{
	instance->start = instance->deadline = instance->finish = instance->complexity = 0;
}

ND_HIDDEN void
work_interval_instance_set_start(work_interval_instance_t instance, uint64_t start)
{
	instance->start = start;
}

ND_HIDDEN void
work_interval_instance_set_deadline(work_interval_instance_t instance, uint64_t deadline)
{
	instance->deadline = deadline;
}

ND_HIDDEN void
work_interval_instance_set_finish(work_interval_instance_t instance, uint64_t finish)
{
	instance->finish = finish;
}

ND_HIDDEN void
work_interval_instance_set_complexity(work_interval_instance_t instance, uint64_t complexity)
{
	instance->complexity = complexity;
}

ND_HIDDEN int
work_interval_instance_start(work_interval_instance_t instance)
{
	return instance->wi == NULL ? (errno = EINVAL, -1) : 0;
}

ND_HIDDEN int
work_interval_instance_update(work_interval_instance_t instance)
{
	return instance->wi == NULL ? (errno = EINVAL, -1) : 0;
}

ND_HIDDEN int
work_interval_instance_finish(work_interval_instance_t instance)
{
	return work_interval_notify(instance->wi, instance->start, instance->finish,
	           instance->deadline, 0, 0);
}

ND_HIDDEN void
work_interval_instance_get_telemetry_data(work_interval_instance_t instance,
    work_interval_data_t data, size_t size)
{
	(void)instance;
	memset(data, 0, size < sizeof(*data) ? size : sizeof(*data));
}
