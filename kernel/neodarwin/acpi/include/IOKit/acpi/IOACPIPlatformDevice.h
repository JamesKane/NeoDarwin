/* SPDX-License-Identifier: BSD-2-Clause */
/* NeoDarwin-Language: expressibility: IOKit classes are C++; this declares an IOService subclass. */
/*
 * IOACPIPlatformDevice: the nub NeoDarwin's ACPI platform publishes for each
 * present device in the ACPI namespace (docs/kernel/acpi.md).
 *
 * The class name and the methods below have the names and signatures of
 * Apple's <IOKit/acpi/IOACPIPlatformDevice.h> (Kernel.framework, Intel
 * era), so that a driver written against it compiles here. It is a subset,
 * written from the published interface: object evaluation, table access,
 * the global lock and the device's identity. Power management, fixed
 * events, GPEs, address-space handlers and I/O-port helpers are not
 * declared; hardware-reduced Arm machines have no GPEs or I/O ports. The
 * vtable is NeoDarwin's own: binary Intel kexts do not load.
 *
 * Matching: IONameMatch compares against the device's _HID and each _CID
 * (the "compatible" property), e.g. "PNP0A08", "LNRO0005", "ARMH0011".
 */

#ifndef _IOKIT_IOACPIPLATFORMDEVICE_H
#define _IOKIT_IOACPIPLATFORMDEVICE_H

#include <IOKit/IOPlatformExpert.h>
#include <IOKit/acpi/IOACPITypes.h>

class IOACPIPlatformDevice : public IOPlatformDevice
{
	OSDeclareDefaultStructors(IOACPIPlatformDevice);

protected:
	void *_deviceHandle;        /* ACPI_HANDLE */
	UInt32 _deviceType;
	UInt32 _deviceStatus;       /* _STA, or 0x0F when the device has none */
	IOService *_platform;       /* NeoDarwinACPIPlatform */

public:
	virtual bool init(IOService *platform, void *handle, OSDictionary *properties);

	virtual bool compareName(OSString *name, OSString **matched = NULL) const APPLE_KEXT_OVERRIDE;
	/* Resources are set when the nub is published: IODeviceMemory from
	 * _CRS, and interrupt specifiers on the GIC. */
	virtual IOReturn getResources(void) APPLE_KEXT_OVERRIDE;

	virtual void *getDeviceHandle(void) const;
	virtual UInt32 getDeviceStatus(void) const;

	enum {
		kTypeDevice         = 0,
		kTypeProcessor      = 1,
		kTypePowerResource  = 2
	};
	virtual UInt32 getDeviceType(void) const;
	virtual void setDeviceType(UInt32 deviceType);

	/* Method (object) evaluation. Names are relative to the device
	 * ("_CRS", "_DSM") or absolute ("\\_SB.PCI0._CBA"). Arguments and
	 * results convert as OSNumber <-> Integer, OSString <-> String,
	 * OSData <-> Buffer, OSArray <-> Package. */
	virtual IOReturn validateObject(const OSSymbol *objectName);
	virtual IOReturn validateObject(const char *objectName);

	virtual IOReturn evaluateObject(const OSSymbol *objectName, OSObject **result = NULL,
	    OSObject *params[] = NULL, IOItemCount paramCount = 0, IOOptionBits options = 0);
	virtual IOReturn evaluateObject(const char *objectName, OSObject **result = NULL,
	    OSObject *params[] = NULL, IOItemCount paramCount = 0, IOOptionBits options = 0);

	virtual IOReturn evaluateInteger(const OSSymbol *objectName, UInt32 *resultInt32,
	    OSObject *params[] = NULL, IOItemCount paramCount = 0, IOOptionBits options = 0);
	virtual IOReturn evaluateInteger(const char *objectName, UInt32 *resultInt32,
	    OSObject *params[] = NULL, IOItemCount paramCount = 0, IOOptionBits options = 0);
	virtual IOReturn evaluateInteger(const OSSymbol *objectName, UInt64 *resultInt64,
	    OSObject *params[] = NULL, IOItemCount paramCount = 0, IOOptionBits options = 0);
	virtual IOReturn evaluateInteger(const char *objectName, UInt64 *resultInt64,
	    OSObject *params[] = NULL, IOItemCount paramCount = 0, IOOptionBits options = 0);

	/* The table with this signature ("MCFG", "IORT", "SSDT", ...), the
	 * tableInstance'th of them from 0. Owned by the platform; don't release. */
	virtual const OSData *getACPITableData(const char *tableName, UInt32 tableInstance = 0) const;

	/* Hardware-reduced ACPI has no global lock (no FACS): nothing else can
	 * hold it, so these succeed at once, with a token of 0. */
	virtual IOReturn acquireGlobalLock(UInt32 *lockToken, const mach_timespec_t *timeout = NULL);
	virtual void releaseGlobalLock(UInt32 lockToken);

	/* On a PCI host bridge (one with _PRT), the platform functions
	 * IOPCIFamily sends up the provider chain to route legacy interrupts
	 * (NeoDarwin; docs/kernel/pci.md): "ResolvePCIInterrupt" (requesting
	 * bridge's provider, device number, pin 0-3, UInt32 *gsiv) swizzles the
	 * pin across PCI-to-PCI bridges and looks it up in _PRT, directly or
	 * through a link device's _CRS; "SetDeviceInterrupts" (IOPCIDevice,
	 * UInt32 *gsivs, count) makes them the device's interrupt specifiers
	 * on the GIC, level-triggered and shareable. Anything else goes up. */
	using IOPlatformDevice::callPlatformFunction;
	virtual IOReturn callPlatformFunction(const OSSymbol *functionName, bool waitForFunction,
	    void *param1, void *param2, void *param3, void *param4) APPLE_KEXT_OVERRIDE;

protected:
	IOReturn resolvePCIInterrupt(IOService *requester, UInt32 device, UInt32 pin, UInt32 *gsiv);
	IOReturn setPCIDeviceInterrupts(IOService *device, const UInt32 *gsivs, UInt32 count);
};

#endif /* _IOKIT_IOACPIPLATFORMDEVICE_H */
