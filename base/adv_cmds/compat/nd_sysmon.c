// SPDX-License-Identifier: BSD-2-Clause
// NeoDarwin-Language: portability: libsysmon's and libxpc's C interfaces as Apple's C pkill calls them.
//
// sysmon.h and xpc/xpc.h's functions for pkill (base/adv_cmds/build.sh).
// The table is read once from the kernel: sysctl(3) KERN_PROC_ALL for each
// process's kinfo_proc, and KERN_PROCARGS2 for its arguments (empty for a
// process whose arguments can't be read: another user's, or one that
// exited). Values live as long as pkill does; retain and release do
// nothing.

#include <sys/types.h>
#include <sys/proc.h>
#include <sys/proc_info.h>
#include <sys/sysctl.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include "sysmon.h"

enum nd_xpc_type { ND_XPC_UINT64, ND_XPC_DATE, ND_XPC_STRING, ND_XPC_ARRAY };

struct nd_xpc_value {
	enum nd_xpc_type type;
	uint64_t u;
	const char *s;
	size_t count;
	const struct nd_xpc_value *items;
};

struct nd_sysmon_row {
	struct nd_xpc_value v[ND_SYSMON_ATTR_COUNT];
};

struct nd_sysmon_table {
	size_t count;
	struct nd_sysmon_row *rows;
};

struct nd_sysmon_request {
	sysmon_request_handler_t handler;
};

uint64_t
xpc_uint64_get_value(xpc_object_t x)
{
	return (x != NULL && x->type == ND_XPC_UINT64 ? x->u : 0);
}

int64_t
xpc_date_get_value(xpc_object_t x)
{
	return (x != NULL && x->type == ND_XPC_DATE ? (int64_t)x->u : 0);
}

const char *
xpc_string_get_string_ptr(xpc_object_t x)
{
	return (x != NULL && x->type == ND_XPC_STRING ? x->s : NULL);
}

size_t
xpc_array_get_count(xpc_object_t x)
{
	return (x != NULL && x->type == ND_XPC_ARRAY ? x->count : 0);
}

const char *
xpc_array_get_string(xpc_object_t x, size_t index)
{
	return (index < xpc_array_get_count(x) ? xpc_string_get_string_ptr(&x->items[index]) : NULL);
}

bool
xpc_array_apply(xpc_object_t x, xpc_array_applier_t applier)
{
	for (size_t i = 0; i < xpc_array_get_count(x); i++)
		if (!applier(i, &x->items[i]))
			return (false);
	return (true);
}

static struct nd_xpc_value
uint_value(uint64_t u)
{
	return ((struct nd_xpc_value){ .type = ND_XPC_UINT64, .u = u });
}

static struct nd_xpc_value
string_value(const char *s)
{
	return ((struct nd_xpc_value){ .type = ND_XPC_STRING, .s = s });
}

// The process's arguments from KERN_PROCARGS2: argc, the executable's
// path, padding, then argc strings.
static struct nd_xpc_value
arguments(pid_t pid, size_t argmax)
{
	struct nd_xpc_value a = { .type = ND_XPC_ARRAY };
	int mib[3] = { CTL_KERN, KERN_PROCARGS2, pid };
	size_t len = argmax;
	char *buf = malloc(argmax), *p, *end;
	struct nd_xpc_value *items;
	int argc;

	if (buf == NULL || sysctl(mib, 3, buf, &len, NULL, 0) < 0 || len < sizeof(argc)) {
		free(buf);
		return (a);
	}
	memcpy(&argc, buf, sizeof(argc));
	p = buf + sizeof(argc);
	end = buf + len;
	p += strnlen(p, (size_t)(end - p));	/* the executable's path */
	while (p < end && *p == '\0')
		p++;
	if (argc <= 0 || (items = calloc((size_t)argc, sizeof(*items))) == NULL)
		return (a);
	while (a.count < (size_t)argc && p < end) {
		items[a.count++] = string_value(p);
		p += strnlen(p, (size_t)(end - p)) + 1;
	}
	a.items = items;
	return (a);
}

sysmon_request_t
sysmon_request_create_with_error(int type, sysmon_request_handler_t handler)
{
	struct nd_sysmon_request *r;

	(void)type;
	if ((r = calloc(1, sizeof(*r))) != NULL)
		r->handler = handler;
	return (r);
}

