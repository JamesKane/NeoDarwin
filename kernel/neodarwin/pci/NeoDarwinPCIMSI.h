// SPDX-License-Identifier: BSD-2-Clause
// NeoDarwin-Language: expressibility: IOKit classes are C++; this subclasses IOPCIFamily's IOPCIMessagedInterruptController.
//
// PCI MSI and MSI-X through the GIC ITS (docs/kernel/gic-its.md, P1-09
// checkpoint 3). IOPCIFamily does the PCI half: it finds a device's MSI or
// MSI-X capability, asks its bridge's provider chain for a messaged
// interrupt controller ("GetMessagedInterruptController"), allocates vectors
// from it, asks for the message ("GetMessagedInterruptAddress"), programs the
// capability or the MSI-X table, and appends the vectors to the device's
// interrupt specifiers after INTx. This class, which the host bridge hands
// out, is the platform half, which is closed on Intel and Apple silicon Macs:
//
//  - a vector is an LPI: controller vector v is INTID 8192 + v (the
//    IOPCIMessagedInterruptController's base vector is 8192), and the GIC's
//    IRQ loop hands every LPI to handleInterrupt;
//  - when IOPCIFamily allocates n vectors for a device, the device's
//    requester ID becomes a DeviceID through the IORT (nd_iort.h), its ITS is
//    told about it (MAPD, with an ITT for n events), and EventID i becomes
//    LPI 8192 + first + i on the boot CPU's collection (MAPTI);
//  - the message is GITS_TRANSLATER's address with data 0: MSI adds the
//    vector number to the data, MSI-X tables get data + i.
//
// One controller for the kernel: LPIs are global. It is created the first
// time a host bridge probes, unless the boot-arg nd_pci_msi=0 disables MSIs
// (IOPCIFamily then uses INTx alone).

#ifndef _NEODARWIN_PCI_MSI_H
#define _NEODARWIN_PCI_MSI_H

#include <IOKit/pci/IOPCIDevice.h>
#include <IOKit/pci/IOPCIPrivate.h>
#include <IOKit/acpi/IOACPIPlatformDevice.h>
#include "nd_iort.h"

class NeoDarwinGICv3;
class NeoDarwinGICv3ITS;

// Where a host bridge's requester IDs go (IORT), for one device.
struct NDMSIRoute {
	uint32_t deviceID;
	uint32_t itsID;
	enum nd_iort_status status;     // ND_IORT_OK, or why the IORT gave nothing
	struct nd_iort_route iort;
};

class NeoDarwinPCIMessagedInterruptController : public IOPCIMessagedInterruptController
{
	OSDeclareDefaultStructors(NeoDarwinPCIMessagedInterruptController);

public:
	// The controller, set up on the first call from the MADT's GIC ITS
	// structures (through `acpi`'s tables); NULL when there are no MSIs, and
	// then the reason is logged once.
	static NeoDarwinPCIMessagedInterruptController *shared(IOACPIPlatformDevice *acpi);

	// "GetMessagedInterruptAddress" for `entry`'s vectors from `vector`
	// (an INTID): address low, address high, data.
	IOReturn messageFor(IOService *entry, uint32_t vector, uint32_t message[3]);

	virtual void deallocateInterrupt(UInt32 vector) APPLE_KEXT_OVERRIDE;
	// IOPCIFamily masks and unmasks an MSI-X vector at the table entry of
	// its controller vector number, which is the device's own vector index
	// only for vectors sharing one controller vector; these pass the index.
	virtual void enableVector(IOInterruptVectorNumber vectorNumber, IOInterruptVector *vector) APPLE_KEXT_OVERRIDE;
	virtual void disableVectorHard(IOInterruptVectorNumber vectorNumber, IOInterruptVector *vector) APPLE_KEXT_OVERRIDE;

protected:
	virtual bool allocateInterruptVectors(IOService *device, uint32_t numVectors, IORangeScalar *rangeStartOut) APPLE_KEXT_OVERRIDE;

private:
	struct Mapping {
		uint32_t deviceID;
		uint16_t eventID;
		uint8_t its;
		bool valid;
	};

	static void lpiInterrupt(void *target, uint32_t intid);
	IOInterruptVectorNumber tableIndex(IOInterruptVectorNumber vectorNumber, IOInterruptVector *vector);

	NeoDarwinGICv3 *gic;
	NeoDarwinGICv3ITS *its[4];
	unsigned int itsCount;
	Mapping *mappings;              // per vector
	uint32_t vectorCount;
	IOLock *mapLock;
};

#endif
