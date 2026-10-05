// SPDX-License-Identifier: BSD-2-Clause
// NeoDarwin-Language: portability: CoreFoundation's C interface, compiled inside swift-corelibs-foundation's C CoreFoundation.
// CFMachPort for NeoDarwin (docs/base/corefoundation.md). swift-corelibs-
// foundation doesn't carry CFMachPort.c, which only Darwin uses, but its
// CFRunLoop runs version 1 (Mach port) sources on Darwin, and IOKitLib's
// IONotificationPortGetRunLoopSource wraps its port in one. This is the
// public interface on top of those sources: a CFMachPort names a receive
// right, and its run loop source calls the callout with each message that
// arrives. As in Apple's CF, a valid CFMachPort is unique per port:
// CFMachPortCreateWithPort returns the existing one, retained, and sets
// *shouldFreeInfo. Not implemented: dead-name notification of send rights.
#include "CFInternal.h"
#include "CFRuntime_Internal.h"
#include "CFMachPort.h"
#include "CFRunLoop.h"
#include <mach/mach.h>
#include <os/lock.h>

struct __CFMachPort {
	CFRuntimeBase _base;
	struct __CFMachPort *_next;	// in the table of valid ports
	mach_port_t _port;
	Boolean _valid;
	Boolean _ownsPort;		// created by CFMachPortCreate
	CFMachPortCallBack _callout;
	CFMachPortContext _context;
	CFMachPortInvalidationCallBack _invalidation;
	CFRunLoopSourceRef _source;
};

static os_unfair_lock __CFMachPortsLock = OS_UNFAIR_LOCK_INIT;
static struct __CFMachPort *__CFMachPorts;

CF_PRIVATE void __CFMachMessageCheckForAndDestroyUnsentMessage(kern_return_t kr, mach_msg_header_t *msg) {
	// A message the kernel didn't take still holds its rights.
	switch (kr) {
	case MACH_SEND_TIMED_OUT:
	case MACH_SEND_INTERRUPTED:
	case MACH_SEND_INVALID_DEST:
		mach_msg_destroy(msg);
		break;
	default:
		break;
	}
}

CF_EXPORT Boolean _CFGetCurrentDirectory(char *path, int maxlen) {
	return getcwd(path, maxlen) != NULL;
}

static void __CFMachPortDeallocate(CFTypeRef cf) {
	CFMachPortInvalidate((CFMachPortRef)cf);
}

static CFStringRef __CFMachPortCopyDescription(CFTypeRef cf) {
	struct __CFMachPort *mp = (struct __CFMachPort *)cf;
	return CFStringCreateWithFormat(kCFAllocatorSystemDefault, NULL, CFSTR("<CFMachPort %p [%p]>{valid = %s, port = %x}"),
	    cf, CFGetAllocator(cf), mp->_valid ? "Yes" : "No", mp->_port);
}

const CFRuntimeClass __CFMachPortClass = {
	0, "CFMachPort", NULL, NULL, __CFMachPortDeallocate, NULL, NULL, NULL, __CFMachPortCopyDescription
};

CFTypeID CFMachPortGetTypeID(void) {
	return _kCFRuntimeIDCFMachPort;
}

CFMachPortRef CFMachPortCreateWithPort(CFAllocatorRef allocator, mach_port_t port, CFMachPortCallBack callout,
    CFMachPortContext *context, Boolean *shouldFreeInfo) {
	if (shouldFreeInfo) *shouldFreeInfo = true;
	if (!MACH_PORT_VALID(port)) return NULL;
	os_unfair_lock_lock(&__CFMachPortsLock);
	for (struct __CFMachPort *mp = __CFMachPorts; mp != NULL; mp = mp->_next) {
		if (mp->_port == port && mp->_valid) {
			CFRetain(mp);
			os_unfair_lock_unlock(&__CFMachPortsLock);
			return mp;
		}
	}
	struct __CFMachPort *mp = (struct __CFMachPort *)_CFRuntimeCreateInstance(allocator, _kCFRuntimeIDCFMachPort,
	    sizeof(struct __CFMachPort) - sizeof(CFRuntimeBase), NULL);
	if (mp == NULL) {
		os_unfair_lock_unlock(&__CFMachPortsLock);
		return NULL;
	}
	mp->_port = port;
	mp->_valid = true;
	mp->_callout = callout;
	if (context) {
		mp->_context = *context;
		if (context->retain) mp->_context.info = (void *)context->retain(context->info);
	}
	mp->_next = __CFMachPorts;
	__CFMachPorts = mp;
	os_unfair_lock_unlock(&__CFMachPortsLock);
	if (shouldFreeInfo) *shouldFreeInfo = false;
	return mp;
}