void
sysmon_request_add_attribute(sysmon_request_t request, sysmon_attribute_t attribute)
{
	(void)request, (void)attribute;	/* every attribute is always read */
}

void
sysmon_request_execute(sysmon_request_t request)
{
	int mib[4] = { CTL_KERN, KERN_PROC, KERN_PROC_ALL, 0 };
	int argmax_mib[2] = { CTL_KERN, KERN_ARGMAX };
	struct nd_sysmon_table *t;
	struct kinfo_proc *kp = NULL;
	size_t len = 0, argmax_len = sizeof(int);
	int argmax = 0;

	for (int tries = 0; tries < 8; tries++) {
		if (sysctl(mib, 4, NULL, &len, NULL, 0) < 0)
			break;
		len += len / 4;
		free(kp);
		if ((kp = malloc(len)) == NULL)
			break;
		if (sysctl(mib, 4, kp, &len, NULL, 0) == 0)
			goto read;
		if (errno != ENOMEM)
			break;
	}
	free(kp);
	request->handler(NULL, "sysctl(KERN_PROC_ALL) failed");
	return;
read:
	if (sysctl(argmax_mib, 2, &argmax, &argmax_len, NULL, 0) < 0 || argmax <= 0)
		argmax = 256 * 1024;
	if ((t = calloc(1, sizeof(*t))) == NULL ||
	    (t->rows = calloc(len / sizeof(*kp) + 1, sizeof(*t->rows))) == NULL) {
		request->handler(NULL, "out of memory");
		return;
	}
	for (size_t i = 0; i < len / sizeof(*kp); i++) {
		struct extern_proc *p = &kp[i].kp_proc;
		struct eproc *e = &kp[i].kp_eproc;
		struct nd_sysmon_row *row = &t->rows[t->count++];
		uint64_t flags = 0;

		if (p->p_flag & P_SYSTEM)
			flags |= PROC_FLAG_SYSTEM;
		if (p->p_flag & P_CONTROLT)
			flags |= PROC_FLAG_CONTROLT;
		row->v[SYSMON_ATTR_PROC_PID] = uint_value((uint64_t)p->p_pid);
		row->v[SYSMON_ATTR_PROC_FLAGS] = uint_value(flags);
		row->v[SYSMON_ATTR_PROC_UID] = uint_value(e->e_ucred.cr_uid);
		row->v[SYSMON_ATTR_PROC_COMM] = string_value(p->p_comm);
		row->v[SYSMON_ATTR_PROC_ARGUMENTS] = arguments(p->p_pid, (size_t)argmax);
		row->v[SYSMON_ATTR_PROC_RUID] = uint_value(e->e_pcred.p_ruid);
		row->v[SYSMON_ATTR_PROC_RGID] = uint_value(e->e_pcred.p_rgid);
		row->v[SYSMON_ATTR_PROC_PPID] = uint_value((uint64_t)e->e_ppid);
		row->v[SYSMON_ATTR_PROC_PGID] = uint_value((uint64_t)e->e_pgid);
		row->v[SYSMON_ATTR_PROC_TDEV] = uint_value((uint64_t)(uint32_t)e->e_tdev);
		row->v[SYSMON_ATTR_PROC_START] = (struct nd_xpc_value){ .type = ND_XPC_DATE,
		    .u = (uint64_t)p->p_starttime.tv_sec * 1000000000u + (uint64_t)p->p_starttime.tv_usec * 1000u };
	}
	request->handler(t, NULL);
}

void *
sysmon_retain(void *object)
{
	return (object);
}

void
sysmon_release(void *object)
{
	(void)object;
}

size_t
sysmon_table_get_count(sysmon_table_t table)
{
	return (table->count);
}

sysmon_row_t
sysmon_table_get_row(sysmon_table_t table, size_t index)
{
	return (index < table->count ? &table->rows[index] : NULL);
}

xpc_object_t
sysmon_row_get_value(sysmon_row_t row, sysmon_attribute_t attribute)
{
	if (row == NULL || attribute >= ND_SYSMON_ATTR_COUNT)
		return (NULL);
	if (attribute == SYSMON_ATTR_PROC_ARGUMENTS && row->v[attribute].count == 0)
		return (NULL);
	return (&row->v[attribute]);
}
