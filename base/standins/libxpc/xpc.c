// SPDX-License-Identifier: BSD-2-Clause
// NeoDarwin-Language: portability: a stand-in with the C interface of a closed Apple library.
//
// Stand-in for libxpc.dylib, which Apple does not publish; it also carries
// launchd's client library (bootstrap_*). What the open libraries use
// (base/sdk/usr/local/include/xpc/private.h and bootstrap_priv.h declare the
// private calls):
//  - XPC objects: null, bool, int64, uint64, string, date, uuid, array and
//    dictionary, reference counted, with the type objects callers compare
//    xpc_get_type() against. They are plain C objects: none of NeoDarwin's
//    callers is Objective-C, which would treat them as NSObjects;
//  - bootstrap_parent(), bootstrap_look_up2() and bootstrap_strerror(). NeoDarwin has one bootstrap
//    namespace and no launchd until launchd-842 lands (P1-08), so every
//    bootstrap port is its own parent, as the root of the tree is, and no
//    service can be looked up;
//  - service connections and pipes, which therefore never reach a peer:
//    xpc_pipe_create() returns NULL, as it does for a missing service; a
//    connection's messages are answered with XPC_ERROR_CONNECTION_INVALID
//    (its event handler is kept but never called);
//  - calls about the caller's own process: it has no entitlements, is not
//    app-sandboxed, and was not launched for an XPC event; no configuration
//    profile plist is parsed (xpc_create_from_plist() answers NULL, which
//    Libinfo takes as no profile).
// More of libxpc is added as NeoDarwin libraries come to need it
// (docs/base/libsystem.md).

#include <Block.h>
#include <bootstrap_priv.h>
#include <errno.h>
#include <mach/mach.h>
#include <mach/mach_error.h>
#include <servers/bootstrap.h>
#include <stdatomic.h>
#include <stdlib.h>
#include <string.h>
#include <xpc/private.h>
#include <xpc/xpc.h>

#pragma mark - Objects

struct _xpc_type_s {
	const char *name;
	void (*dispose)(xpc_object_t);
};

// Every object starts with this header. Statically allocated objects
// (XPC_BOOL_TRUE, the null object, errors) have refs -1 and are never freed.
struct nd_object {
	const struct _xpc_type_s *type;
	_Atomic int32_t refs;
};
#define ND_STATIC_HEADER(t) { .type = (t), .refs = -1 }

static void nd_dispose_string(xpc_object_t);
static void nd_dispose_array(xpc_object_t);
static void nd_dispose_dictionary(xpc_object_t);
static void nd_dispose_connection(xpc_object_t);

const struct _xpc_type_s _xpc_type_null = { "null", NULL };
const struct _xpc_type_s _xpc_type_bool = { "bool", NULL };
const struct _xpc_type_s _xpc_type_int64 = { "int64", NULL };
const struct _xpc_type_s _xpc_type_uint64 = { "uint64", NULL };
const struct _xpc_type_s _xpc_type_string = { "string", nd_dispose_string };
const struct _xpc_type_s _xpc_type_date = { "date", NULL };
const struct _xpc_type_s _xpc_type_uuid = { "uuid", NULL };
const struct _xpc_type_s _xpc_type_array = { "array", nd_dispose_array };
const struct _xpc_type_s _xpc_type_dictionary = { "dictionary", nd_dispose_dictionary };
const struct _xpc_type_s _xpc_type_connection = { "connection", nd_dispose_connection };
const struct _xpc_type_s _xpc_type_error = { "error", nd_dispose_dictionary };

struct nd_scalar {
	struct nd_object hdr;
	union {
		bool b;
		int64_t i;
		uint64_t u;
		uuid_t uuid;
	} v;
};

