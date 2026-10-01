// SPDX-License-Identifier: BSD-2-Clause
// NeoDarwin-Language: expressibility: IOKit drivers are C++ IOService subclasses.
//
// Names network interfaces until NeoDarwin has a user-space namer
// (docs/kernel/network.md, "Naming").
//
// On macOS an IONetworkInterface that IONetworkStack sees published waits
// for a name: configd's InterfaceNamer chooses its unit (persistently, by
// its location, from /Library/Preferences/SystemConfiguration/
// NetworkInterfaces.plist) and asks IONetworkStack, through its
// properties, to register it; IONetworkStack then names it (en0, en1, ...)
// and attaches it to BSD. Only a network root is named in the kernel
// (IOKitBSDInit.cpp, IORegisterNetworkInterface). NeoDarwin has no configd,
// so this driver does what IORegisterNetworkInterface does for every
// interface, in the order they appear: the lowest free unit of its prefix
// (kIONetworkStackRegisterInterfaceWithLowestUnit, unit 0). From the
// kernel task, so IONetworkStack's entitlement check doesn't apply.
//
// It matches IONetworkStack (its own match category), watches IONetworkInterface
// publications, and names each new one from a thread call, once IONetworkStack
// has taken the interface as its client (the stack's own publication
// handler, which may run after this one). An interface that already has a
// BSD name (IONetworkStack registers it again after the BSD attach) is left
// alone. Boot-arg nd_ifnamer=0 turns it off: interfaces stay unnamed.

#include <IOKit/IOLib.h>
#include <IOKit/IOBSD.h>
#include <IOKit/IOKitKeys.h>
#include <IOKit/IOService.h>
#include <IOKit/network/IONetworkInterface.h>
#include <kern/thread_call.h>
#include <pexpert/pexpert.h>

#include "IONetworkStack.h"

class NeoDarwinInterfaceNamer : public IOService
{
	OSDeclareDefaultStructors(NeoDarwinInterfaceNamer);

public:
	virtual bool start(IOService *provider) APPLE_KEXT_OVERRIDE;
	virtual void stop(IOService *provider) APPLE_KEXT_OVERRIDE;
	virtual void free() APPLE_KEXT_OVERRIDE;

private:
	static bool published(void *target, void *ref, IOService *service, IONotifier *notifier);
	static void work(thread_call_param_t self, thread_call_param_t);
	bool name(IONetworkInterface *netif);

	IOService *stack = NULL;
	IONotifier *notifier = NULL;
	thread_call_t call = NULL;
	IOLock *lock = NULL;
	OSArray *pending = NULL;        // interfaces waiting for a name
};

#define super IOService
OSDefineMetaClassAndStructors(NeoDarwinInterfaceNamer, IOService);

bool
NeoDarwinInterfaceNamer::start(IOService *provider)
{
	uint32_t on = 1;
	if (PE_parse_boot_argn("nd_ifnamer", &on, sizeof(on)) && on == 0) {
		IOLog("NeoDarwinInterfaceNamer: off (nd_ifnamer=0): network interfaces stay unnamed\n");
		return false;
	}
	if (!super::start(provider)) {
		return false;
	}
	stack = provider;
	lock = IOLockAlloc();
	pending = OSArray::withCapacity(4);
	call = thread_call_allocate(&NeoDarwinInterfaceNamer::work, this);
	OSDictionary *matching = serviceMatching(kIONetworkInterfaceClass);
	if (lock == NULL || pending == NULL || call == NULL || matching == NULL) {
		OSSafeReleaseNULL(matching);
		return false;
	}
	notifier = addMatchingNotification(gIOPublishNotification, matching, &NeoDarwinInterfaceNamer::published, this, NULL, 0);
	matching->release();
	return notifier != NULL;
}

void
NeoDarwinInterfaceNamer::stop(IOService *provider)
{
	if (notifier != NULL) {
		notifier->remove();
		notifier = NULL;
	}
	if (call != NULL) {
		thread_call_cancel_wait(call);
	}
	super::stop(provider);
}

void
NeoDarwinInterfaceNamer::free()
{
	if (call != NULL) {
		thread_call_free(call);
		call = NULL;
	}
	OSSafeReleaseNULL(pending);
	if (lock != NULL) {
		IOLockFree(lock);
		lock = NULL;
	}
	super::free();
}

