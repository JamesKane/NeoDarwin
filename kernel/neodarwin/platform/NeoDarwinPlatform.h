// SPDX-License-Identifier: BSD-2-Clause
// NeoDarwin-Language: expressibility: IOKit classes are C++; these subclass XNU's IOPlatformExpert, IOInterruptController and IOPMGR.
//
// NeoDarwin's in-kernel platform layer for generic Arm (SBSA/SBBR) machines
// (docs/kernel/arm64-sbsa-bringup.md §2.3). Compiled into the kernel, as
// iokit/Kernel/arm/AppleARMSMP.cpp is, so boot needs no kexts:
//
//  - NeoDarwinPlatformExpert matches the device tree root "NeoDarwin,sbsa",
//    publishes nubs from the tree, and creates the two classes below. It is
//    also the RTC: it keeps the time of day neoboot read from UEFI and
//    publishes IORTC, which bsd_init waits for.
//  - NeoDarwinGICv3 is the IRQ interrupt controller: Group 1 interrupts
//    through ICC_IAR1/EOIR1, IPIs as SGIs. XNU's pe_fiq.c keeps the timer on
//    Group 0 (FIQ).
//  - NeoDarwinPSCI is the IOPMGR: CPU on and off through PSCI.

#ifndef _NEODARWIN_PLATFORM_H
#define _NEODARWIN_PLATFORM_H

#include <IOKit/IOPlatformExpert.h>
#include <IOKit/IOInterruptController.h>
#include <IOKit/IOPMGR.h>
#include <IOKit/IOLocks.h>

class NeoDarwinGICv3;
class NeoDarwinPSCI;

class NeoDarwinPlatformExpert : public IODTPlatformExpert
{
	OSDeclareDefaultStructors(NeoDarwinPlatformExpert);

public:
	virtual bool start(IOService *provider) APPLE_KEXT_OVERRIDE;
	virtual const char *deleteList(void) APPLE_KEXT_OVERRIDE;
	virtual const char *excludeList(void) APPLE_KEXT_OVERRIDE;
	virtual void getUTCTimeOfDay(clock_sec_t *secs, clock_nsec_t *nsecs) APPLE_KEXT_OVERRIDE;
	virtual void setUTCTimeOfDay(clock_sec_t secs, clock_nsec_t nsecs) APPLE_KEXT_OVERRIDE;

private:
	void startClock(void);

	NeoDarwinGICv3 *gic;
	NeoDarwinPSCI *psci;
	// The time of day at a counter value; now is that plus the ticks since.
	IOSimpleLock *clockLock;
	clock_sec_t clockSecs;
	clock_nsec_t clockNsecs;
	uint64_t clockCounter;
};

class NeoDarwinGICv3 : public IOInterruptController
{
	OSDeclareDefaultStructors(NeoDarwinGICv3);

public:
	// Maps the distributor and redistributors named by `node` (/arm-io/gic)
	// and prepares the boot CPU.
	bool initWithNode(IORegistryEntry *node);
	// Registers as the child of XNU's PassthruInterruptController (blocks
	// until it exists; call from its own thread).
	void attachToCPUController(void);

	virtual IOReturn getInterruptType(IOService *nub, int source, int *interruptType) APPLE_KEXT_OVERRIDE;
	virtual IOInterruptAction getInterruptHandlerAddress(void) APPLE_KEXT_OVERRIDE;
	virtual IOReturn handleInterrupt(void *refCon, IOService *nub, int source) APPLE_KEXT_OVERRIDE;
	virtual bool vectorCanBeShared(IOInterruptVectorNumber vectorNumber, IOInterruptVector *vector) APPLE_KEXT_OVERRIDE;
	virtual void initVector(IOInterruptVectorNumber vectorNumber, IOInterruptVector *vector) APPLE_KEXT_OVERRIDE;
	virtual int getVectorType(IOInterruptVectorNumber vectorNumber, IOInterruptVector *vector) APPLE_KEXT_OVERRIDE;
	virtual void disableVectorHard(IOInterruptVectorNumber vectorNumber, IOInterruptVector *vector) APPLE_KEXT_OVERRIDE;
	virtual void enableVector(IOInterruptVectorNumber vectorNumber, IOInterruptVector *vector) APPLE_KEXT_OVERRIDE;
	virtual void sendIPI(unsigned int cpu_id, bool deferred) APPLE_KEXT_OVERRIDE;
	virtual void cancelDeferredIPI(unsigned int cpu_id) APPLE_KEXT_OVERRIDE;

private:
	vm_offset_t gicd;
	vm_offset_t gicr;
	vm_size_t gicrSize;
	IORegistryEntry *node;
	vm_offset_t redistributorForCurrentCPU(void);
	void initCPUInterface(void);
};

class NeoDarwinPSCI : public IOPMGR
{
	OSDeclareDefaultStructors(NeoDarwinPSCI);

public:
	virtual void enableCPUCore(unsigned int cpu_id, uint64_t entry_pa) APPLE_KEXT_OVERRIDE;
	virtual void disableCPUCore(unsigned int cpu_id) APPLE_KEXT_OVERRIDE;
	virtual void enableCPUCluster(unsigned int cluster_id) APPLE_KEXT_OVERRIDE;
	virtual void disableCPUCluster(unsigned int cluster_id) APPLE_KEXT_OVERRIDE;
	virtual void initCPUIdle(ml_processor_info_t *info) APPLE_KEXT_OVERRIDE;
	virtual void enterCPUIdle(UInt64 *newIdleTimeoutTicks) APPLE_KEXT_OVERRIDE;
	virtual void exitCPUIdle(UInt64 *newIdleTimeoutTicks) APPLE_KEXT_OVERRIDE;
	virtual void updateCPUIdle(UInt64 *newIdleTimeoutTicks) APPLE_KEXT_OVERRIDE;
};

#endif