struct _xpc_bool_s {
	struct nd_scalar s;
};
const struct _xpc_bool_s _xpc_bool_true = { { ND_STATIC_HEADER(&_xpc_type_bool), { .b = true } } };
const struct _xpc_bool_s _xpc_bool_false = { { ND_STATIC_HEADER(&_xpc_type_bool), { .b = false } } };
static const struct nd_object nd_null = ND_STATIC_HEADER(&_xpc_type_null);

struct nd_string {
	struct nd_object hdr;
	size_t length;
	char *s;
};

struct nd_array {
	struct nd_object hdr;
	size_t count, capacity;
	xpc_object_t *values;
};

struct nd_entry {
	char *key;
	xpc_object_t value;
};

struct _xpc_dictionary_s {
	struct nd_object hdr;
	size_t count, capacity;
	struct nd_entry *entries;
};

// XPC_ERROR_CONNECTION_INVALID: an error object, which callers treat as a
// dictionary. It carries no entries.
const struct _xpc_dictionary_s _xpc_error_connection_invalid = { ND_STATIC_HEADER(&_xpc_type_error), 0, 0, NULL };

struct nd_connection {
	struct nd_object hdr;
	char *name;
	xpc_handler_t handler;
};

static void *
nd_alloc(size_t size, const struct _xpc_type_s *type)
{
	struct nd_object *o = calloc(1, size);
	if (o == NULL) {
		abort();   // as libxpc does: its allocations do not fail
	}
	o->type = type;
	atomic_init(&o->refs, 1);
	return o;
}

xpc_type_t
xpc_get_type(xpc_object_t object)
{
	return ((const struct nd_object *)object)->type;
}

xpc_object_t
xpc_retain(xpc_object_t object)
{
	struct nd_object *o = object;
	if (atomic_load_explicit(&o->refs, memory_order_relaxed) >= 0) {
		atomic_fetch_add_explicit(&o->refs, 1, memory_order_relaxed);
	}
	return object;
}

void
xpc_release(xpc_object_t object)
{
	struct nd_object *o = object;
	if (atomic_load_explicit(&o->refs, memory_order_relaxed) < 0) {
		return;
	}
	if (atomic_fetch_sub_explicit(&o->refs, 1, memory_order_acq_rel) == 1) {
		if (o->type->dispose != NULL) {
			o->type->dispose(o);
		}
		free(o);
	}
}

#pragma mark - Scalars and strings

xpc_object_t
xpc_null_create(void)
{
	return (xpc_object_t)&nd_null;
}

xpc_object_t
xpc_int64_create(int64_t value)
{
	struct nd_scalar *o = nd_alloc(sizeof(*o), &_xpc_type_int64);
	o->v.i = value;
	return o;
}

static xpc_object_t
nd_uint64_create(uint64_t value)
{
	struct nd_scalar *o = nd_alloc(sizeof(*o), &_xpc_type_uint64);
	o->v.u = value;
	return o;
}

xpc_object_t
xpc_date_create(int64_t interval)
{
	struct nd_scalar *o = nd_alloc(sizeof(*o), &_xpc_type_date);
	o->v.i = interval;
	return o;
}

xpc_object_t
xpc_uuid_create(const uuid_t uuid)
{
	struct nd_scalar *o = nd_alloc(sizeof(*o), &_xpc_type_uuid);
	memcpy(o->v.uuid, uuid, sizeof(uuid_t));
	return o;
}

xpc_object_t
xpc_string_create(const char *string)
{
	struct nd_string *o = nd_alloc(sizeof(*o), &_xpc_type_string);
	o->s = strdup(string);
	if (o->s == NULL) {
		abort();
	}
	o->length = strlen(string);
	return o;
}

static void
nd_dispose_string(xpc_object_t object)
{
	free(((struct nd_string *)object)->s);
}

size_t
xpc_string_get_length(xpc_object_t xstring)
{
	return xpc_get_type(xstring) == XPC_TYPE_STRING ? ((struct nd_string *)xstring)->length : 0;
}

const char *
xpc_string_get_string_ptr(xpc_object_t xstring)
{
	return xpc_get_type(xstring) == XPC_TYPE_STRING ? ((struct nd_string *)xstring)->s : NULL;
}

