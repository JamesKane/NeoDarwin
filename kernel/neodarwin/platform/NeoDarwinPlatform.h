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
//    publishes IORTC, which bsd_init waits for. Once AppleARMSMP has started
//    every CPU it measures IPI round trips.
//  - NeoDarwinGICv3 is the IRQ interrupt controller: Group 1 interrupts
//    through ICC_IAR1/EOIR1, IPIs as SGIs. It configures each SPI as it is
//    registered: Group 1 Non-secure, a priority, the trigger its specifier
//    names, routed to the boot CPU. XNU's pe_fiq.c puts the timer on
//    Group 0 (FIQ), or on Group 1 when /arm-io/gic timer-group is 1; this
//    controller then hands it to the kernel's timer handler. SGIs and PPIs
//    are banked per CPU: each cpu nub's specifiers get vectors of their own.
//  - NeoDarwinPSCI is the IOPMGR: CPU on and off through PSCI, over the
//    conduit neoboot chose from the firmware (/chosen psci-conduit).

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

	// Waits until AppleARMSMP has started every CPU (IOAllCPUInitialized),
	// then times cross-calls from the boot CPU to each of the others.
	void measureIPIs(void);

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
	// The calling CPU's SGIs, PPIs and Group 1 CPU interface, on a CPU that
	// has just come up (pe_fiq.c's pe_gic_cpu_init_hook, patch 0017).
	void initCurrentCPU(void);

	// SGIs and PPIs named by a cpu nub are that CPU's own (banked) vectors;
	// every other source uses IOInterruptController's table.
	virtual IOReturn registerInterrupt(IOService *nub, int source, void *target,
	    IOInterruptHandler handler, void *refCon) APPLE_KEXT_OVERRIDE;
	virtual IOReturn unregisterInterrupt(IOService *nub, int source) APPLE_KEXT_OVERRIDE;
	virtual IOReturn enableInterrupt(IOService *nub, int source) APPLE_KEXT_OVERRIDE;
	virtual IOReturn disableInterrupt(IOService *nub, int source) APPLE_KEXT_OVERRIDE;
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
	// One CPU's private interrupts (INTIDs 0-31) and its redistributor.
	struct BankedCPU {
		vm_offset_t rd;              // RD_base; SGI_base follows 64 KiB above
		uint32_t enabled;            // INTIDs enabled; re-applied as the CPU comes up
		IOInterruptVector vectors[32];
	};

	vm_offset_t gicd;
	vm_offset_t gicr;
	// SPIs: bit set for an edge-triggered INTID, from the second cell of
	// its interrupt specifier (ACPI flags; one-cell specifiers are level).
	uint32_t spiEdge[32];
	IOLock *spiLock;                 // distributor read-modify-writes
	uint64_t spiRoute;               // GICD_IROUTER value: the boot CPU
	vm_size_t gicrSize;
	IORegistryEntry *node;
	BankedCPU *banked;               // indexed by logical CPU number
	unsigned int bankedCount;
	IOLock *bankedLock;

	vm_offset_t redistributorFor(uint64_t mpidr);
	vm_offset_t redistributorForCurrentCPU(void);
	void initCPUInterface(void);
	IOInterruptVector *bankedVector(IOService *nub, int source, unsigned int *cpu, IOInterruptVectorNumber *intid);
	void enableBanked(unsigned int cpu, IOInterruptVectorNumber intid);
	void dispatch(IOInterruptVector *vector, IOInterruptVectorNumber intid, vm_offset_t rd);
	bool specifierEdge(IOService *nub, int source, IOInterruptVectorNumber *intid);
	bool spiIsEdge(IOInterruptVectorNumber intid);
	void configureSPI(IOInterruptVectorNumber intid);
	void waitForRWP(void);
};

class NeoDarwinPSCI : public IOPMGR
{
	OSDeclareDefaultStructors(NeoDarwinPSCI);

public:
	// How PSCI is reached: an SMC to EL3 firmware, or an HVC to a
	// hypervisor. neoboot chooses it from the FADT (/chosen psci-conduit).
	enum Conduit { kConduitNone, kConduitSMC, kConduitHVC };

	virtual bool init(OSDictionary *dictionary = NULL) APPLE_KEXT_OVERRIDE;
	Conduit getConduit(void) const { return conduit; }

	virtual void enableCPUCore(unsigned int cpu_id, uint64_t entry_pa) APPLE_KEXT_OVERRIDE;
	virtual void disableCPUCore(unsigned int cpu_id) APPLE_KEXT_OVERRIDE;
	virtual void enableCPUCluster(unsigned int cluster_id) APPLE_KEXT_OVERRIDE;
	virtual void disableCPUCluster(unsigned int cluster_id) APPLE_KEXT_OVERRIDE;
	virtual void initCPUIdle(ml_processor_info_t *info) APPLE_KEXT_OVERRIDE;
	virtual void enterCPUIdle(UInt64 *newIdleTimeoutTicks) APPLE_KEXT_OVERRIDE;
	virtual void exitCPUIdle(UInt64 *newIdleTimeoutTicks) APPLE_KEXT_OVERRIDE;
	virtual void updateCPUIdle(UInt64 *newIdleTimeoutTicks) APPLE_KEXT_OVERRIDE;

private:
	Conduit conduit;
	int64_t call(uint64_t function, uint64_t a1, uint64_t a2, uint64_t a3);
};

#endif
