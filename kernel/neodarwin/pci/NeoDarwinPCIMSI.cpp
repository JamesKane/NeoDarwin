// SPDX-License-Identifier: BSD-2-Clause
// NeoDarwin-Language: expressibility: IOKit classes are C++; this subclasses IOPCIFamily's IOPCIMessagedInterruptController.
//
// PCI MSI and MSI-X through the GIC ITS (NeoDarwinPCIMSI.h,
// docs/kernel/gic-its.md).

#include "NeoDarwinPCIMSI.h"
#include "NeoDarwinPCIHostBridge.h"
#include "NeoDarwinPlatform.h"
#include <IOKit/IOLib.h>
#include <pexpert/pexpert.h>

#define super IOPCIMessagedInterruptController
OSDefineMetaClassAndStructors(NeoDarwinPCIMessagedInterruptController, IOPCIMessagedInterruptController);

#define ND_FIRST_LPI     8192
// Vectors (LPIs) the controller hands out: plenty for the devices of the
// machines we know (one per MSI device, one per MSI-X device unless its
// driver asks for more), and each costs an IOInterruptVector with a lock.
#define ND_MSI_VECTORS   2048
#define ND_MAX_ITS       4

static IOLock *gMSILock;
static bool gMSITried;
static NeoDarwinPCIMessagedInterruptController *gMSI;

// Host bridges may probe on several threads; the first creates the lock.
static IOLock *
msi_lock(void)
{
	IOLock *lock = __atomic_load_n(&gMSILock, __ATOMIC_ACQUIRE);
	if (lock == NULL) {
		IOLock *fresh = IOLockAlloc();
		if (__atomic_compare_exchange_n(&gMSILock, &lock, fresh, false, __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE)) {
			lock = fresh;
		} else {
			IOLockFree(fresh);
		}
	}
	return lock;
}

NeoDarwinPCIMessagedInterruptController *
NeoDarwinPCIMessagedInterruptController::shared(IOACPIPlatformDevice *acpi)
{
	IOLockLock(msi_lock());
	if (gMSITried) {
		IOLockUnlock(gMSILock);
		return gMSI;
	}
	gMSITried = true;
	char why[160] = "";
	NeoDarwinPCIMessagedInterruptController *msi = NULL;
	do {
		uint32_t enabled = 1;
		if (PE_parse_boot_argn("nd_pci_msi", &enabled, sizeof(enabled)) && enabled == 0) {
			snprintf(why, sizeof(why), "disabled by nd_pci_msi=0");
			break;
		}
		NeoDarwinGICv3 *gic = NeoDarwinGICv3::instance();
		if (gic == NULL) {
			snprintf(why, sizeof(why), "no GICv3");
			break;
		}
		const OSData *madt = acpi->getACPITableData("APIC", 0);
		struct nd_madt_its found[ND_MAX_ITS];
		unsigned int n = madt != NULL ? nd_madt_its((const uint8_t *)madt->getBytesNoCopy(), madt->getLength(), found, ND_MAX_ITS) : 0;
		if (n == 0) {
			snprintf(why, sizeof(why), "the MADT has no GIC ITS");
			break;
		}
		if (!gic->initLPIs(why, sizeof(why))) {
			break;
		}
		NeoDarwinGICv3ITS *ready[ND_MAX_ITS];
		unsigned int readyCount = 0;
		for (unsigned int i = 0; i < n && i < ND_MAX_ITS; i++) {
			char itsWhy[160] = "";
			NeoDarwinGICv3ITS *its = NeoDarwinGICv3ITS::withAddress(gic, found[i].base, found[i].id, itsWhy, sizeof(itsWhy));
			if (its == NULL) {
				IOLog("NeoDarwinPCIMSI: ITS %u at 0x%llx: %s\n", found[i].id, found[i].base, itsWhy);
				continue;
			}
			IOLog("NeoDarwinPCIMSI: %s\n", its->describe());
			ready[readyCount++] = its;
		}
		if (readyCount == 0) {
			snprintf(why, sizeof(why), "no ITS could be set up");
			break;
		}
		msi = OSTypeAlloc(NeoDarwinPCIMessagedInterruptController);
		if (msi == NULL) {
			snprintf(why, sizeof(why), "no memory");
			break;
		}
		msi->gic = gic;
		for (unsigned int i = 0; i < readyCount; i++) {
			msi->its[msi->itsCount++] = ready[i];
		}
		uint32_t count = gic->lpiCount() < ND_MSI_VECTORS ? gic->lpiCount() : ND_MSI_VECTORS;
		msi->vectorCount = count;
		msi->mappings = IONewZero(Mapping, count);
		msi->mapLock = IOLockAlloc();
		if (msi->mappings == NULL || msi->mapLock == NULL || !msi->init(count, ND_FIRST_LPI, 0)) {
			snprintf(why, sizeof(why), "the messaged interrupt controller did not initialise");
			// An ITS stays set up; nothing uses it.
			msi = NULL;
			break;
		}
		gic->setLPIHandler(&NeoDarwinPCIMessagedInterruptController::lpiInterrupt, msi);
		IOLog("NeoDarwinPCIMSI: MSI and MSI-X on LPIs %u-%u through %u ITS%s; %s\n", ND_FIRST_LPI,
		    ND_FIRST_LPI + count - 1, msi->itsCount, msi->itsCount == 1 ? "" : "s", gic->lpiAttributes());
	} while (false);
	if (msi == NULL) {
		IOLog("NeoDarwinPCIMSI: no MSIs: %s; PCI devices use INTx\n", why);
	}
	gMSI = msi;
	IOLockUnlock(gMSILock);
	return msi;
}