#pragma mark - Arrays

size_t
xpc_array_get_count(xpc_object_t xarray)
{
	return xpc_get_type(xarray) == XPC_TYPE_ARRAY ? ((struct nd_array *)xarray)->count : 0;
}

bool
xpc_array_apply(xpc_object_t xarray, xpc_array_applier_t applier)
{
	if (xpc_get_type(xarray) != XPC_TYPE_ARRAY) {
		return true;
	}
	struct nd_array *a = xarray;
	for (size_t i = 0; i < a->count; i++) {
		if (!applier(i, a->values[i])) {
			return false;
		}
	}
	return true;
}

static void
nd_dispose_array(xpc_object_t object)
{
	struct nd_array *a = object;
	for (size_t i = 0; i < a->count; i++) {
		xpc_release(a->values[i]);
	}
	free(a->values);
}

#pragma mark - Dictionaries

xpc_object_t
xpc_dictionary_create(const char *const *keys, const xpc_object_t *values, size_t count)
{
	struct _xpc_dictionary_s *d = nd_alloc(sizeof(*d), &_xpc_type_dictionary);
	for (size_t i = 0; i < count; i++) {
		xpc_dictionary_set_value(d, keys[i], values[i]);
	}
	return d;
}

// Sets key to value (retained), or removes key when value is NULL.
void
xpc_dictionary_set_value(xpc_object_t xdict, const char *key, xpc_object_t value)
{
	if (xpc_get_type(xdict) != XPC_TYPE_DICTIONARY) {
		return;
	}
	struct _xpc_dictionary_s *d = xdict;
	for (size_t i = 0; i < d->count; i++) {
		if (strcmp(d->entries[i].key, key) == 0) {
			xpc_object_t old = d->entries[i].value;
			if (value != NULL) {
				d->entries[i].value = xpc_retain(value);
			} else {
				free(d->entries[i].key);
				d->entries[i] = d->entries[--d->count];
			}
			xpc_release(old);
			return;
		}
	}
	if (value == NULL) {
		return;
	}
	if (d->count == d->capacity) {
		size_t capacity = d->capacity != 0 ? 2 * d->capacity : 8;
		struct nd_entry *entries = realloc(d->entries, capacity * sizeof(*entries));
		if (entries == NULL) {
			abort();
		}
		d->entries = entries;
		d->capacity = capacity;
	}
	char *k = strdup(key);
	if (k == NULL) {
		abort();
	}
	d->entries[d->count++] = (struct nd_entry){ k, xpc_retain(value) };
}

bool
xpc_dictionary_get_bool(xpc_object_t xdict, const char *key)
{
	if (xpc_get_type(xdict) != XPC_TYPE_DICTIONARY) {
		return false;
	}
	struct _xpc_dictionary_s *d = xdict;
	for (size_t i = 0; i < d->count; i++) {
		if (strcmp(d->entries[i].key, key) == 0) {
			struct nd_scalar *v = d->entries[i].value;
			return v->hdr.type == XPC_TYPE_BOOL && v->v.b;
		}
	}
	return false;
}

void
xpc_dictionary_set_string(xpc_object_t xdict, const char *key, const char *string)
{
	xpc_object_t value = xpc_string_create(string);
	xpc_dictionary_set_value(xdict, key, value);
	xpc_release(value);
}

void
xpc_dictionary_set_uint64(xpc_object_t xdict, const char *key, uint64_t value)
{
	xpc_object_t v = nd_uint64_create(value);
	xpc_dictionary_set_value(xdict, key, v);
	xpc_release(v);
}

static void
nd_dispose_dictionary(xpc_object_t object)
{
	struct _xpc_dictionary_s *d = object;
	for (size_t i = 0; i < d->count; i++) {
		free(d->entries[i].key);
		xpc_release(d->entries[i].value);
	}
	free(d->entries);
}

