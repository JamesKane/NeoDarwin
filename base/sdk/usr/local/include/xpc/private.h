/*
 * SPDX-License-Identifier: BSD-2-Clause
 * NeoDarwin-Language: expressibility: a C header standing in for one from Apple's internal SDK.
 *
 * Stands in for libxpc's xpc/private.h; libxpc is not published. It declares
 * the private calls NeoDarwin's libraries make, with the names and signatures
 * Apple's code uses; base/standins/libxpc implements them:
 *  - XPC pipes (libsystem_asl): a synchronous request/reply channel to a
 *    launchd-registered service;
 *  - the event publisher and token queries (libsystem_notify);
 *  - xpc_create_from_plist (libsystem_info's configuration profiles,
 *    libsystem_darwin's os_variant);
 *  - _xpc_runtime_is_app_sandboxed (copyfile's xattr_flags.c picks its
 *    table of attribute properties by whether the caller is app-sandboxed).
 * The pipe flag values are the ones Apple's libsystem_asl passes.
 */
#ifndef __XPC_PRIVATE_H__
#define __XPC_PRIVATE_H__

#include <bsm/audit.h>
#include <errno.h>   /* xpc_pipe_routine() returns errno values (EPIPE: re-create the pipe) */
#include <stdbool.h>
#include <stddef.h>
#include <xpc/xpc.h>

__BEGIN_DECLS

XPC_DECL(xpc_pipe);

#define XPC_PIPE_PRIVILEGED            (1ull << 1)
#define XPC_PIPE_USE_SYNC_IPC_OVERRIDE (1ull << 2)
#define XPC_PIPE_PROPAGATE_QOS         (1ull << 3)

xpc_pipe_t xpc_pipe_create(const char *name, uint64_t flags);
void xpc_pipe_invalidate(xpc_pipe_t pipe);
int xpc_pipe_routine(xpc_pipe_t pipe, xpc_object_t request, xpc_object_t *reply);

XPC_DECL(xpc_event_publisher);

/* A launchd event name; launchd's size is not published, and this is at least as large. */
typedef char event_name_t[128];

int xpc_event_publisher_fire_noboost(xpc_event_publisher_t xpub, uint64_t token, xpc_object_t details);
bool xpc_get_service_identifier_for_token(uint64_t token, event_name_t identifier);
xpc_object_t xpc_copy_entitlement_for_token(const char *key, audit_token_t *token);

XPC_EXPORT XPC_RETURNS_RETAINED XPC_WARN_RESULT
xpc_object_t _Nullable xpc_create_from_plist(const void *_Nonnull data, size_t len);

bool _xpc_runtime_is_app_sandboxed(void);

__END_DECLS

#endif /* __XPC_PRIVATE_H__ */