// From the GIC's IRQ loop, for every LPI. IOPCIMessagedInterruptController
// runs the vector's handler (or, for MSI-X vectors sharing one LPI, those of
// its sub-vectors) and, if the vector is disabled, remembers it for enable.
void
NeoDarwinPCIMessagedInterruptController::lpiInterrupt(void *target, uint32_t intid)
{
	static_cast<NeoDarwinPCIMessagedInterruptController *>(target)->handleInterrupt(NULL, NULL, (int)intid);
}

static NeoDarwinPCIHostBridge *
host_bridge_of(IOService *device)
{
	for (IOService *p = device->getProvider(); p != NULL; p = p->getProvider()) {
		if (NeoDarwinPCIHostBridge *b = OSDynamicCast(NeoDarwinPCIHostBridge, p)) {
			return b;
		}
	}
	return NULL;
}

// IOPCIFamily allocates a device's vectors here, first for as many as it
// wants, then fewer (MSI halves, MSI-X decrements) if that fails. The range
// comes from the superclass's allocator (aligned to its size, as multiple
// MSI requires); then the ITS learns the device and its events. A failure
// in the ITS gives the vectors back, and IOPCIFamily leaves the device on
// INTx.
bool
NeoDarwinPCIMessagedInterruptController::allocateInterruptVectors(IOService *entry, uint32_t numVectors, IORangeScalar *rangeStartOut)
{
	IOPCIDevice *device = OSDynamicCast(IOPCIDevice, entry);
	NeoDarwinPCIHostBridge *bridge = device != NULL ? host_bridge_of(device) : NULL;
	if (bridge == NULL || numVectors == 0) {
		return false;
	}
	NDMSIRoute route;
	bridge->msiRoute(device, &route);
	char where[24];
	snprintf(where, sizeof(where), "%04x:%02x:%02x.%x", bridge->getSegment(), device->getBusNumber(), device->getDeviceNumber(),
	    device->getFunctionNumber());
	unsigned int which = ND_MAX_ITS;
	for (unsigned int i = 0; i < itsCount; i++) {
		if (its[i]->getID() == route.itsID) {
			which = i;
		}
	}
	if (which == ND_MAX_ITS) {
		IOLog("NeoDarwinPCIMSI: %s: requester ID goes to ITS %u, which is not set up\n", where, route.itsID);
		return false;
	}

	IOLockLock(mapLock);
	bool ok = super::allocateInterruptVectors(entry, numVectors, rangeStartOut);
	uint32_t first = ok ? (uint32_t)*rangeStartOut : 0;
	if (ok) {
		ok = its[which]->mapDevice(route.deviceID, numVectors);
		uint32_t i = 0;
		for (; ok && i < numVectors; i++) {
			ok = its[which]->mapEvent(route.deviceID, i, ND_FIRST_LPI + first + i, gic->bootCPU());
			if (ok) {
				mappings[first + i] = Mapping{ route.deviceID, (uint16_t)i, (uint8_t)which, true };
			}
		}
		if (!ok) {
			while (i-- > 0) {
				its[which]->discardEvent(route.deviceID, i, gic->bootCPU());
				gic->configureLPI(ND_FIRST_LPI + first + i, false);
				mappings[first + i].valid = false;
			}
			_messagedInterruptsAllocator->deallocate(first, numVectors);
			setProperty("MSIFree", _messagedInterruptsAllocator->getFreeCount(), 32);
		}
	}
	IOLockUnlock(mapLock);
	if (!ok) {
		return false;
	}

	// The log: which capability (IOPCIFamily's choice: MSI-X if the device
	// has no MSI capability or prefers MSI-X), how many table entries, the
	// LPIs, the DeviceID and what the IORT says is between.
	char kind[48];
	IOByteCount cap = 0;
	if (device->getProperty(kIOPCIMSIXMessageControlKey) != NULL && device->extendedFindPCICapability(kIOPCIMSIXCapability, &cap)) {
		uint32_t table = (device->configRead16(cap + 2) & 0x7ff) + 1;
		snprintf(kind, sizeof(kind), "MSI-X %u of %u vector%s", numVectors, table, table == 1 ? "" : "s");
	} else {
		snprintf(kind, sizeof(kind), "MSI %u vector%s", numVectors, numVectors == 1 ? "" : "s");
	}
	char lpis[24], via[80] = "";
	if (numVectors == 1) {
		snprintf(lpis, sizeof(lpis), "LPI %u", ND_FIRST_LPI + first);
	} else {
		snprintf(lpis, sizeof(lpis), "LPIs %u-%u", ND_FIRST_LPI + first, ND_FIRST_LPI + first + numVectors - 1);
	}
	if (route.status != ND_IORT_OK) {
		snprintf(via, sizeof(via), " (%s: DeviceID is the requester ID)", nd_iort_status_string(route.status));
	} else if (route.iort.smmu_count != 0) {
		snprintf(via, sizeof(via), " via SMMU%s at 0x%llx as stream 0x%x (bypass)", route.iort.smmu[0].type == 4 ? "v3" : "v2",
		    route.iort.smmu[0].base, route.iort.smmu[0].stream_id);
	}
	IOLog("NeoDarwinPCIMSI: %s: %s -> %s, DeviceID 0x%x on ITS %u%s\n", where, kind, lpis, route.deviceID, route.itsID, via);
	device->setProperty("msi-lpi-base", ND_FIRST_LPI + first, 32);
	device->setProperty("msi-lpi-count", numVectors, 32);
	device->setProperty("msi-device-id", route.deviceID, 32);
	return true;
}