CFMachPortRef CFMachPortCreate(CFAllocatorRef allocator, CFMachPortCallBack callout, CFMachPortContext *context,
    Boolean *shouldFreeInfo) {
	mach_port_t port = MACH_PORT_NULL;
	if (shouldFreeInfo) *shouldFreeInfo = true;
	if (mach_port_allocate(mach_task_self(), MACH_PORT_RIGHT_RECEIVE, &port) != KERN_SUCCESS) return NULL;
	if (mach_port_insert_right(mach_task_self(), port, port, MACH_MSG_TYPE_MAKE_SEND) != KERN_SUCCESS) {
		mach_port_mod_refs(mach_task_self(), port, MACH_PORT_RIGHT_RECEIVE, -1);
		return NULL;
	}
	struct __CFMachPort *mp = (struct __CFMachPort *)CFMachPortCreateWithPort(allocator, port, callout, context, shouldFreeInfo);
	if (mp == NULL) {
		mach_port_mod_refs(mach_task_self(), port, MACH_PORT_RIGHT_RECEIVE, -1);
		mach_port_deallocate(mach_task_self(), port);
		return NULL;
	}
	mp->_ownsPort = true;
	return mp;
}

void CFMachPortInvalidate(CFMachPortRef port) {
	struct __CFMachPort *mp = (struct __CFMachPort *)port;
	__CFGenericValidateType(port, CFMachPortGetTypeID());
	CFRetain(port);
	os_unfair_lock_lock(&__CFMachPortsLock);
	if (!mp->_valid) {
		os_unfair_lock_unlock(&__CFMachPortsLock);
		CFRelease(port);
		return;
	}
	mp->_valid = false;
	for (struct __CFMachPort **p = &__CFMachPorts; *p != NULL; p = &(*p)->_next) {
		if (*p == mp) { *p = mp->_next; break; }
	}
	CFRunLoopSourceRef source = mp->_source;
	mp->_source = NULL;
	os_unfair_lock_unlock(&__CFMachPortsLock);
	if (source) {
		CFRunLoopSourceInvalidate(source);
		CFRelease(source);
	}
	if (mp->_invalidation) mp->_invalidation(port, mp->_context.info);
	if (mp->_context.release) mp->_context.release(mp->_context.info);
	mp->_context.info = NULL;
	if (mp->_ownsPort) {
		mach_port_mod_refs(mach_task_self(), mp->_port, MACH_PORT_RIGHT_RECEIVE, -1);
		mach_port_deallocate(mach_task_self(), mp->_port);
	}
	CFRelease(port);
}

Boolean CFMachPortIsValid(CFMachPortRef port) {
	__CFGenericValidateType(port, CFMachPortGetTypeID());
	return ((struct __CFMachPort *)port)->_valid;
}

mach_port_t CFMachPortGetPort(CFMachPortRef port) {
	__CFGenericValidateType(port, CFMachPortGetTypeID());
	return ((struct __CFMachPort *)port)->_port;
}

void CFMachPortGetContext(CFMachPortRef port, CFMachPortContext *context) {
	__CFGenericValidateType(port, CFMachPortGetTypeID());
	*context = ((struct __CFMachPort *)port)->_context;
}

CFMachPortInvalidationCallBack CFMachPortGetInvalidationCallBack(CFMachPortRef port) {
	__CFGenericValidateType(port, CFMachPortGetTypeID());
	return ((struct __CFMachPort *)port)->_invalidation;
}

void CFMachPortSetInvalidationCallBack(CFMachPortRef port, CFMachPortInvalidationCallBack callout) {
	struct __CFMachPort *mp = (struct __CFMachPort *)port;
	__CFGenericValidateType(port, CFMachPortGetTypeID());
	if (callout && !mp->_valid) {
		// Apple's CF calls a callback set on an invalid port at once.
		callout(port, mp->_context.info);
		return;
	}
	mp->_invalidation = callout;
}

static mach_port_t __CFMachPortSourceGetPort(void *info) {
	return ((struct __CFMachPort *)info)->_port;
}

static void *__CFMachPortSourcePerform(void *msg, CFIndex size, CFAllocatorRef allocator, void *info) {
	struct __CFMachPort *mp = (struct __CFMachPort *)info;
	if (mp->_valid && mp->_callout) mp->_callout(mp, msg, size, mp->_context.info);
	return NULL;
}

CFRunLoopSourceRef CFMachPortCreateRunLoopSource(CFAllocatorRef allocator, CFMachPortRef port, CFIndex order) {
	struct __CFMachPort *mp = (struct __CFMachPort *)port;
	__CFGenericValidateType(port, CFMachPortGetTypeID());
	if (!mp->_valid) return NULL;
	os_unfair_lock_lock(&__CFMachPortsLock);
	CFRunLoopSourceRef source = mp->_source;
	if (source) {
		CFRetain(source);
		os_unfair_lock_unlock(&__CFMachPortsLock);
		return source;
	}
	os_unfair_lock_unlock(&__CFMachPortsLock);
	CFRunLoopSourceContext1 context = {
		.version = 1,
		.info = (void *)port,
		.retain = CFRetain,
		.release = CFRelease,
		.copyDescription = CFCopyDescription,
		.getPort = __CFMachPortSourceGetPort,
		.perform = __CFMachPortSourcePerform,
	};
	source = CFRunLoopSourceCreate(allocator, order, (CFRunLoopSourceContext *)&context);
	if (source == NULL) return NULL;
	os_unfair_lock_lock(&__CFMachPortsLock);
	if (mp->_source == NULL && mp->_valid) {
		mp->_source = (CFRunLoopSourceRef)CFRetain(source);
	}
	os_unfair_lock_unlock(&__CFMachPortsLock);
	return source;
}
