// SPDX-License-Identifier: BSD-2-Clause
// NeoDarwin-Language: expressibility: a compat header for Apple's C pkill, which includes it.
//
// libsysmon's <sysmon.h> as pkill (adv_cmds) uses it: a request for a
// table of processes with the listed attributes, answered through a block.
// libsysmon is closed (it asks sysmond over XPC); nd_sysmon.c answers from
// the kernel's process table (sysctl(3) KERN_PROC_ALL and KERN_PROCARGS2),
// synchronously, inside pkill.
#ifndef ND_SYSMON_H
#define ND_SYSMON_H

#include <dispatch/dispatch.h>
#include <xpc/xpc.h>

typedef struct nd_sysmon_request *sysmon_request_t;
typedef struct nd_sysmon_table *sysmon_table_t;
typedef const struct nd_sysmon_row *sysmon_row_t;
typedef void (^sysmon_request_handler_t)(sysmon_table_t table, const char *error_str);

enum {
	SYSMON_REQUEST_TYPE_PROCESS = 1,
};

typedef enum {
	SYSMON_ATTR_PROC_PID,
	SYSMON_ATTR_PROC_FLAGS,	/* PROC_FLAG_SYSTEM, PROC_FLAG_CONTROLT (<sys/proc_info.h>) */
	SYSMON_ATTR_PROC_UID,
	SYSMON_ATTR_PROC_COMM,
	SYSMON_ATTR_PROC_ARGUMENTS,
	SYSMON_ATTR_PROC_RUID,
	SYSMON_ATTR_PROC_RGID,
	SYSMON_ATTR_PROC_PPID,
	SYSMON_ATTR_PROC_PGID,
	SYSMON_ATTR_PROC_TDEV,
	SYSMON_ATTR_PROC_START,	/* a date: nanoseconds since the epoch */
	ND_SYSMON_ATTR_COUNT
} sysmon_attribute_t;

sysmon_request_t sysmon_request_create_with_error(int type, sysmon_request_handler_t handler);
void sysmon_request_add_attribute(sysmon_request_t request, sysmon_attribute_t attribute);
void sysmon_request_execute(sysmon_request_t request);
void *sysmon_retain(void *object);
void sysmon_release(void *object);
size_t sysmon_table_get_count(sysmon_table_t table);
sysmon_row_t sysmon_table_get_row(sysmon_table_t table, size_t index);
xpc_object_t sysmon_row_get_value(sysmon_row_t row, sysmon_attribute_t attribute);

#endif