// The message for the vectors IOPCIFamily just allocated: the doorbell of
// the device's ITS, and EventID 0 (MSI and MSI-X add the vector's index).
IOReturn
NeoDarwinPCIMessagedInterruptController::messageFor(IOService *entry, uint32_t vector, uint32_t message[3])
{
	(void)entry;
	uint32_t v = vector - ND_FIRST_LPI;
	if (vector < ND_FIRST_LPI || v >= vectorCount || message == NULL) {
		return kIOReturnBadArgument;
	}
	IOLockLock(mapLock);
	Mapping m = mappings[v];
	IOLockUnlock(mapLock);
	if (!m.valid) {
		return kIOReturnNotFound;
	}
	uint64_t doorbell = its[m.its]->getDoorbell();
	message[0] = (uint32_t)doorbell;
	message[1] = (uint32_t)(doorbell >> 32);
	message[2] = m.eventID;
	return kIOReturnSuccess;
}

// A device's vectors are freed (the device went away): its events are
// discarded and the LPIs disabled before the vectors are reused.
void
NeoDarwinPCIMessagedInterruptController::deallocateInterrupt(UInt32 vector)
{
	if (vector < vectorCount) {
		IOLockLock(mapLock);
		Mapping m = mappings[vector];
		mappings[vector].valid = false;
		IOLockUnlock(mapLock);
		if (m.valid) {
			its[m.its]->discardEvent(m.deviceID, m.eventID, gic->bootCPU());
			gic->configureLPI(ND_FIRST_LPI + vector, false);
		}
	}
	super::deallocateInterrupt(vector);
}

// IOPCIMessagedInterruptController::enableVector and disableVectorHard write
// the MSI-X table entry `vectorNumber`. For a device whose vectors each have
// a controller vector (a driver asked for several, configureInterrupts), the
// number they get is the controller's (first + i), so upstream would unmask
// entry first + i instead of i: another vector's entry, or past the table.
// A device whose MSI-X vectors share one controller vector (IOPCIFamily's
// default: one vector, the others dispatched from it) passes its own
// sub-vector index, and a vector outside the controller's table is one of
// those sub-vectors. The EventID is the device's vector index.
IOInterruptVectorNumber
NeoDarwinPCIMessagedInterruptController::tableIndex(IOInterruptVectorNumber vectorNumber, IOInterruptVector *vector)
{
	if (vector >= vectors && vector < vectors + vectorCount) {
		uint32_t v = (uint32_t)(vector - vectors);
		if (mappings[v].valid) {
			return mappings[v].eventID;
		}
	}
	return vectorNumber;
}

void
NeoDarwinPCIMessagedInterruptController::enableVector(IOInterruptVectorNumber vectorNumber, IOInterruptVector *vector)
{
	super::enableVector(tableIndex(vectorNumber, vector), vector);
}

void
NeoDarwinPCIMessagedInterruptController::disableVectorHard(IOInterruptVectorNumber vectorNumber, IOInterruptVector *vector)
{
	super::disableVectorHard(tableIndex(vectorNumber, vector), vector);
}
