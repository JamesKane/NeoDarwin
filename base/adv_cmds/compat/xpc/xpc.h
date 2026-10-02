// SPDX-License-Identifier: BSD-2-Clause
// NeoDarwin-Language: expressibility: a compat header for Apple's C pkill, which includes it.
//
// The part of libxpc's <xpc/xpc.h> that pkill uses on the values in
// sysmon's process table (sysmon.h here): unsigned integers, strings,
// dates and arrays of strings. libxpc is closed and NeoDarwin's is a
// stand-in (docs/base/libsystem.md); these values are nd_sysmon.c's own,
// not XPC objects, and exist only inside pkill.
#ifndef ND_XPC_XPC_H
#define ND_XPC_XPC_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef const struct nd_xpc_value *xpc_object_t;
typedef bool (^xpc_array_applier_t)(size_t index, xpc_object_t value);

uint64_t xpc_uint64_get_value(xpc_object_t xuint);
int64_t xpc_date_get_value(xpc_object_t xdate);
const char *xpc_string_get_string_ptr(xpc_object_t xstring);
size_t xpc_array_get_count(xpc_object_t xarray);
const char *xpc_array_get_string(xpc_object_t xarray, size_t index);
bool xpc_array_apply(xpc_object_t xarray, xpc_array_applier_t applier);

#endif