#pragma mark - Connections and pipes

xpc_connection_t
xpc_connection_create_mach_service(const char *name, dispatch_queue_t targetq, uint64_t flags)
{
	(void)targetq;
	(void)flags;
	struct nd_connection *c = nd_alloc(sizeof(*c), &_xpc_type_connection);
	c->name = strdup(name);
	if (c->name == NULL) {
		abort();
	}
	return (xpc_connection_t)c;
}

void
xpc_connection_set_event_handler(xpc_connection_t connection, xpc_handler_t handler)
{
	struct nd_connection *c = (struct nd_connection *)connection;
	xpc_handler_t old = c->handler;
	c->handler = Block_copy(handler);
	if (old != NULL) {
		Block_release(old);
	}
}

void
xpc_connection_resume(xpc_connection_t connection)
{
	(void)connection;
}

xpc_object_t
xpc_connection_send_message_with_reply_sync(xpc_connection_t connection, xpc_object_t message)
{
	(void)connection;
	(void)message;
	return (xpc_object_t)XPC_ERROR_CONNECTION_INVALID;   // a static object: xpc_release() ignores it
}

static void
nd_dispose_connection(xpc_object_t object)
{
	struct nd_connection *c = object;
	free(c->name);
	if (c->handler != NULL) {
		Block_release(c->handler);
	}
}

xpc_pipe_t
xpc_pipe_create(const char *name, uint64_t flags)
{
	(void)name;
	(void)flags;
	return NULL;
}

void
xpc_pipe_invalidate(xpc_pipe_t pipe)
{
	(void)pipe;
}

int
xpc_pipe_routine(xpc_pipe_t pipe, xpc_object_t request, xpc_object_t *reply)
{
	(void)pipe;
	(void)request;
	*reply = NULL;
	return EPIPE;
}

#pragma mark - The caller's process

xpc_object_t
xpc_copy_entitlement_for_token(const char *key, audit_token_t *token)
{
	(void)key;
	(void)token;
	return NULL;
}

bool
_xpc_runtime_is_app_sandboxed(void)
{
	return false;
}

bool
xpc_get_service_identifier_for_token(uint64_t token, event_name_t identifier)
{
	(void)token;
	(void)identifier;
	return false;
}

int
xpc_event_publisher_fire_noboost(xpc_event_publisher_t xpub, uint64_t token, xpc_object_t details)
{
	(void)xpub;
	(void)token;
	(void)details;
	return ENOTSUP;
}

xpc_object_t
xpc_create_from_plist(const void *data, size_t len)
{
	(void)data;
	(void)len;
	return NULL;
}

#pragma mark - Bootstrap

kern_return_t
bootstrap_parent(mach_port_t bp, mach_port_t *parent_port)
{
	*parent_port = bp;   // declared nonnull in <servers/bootstrap.h>
	return KERN_SUCCESS;
}

kern_return_t
bootstrap_look_up2(mach_port_t bp, const name_t service_name, mach_port_t *sp, pid_t target_pid, uint64_t flags)
{
	(void)bp;
	(void)service_name;
	(void)target_pid;
	(void)flags;
	*sp = MACH_PORT_NULL;
	return BOOTSTRAP_UNKNOWN_SERVICE;
}

const char *
bootstrap_strerror(kern_return_t r)
{
	switch (r) {
	case BOOTSTRAP_SUCCESS: return "Success";
	case BOOTSTRAP_NOT_PRIVILEGED: return "Permission denied";
	case BOOTSTRAP_NAME_IN_USE: return "Service name already exists";
	case BOOTSTRAP_UNKNOWN_SERVICE: return "Unknown service name";
	case BOOTSTRAP_SERVICE_ACTIVE: return "Service already active";
	case BOOTSTRAP_BAD_COUNT: return "Too many lookups were requested in one request";
	case BOOTSTRAP_NO_MEMORY: return "Out of memory";
	default: return mach_error_string(r);
	}
}
