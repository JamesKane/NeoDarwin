// SPDX-License-Identifier: BSD-2-Clause
// NeoDarwin-Language: expressibility: IOKit classes are C++; this subclasses IOPCIFamily's IOPCIHostBridge.
//
// NeoDarwin's PCI host bridge (docs/kernel/pci.md, P1-09 checkpoint 2): the
// IOPCIHostBridge that Apple's open IOPCIFamily needs from the platform, for
// an ACPI host bridge (PNP0A08, PNP0A03) that NeoDarwinACPIPlatform
// published as an IOACPIPlatformDevice. Intel Macs had AppleACPIPCI and
// Apple silicon Macs have their SoC's PCIe driver, both closed; this is the
// generic ECAM one.
//
// It gives IOPCIFamily's configurator what it enumerates with:
//  - configuration space: ECAM of the bridge's segment (_CBA, else the MCFG
//    allocation for _SEG and _BBN), through nd_pci.h;
//  - the bus range (the producer bus-number descriptor of _CRS, else _BBN
//    to 255) and the windows (acpi-windows): 32- and 64-bit memory,
//    prefetchable memory, and I/O, which Arm decodes as memory at the
//    window's translation offset (ioDeviceMemory);
// and, before the configurator exists, negotiates PCIe native control with
// _OSC. The configurator keeps the BARs, bus numbers and bridge windows the
// firmware assigned when they fit the windows, and assigns the rest.
// Legacy interrupts are routed by the ACPI nub (_PRT, IOACPIPlatformDevice),
// which IOPCIFamily asks through callPlatformFunction. MSIs: the bridge
// answers IOPCIFamily's "GetMessagedInterruptController" and
// "GetMessagedInterruptAddress" with the kernel's ITS-backed controller
// (NeoDarwinPCIMSI.h), and gives it each device's DeviceID from the IORT.
//
// Every IOPCIDevice under the bridge is logged as it is published: address,
// IDs, class, BARs, INTx GSIV and MSI/MSI-X capabilities (without resolving
// its interrupts, which is left to its driver); then a summary once the bus
// is quiet.

#ifndef _NEODARWIN_PCI_HOST_BRIDGE_H
#define _NEODARWIN_PCI_HOST_BRIDGE_H

#include <IOKit/pci/IOPCIBridge.h>
#include <IOKit/pci/IOPCIPrivate.h>
#include <IOKit/acpi/IOACPIPlatformDevice.h>
#include "NeoDarwinPCIMSI.h"

class NeoDarwinPCIHostBridge : public IOPCIHostBridge
{
	OSDeclareDefaultStructors(NeoDarwinPCIHostBridge);

public:
	virtual IOService *probe(IOService *provider, SInt32 *score) APPLE_KEXT_OVERRIDE;
	virtual bool start(IOService *provider) APPLE_KEXT_OVERRIDE;
	virtual void free(void) APPLE_KEXT_OVERRIDE;

	virtual UInt8 firstBusNum(void) APPLE_KEXT_OVERRIDE;
	virtual UInt8 lastBusNum(void) APPLE_KEXT_OVERRIDE;
	virtual IODeviceMemory *ioDeviceMemory(void) APPLE_KEXT_OVERRIDE;
	virtual IOPCIAddressSpace getBridgeSpace(void) APPLE_KEXT_OVERRIDE;

	virtual UInt32 configRead32(IOPCIAddressSpace space, UInt8 offset) APPLE_KEXT_OVERRIDE;
	virtual void configWrite32(IOPCIAddressSpace space, UInt8 offset, UInt32 data) APPLE_KEXT_OVERRIDE;
	virtual UInt16 configRead16(IOPCIAddressSpace space, UInt8 offset) APPLE_KEXT_OVERRIDE;
	virtual void configWrite16(IOPCIAddressSpace space, UInt8 offset, UInt16 data) APPLE_KEXT_OVERRIDE;
	virtual UInt8 configRead8(IOPCIAddressSpace space, UInt8 offset) APPLE_KEXT_OVERRIDE;
	virtual void configWrite8(IOPCIAddressSpace space, UInt8 offset, UInt8 data) APPLE_KEXT_OVERRIDE;

	virtual IOReturn callPlatformFunction(const OSSymbol *functionName, bool waitForFunction,
	    void *param1, void *param2, void *param3, void *param4) APPLE_KEXT_OVERRIDE;

	UInt16 getSegment(void) const { return segment; }
	// Where `device`'s MSI writes go: the IORT's DeviceID and ITS for its
	// requester ID (bus << 8 | device << 3 | function), else the requester
	// ID itself and the first ITS, with route->status saying why.
	void msiRoute(IOPCIDevice *device, NDMSIRoute *route);

	// A published IOPCIDevice (gIOPublishNotification); logs it if it is below this bridge.
	bool devicePublished(void *refCon, IOService *service, IONotifier *notifier);
	// Once the bus is quiet: the summary line.
	void logSummary(void);

private:
	bool readHostBridge(IOACPIPlatformDevice *nub);
	void addWindows(IOService *provider);
	void negotiateOSC(IOACPIPlatformDevice *nub);
	void findCoherence(IOACPIPlatformDevice *nub);
	void pairWithACPI(IOPCIDevice *device);
	bool below(IOService *service);
	UInt32 registerOf(IOPCIAddressSpace space, UInt8 offset);

	IOACPIPlatformDevice *acpi;
	NeoDarwinPCIMessagedInterruptController *msi;
	IODeviceMemory *ioMemory;
	IONotifier *publishNotifier;
	IOLock *logLock;
	const char *ecamSource;
	char path[64];
	char windows[256];
	UInt64 ecam;
	UInt64 ioTranslation;
	UInt32 oscRequested;
	UInt32 oscGranted;
	const char *oscResult;
	const char *coherenceSource;
	const char *iortOutput;
	UInt16 segment;
	UInt8 busFirst, busLast, busMaxSeen;
	UInt32 devices, bridges, intx, msiDevices;
	UInt32 dmaBits;                 // IORT Memory Size Limit, or 0
	bool coherent;
	bool hasIO;
};

#endif