// A publication: a new interface, or one IONetworkStack has just attached
// to BSD and registered again (it has a BSD name then).
bool
NeoDarwinInterfaceNamer::published(void *target, void *ref, IOService *service, IONotifier *notifier)
{
	(void)ref; (void)notifier;
	NeoDarwinInterfaceNamer *self = static_cast<NeoDarwinInterfaceNamer *>(target);
	IONetworkInterface *netif = OSDynamicCast(IONetworkInterface, service);
	if (netif == NULL || netif->getProperty(kIOBSDNameKey) != NULL) {
		return true;
	}
	IOLockLock(self->lock);
	if (self->pending->getNextIndexOfObject(netif, 0) == (unsigned int)-1) {
		self->pending->setObject(netif);
	}
	IOLockUnlock(self->lock);
	self->retain();
	if (thread_call_enter(self->call)) {
		self->release();                // already scheduled, with its reference
	}
	return true;
}

// Asks IONetworkStack to register the interface with the lowest free unit,
// as IORegisterNetworkInterface does for a network root.
bool
NeoDarwinInterfaceNamer::name(IONetworkInterface *netif)
{
	OSDictionary *dict = OSDictionary::withCapacity(3);
	OSNumber *unit = OSNumber::withNumber((UInt64)0, 32);
	OSNumber *command = OSNumber::withNumber((UInt64)kIONetworkStackRegisterInterfaceWithLowestUnit, 32);
	uint64_t id = netif->getRegistryEntryID();
	OSData *entry = OSData::withBytes(&id, sizeof(id));
	IOReturn ret = kIOReturnNoMemory;
	if (dict != NULL && unit != NULL && command != NULL && entry != NULL) {
		dict->setObject(kIOInterfaceUnit, unit);
		dict->setObject(kIONetworkStackUserCommandKey, command);
		dict->setObject(kIORegistryEntryIDKey, entry);
		ret = stack->setProperties(dict);
	}
	OSSafeReleaseNULL(dict);
	OSSafeReleaseNULL(unit);
	OSSafeReleaseNULL(command);
	OSSafeReleaseNULL(entry);
	return ret == kIOReturnSuccess;
}

void
NeoDarwinInterfaceNamer::work(thread_call_param_t param, thread_call_param_t)
{
	NeoDarwinInterfaceNamer *self = static_cast<NeoDarwinInterfaceNamer *>(param);
	for (;;) {
		IOLockLock(self->lock);
		IONetworkInterface *netif = OSDynamicCast(IONetworkInterface, self->pending->getObject(0));
		if (netif != NULL) {
			netif->retain();
			self->pending->removeObject(0);
		}
		IOLockUnlock(self->lock);
		if (netif == NULL) {
			break;
		}
		// IONetworkStack's own publication handler attaches the interface
		// as its client and marks it waiting for a name; it may run after
		// ours. Wait for it (up to 5 s), then name.
		bool ready = false;
		for (int i = 0; i < 500 && !netif->isInactive(); i++) {
			if (self->stack->isParent(netif, gIOServicePlane)) {
				ready = true;
				break;
			}
			IOSleep(10);
		}
		const char *prefix = netif->getNamePrefix();
		const char *driver = netif->getProvider() != NULL ? netif->getProvider()->getName() : "?";
		if (!ready) {
			IOLog("NeoDarwinInterfaceNamer: an interface of %s was not taken by IONetworkStack; left unnamed\n", driver);
		} else {
			IOSleep(1);                     // the stack's state, set right after the attach
			bool ok = false;
			for (int tries = 0; tries < 3 && !ok && !netif->isInactive(); tries++) {
				ok = self->name(netif);
				if (!ok) {
					IOSleep(100);
				}
			}
			OSString *bsd = OSDynamicCast(OSString, netif->getProperty(kIOBSDNameKey));
			if (ok && bsd != NULL) {
				IOLog("NeoDarwinInterfaceNamer: %s: %s of %s\n", bsd->getCStringNoCopy(), netif->getMetaClass()->getClassName(), driver);
			} else {
				IOLog("NeoDarwinInterfaceNamer: cannot name an interface (%s) of %s\n", prefix != NULL ? prefix : "no prefix", driver);
			}
		}
		netif->release();
	}
	self->release();
}
