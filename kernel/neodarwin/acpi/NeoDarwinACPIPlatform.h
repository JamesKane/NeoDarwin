// SPDX-License-Identifier: BSD-2-Clause
// NeoDarwin-Language: expressibility: IOKit classes are C++; this subclasses XNU's IOService.
//
// NeoDarwin's ACPI platform (docs/kernel/acpi.md, P1-09): ACPICA in the
// kernel, over the tables neoboot copied (/chosen acpi-tables), and an
// IOACPIPlatformDevice nub for each present device in \_SB.
//
// NeoDarwinPlatformExpert creates it once IOKit is up and it runs on a
// thread of its own: it initialises ACPICA (hardware-reduced: no SCI),
// loads the tables, runs _INI and _REG, walks the namespace and publishes
// the nubs. Like the platform expert it is compiled into the kernel (patch
// 0018), since kcgen links no kexts until M5.

#ifndef _NEODARWIN_ACPI_PLATFORM_H
#define _NEODARWIN_ACPI_PLATFORM_H

#include <IOKit/IOService.h>
#include <IOKit/IOLocks.h>
#include <IOKit/acpi/IOACPIPlatformDevice.h>

// Interrupt flags, per interrupt in acpi-interrupt-flags and as the second
// cell of each IOInterruptSpecifiers entry (NeoDarwinGICv3 reads bit 0).
#define ND_ACPI_IRQ_EDGE       0x1
#define ND_ACPI_IRQ_ACTIVE_LOW 0x2
#define ND_ACPI_IRQ_SHARED     0x4
#define ND_ACPI_IRQ_WAKE       0x8

class NeoDarwinACPIPlatform : public IOService
{
	OSDeclareDefaultStructors(NeoDarwinACPIPlatform);

public:
	// Called by NeoDarwinPlatformExpert::start. Does nothing when the
	// loader passed no ACPI tables (/chosen acpi-rsdp).
	static void startFromPlatformExpert(IOService *platformExpert);

	virtual bool start(IOService *provider) APPLE_KEXT_OVERRIDE;

	// An ACPI table as OSData, kept for the platform's lifetime
	// (IOACPIPlatformDevice::getACPITableData).
	const OSData *getTableData(const char *signature, UInt32 instance);

	// The namespace walk's work for one present device (an ACPI_HANDLE).
	void publish(void *handle, UInt32 status);

	// The nub published for the device at an absolute namespace path
	// (\_SB.L000), or NULL. The registry holds it.
	IOACPIPlatformDevice *nubForPath(const char *path);
	// The GIC's interrupt controller name, which interrupt specifiers name.
	const OSSymbol *getGICName(void) const { return gicName; }

private:
	bool initACPICA(void);
	void registerECAM(void);
	void publishDevices(void);
	void addResources(IOACPIPlatformDevice *nub, void *handle, bool bridge, char *summary, size_t summarySize);
	void describeHostBridge(IOACPIPlatformDevice *nub);

	OSDictionary *tables;
	OSArray *hostBridges;           // during the walk
	IOLock *tablesLock;
	const OSSymbol *gicName;
	UInt32 gicPHandle;
	UInt32 tableCount;
	UInt32 published;
	UInt32 addressOnly;
	bool verbose;
};

// The same, as a plain function for NeoDarwinPlatformExpert (kernel/neodarwin/platform).
void nd_acpi_platform_start(IOService *platformExpert);

#endif
