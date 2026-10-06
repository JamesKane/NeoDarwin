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
//    LPIs (INTIDs 8192 and up) come through the same IRQ loop: the
//    configuration and pending tables are set up on first use, when PCI
//    asks for MSIs (docs/kernel/gic-its.md), and an LPI goes to the one
//    handler registered for LPIs (the PCI MSI controller).
//  - NeoDarwinGICv3ITS is a GIC Interrupt Translation Service: its device
//    and collection tables and command queue, and the commands that map a
//    PCI requester's MSI writes (DeviceID, EventID) to LPIs.
//  - NeoDarwinPSCI is the IOPMGR: CPU on and off through PSCI, over the
//    conduit neoboot chose from the firmware (/chosen psci-conduit).

#ifndef _NEODARWIN_PLATFORM_H
#define _NEODARWIN_PLATFORM_H

#include <IOKit/IOPlatformExpert.h>
#include <IOKit/IOInterruptController.h>
#include <IOKit/IOPMGR.h>
#include <IOKit/IOLocks.h>

class IOBufferMemoryDescriptor;
class NeoDarwinGICv3;
class NeoDarwinPSCI;

// Memory the GIC reads and writes itself (LPI configuration and pending
// tables, ITS tables, command queue, ITTs): physically contiguous, zeroed,
// aligned as asked, below 2^48. If the GIC accesses it non-cacheably
// (a shareability attribute that reads back as non-shareable), whatever the
// CPU writes must be cleaned to the point of coherency: ndGICFlush.
struct NDGICTable {
	IOBufferMemoryDescriptor *md;
	uint8_t *va;
	uint64_t pa;
	size_t size;
};
bool ndGICTableAlloc(NDGICTable *table, size_t size, size_t align);
void ndGICTableFree(NDGICTable *table);
void ndGICFlush(const void *va, size_t size);

// An LPI's handler: called in the IRQ loop with the INTID, before the GIC
// completes it (EOI). LPIs are edge-triggered and have no active state.
typedef void (*NDLPIHandler)(void *target, uint32_t intid);

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

	// The controller the platform expert created, once it exists.
	static NeoDarwinGICv3 *instance(void);

	// LPIs (docs/kernel/gic-its.md). initLPIs allocates the shared
	// configuration table and a pending table per CPU, points every
	// redistributor at them and enables LPIs; it is idempotent, and false
	// (with the reason in `why`) when the GIC has no LPIs. CPUs that come up
	// later enable theirs in initCurrentCPU.
	bool initLPIs(char *why, size_t whyLength);
	// The LPIs the tables cover: INTIDs 8192 to 8192 + lpiCount() - 1.
	uint32_t lpiCount(void) const { return lpiNum; }
	// Priority and enable in the configuration table. The ITS must then be
	// told (INV) before the redistributor is sure to see it.
	void configureLPI(uint32_t intid, bool enable);
	void setLPIHandler(NDLPIHandler handler, void *target);
	// The target of an ITS collection for logical CPU `cpu`: the
	// redistributor's physical address (GITS_TYPER.PTA 1) or its processor
	// number (PTA 0), as MAPC and SYNC encode it (bits 51:16). ~0 if the
	// kernel does not run that CPU.
	uint64_t collectionTarget(unsigned int cpu, bool pta);
	unsigned int cpuCount(void) const { return bankedCount; }
	unsigned int bootCPU(void) const { return bootCPUNumber; }
	// Boot-arg nd_gic_lpi_nc=1: treat every GIC table as if the GIC had
	// read back non-shareable (non-cacheable, cleaned to PoC), to exercise
	// that path where the GIC is coherent (QEMU).
	bool forceNonCacheable(void) const { return lpiForceNC; }
	// What initLPIs chose, for the log.
	const char *lpiAttributes(void) const { return lpiAttrs; }

private:
	// One CPU's private interrupts (INTIDs 0-31) and its redistributor.
	struct BankedCPU {
		vm_offset_t rd;              // RD_base; SGI_base follows 64 KiB above
		uint32_t enabled;            // INTIDs enabled; re-applied as the CPU comes up
		IOInterruptVector vectors[32];
	};

	vm_offset_t gicd;
	vm_offset_t gicr;
	uint64_t gicrPhys;              // physical address of the first redistributor frame
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
	unsigned int bootCPUNumber;

	// LPIs. lpiReady is published (release) once the tables exist.
	NDGICTable lpiProp;              // configuration: a byte per LPI
	NDGICTable *lpiPend;             // pending: per logical CPU
	uint64_t lpiPropBaser;           // GICR_PROPBASER as read back
	uint32_t lpiNum;
	uint8_t lpiIDBits;
	bool lpiReady;
	bool lpiPropFlush;               // the configuration table is read non-cacheably
	bool lpiPendNonCacheable;
	bool lpiForceNC;
	IOLock *lpiInitLock;
	IOSimpleLock *lpiRDLock;         // programming a redistributor's LPI registers
	NDLPIHandler lpiHandler;
	void *lpiTarget;
	uint64_t lpiSpurious;
	char lpiAttrs[96];

	bool enableLPIsOn(unsigned int cpu);

	vm_offset_t redistributorFor(uint64_t mpidr);
	vm_offset_t redistributorForCurrentCPU(void);
	void initCPUInterface(void);
	void earlyIRQSelfTest(void);
	IOInterruptVector *bankedVector(IOService *nub, int source, unsigned int *cpu, IOInterruptVectorNumber *intid);
	void enableBanked(unsigned int cpu, IOInterruptVectorNumber intid);
	void dispatch(IOInterruptVector *vector, IOInterruptVectorNumber intid, vm_offset_t rd);
	bool specifierEdge(IOService *nub, int source, IOInterruptVectorNumber *intid);
	bool spiIsEdge(IOInterruptVectorNumber intid);
	void configureSPI(IOInterruptVectorNumber intid);
	void waitForRWP(void);
};

// A GICv3 Interrupt Translation Service (Arm IHI 0069 chapter 5): maps
// (DeviceID, EventID), the requester and data of an MSI write to
// GITS_TRANSLATER, to an LPI and a collection (a redistributor). One
// collection per CPU the kernel runs, collection N being logical CPU N.
class NeoDarwinGICv3ITS : public OSObject
{
	OSDeclareDefaultStructors(NeoDarwinGICv3ITS);

public:
	// Resets and programs the ITS at physical `base` (MADT GIC ITS
	// structure, translation ID `id`): tables, command queue, collections.
	// The GIC's LPIs must be up (initLPIs). NULL, with the reason in `why`,
	// on failure.
	static NeoDarwinGICv3ITS *withAddress(NeoDarwinGICv3 *gic, uint64_t base, uint32_t id, char *why, size_t whyLength);

	uint32_t getID(void) const { return itsID; }
	uint64_t getBase(void) const { return itsPhys; }
	// The MSI doorbell: GITS_TRANSLATER's physical address.
	uint64_t getDoorbell(void) const { return itsPhys + 0x10040; }
	uint32_t deviceIDBits(void) const { return devBits; }
	// One line for the log: tables and their attributes.
	const char *describe(void) const { return summary; }

	// MAPD with an ITT for `events` EventIDs (at least 2, a power of two).
	// A device already mapped with a large enough ITT is left as it is.
	// Commands are serialised inside; callers serialise mapDevice (the
	// MSI controller holds its lock), and none of these may be called in
	// interrupt context: each waits for the ITS, up to a second.
	bool mapDevice(uint32_t deviceID, uint32_t events);
	// MAPTI (EventID -> LPI on collection `cpu`), then INV and SYNC.
	bool mapEvent(uint32_t deviceID, uint32_t eventID, uint32_t lpi, unsigned int cpu);
	// DISCARD, then SYNC.
	bool discardEvent(uint32_t deviceID, uint32_t eventID, unsigned int cpu);

	virtual void free(void) APPLE_KEXT_OVERRIDE;

private:
	struct Device {
		uint32_t id;
		uint32_t events;
		NDGICTable itt;
	};

	bool start(NeoDarwinGICv3 *gic, uint64_t base, uint32_t id, char *why, size_t whyLength);
	bool setupTable(unsigned int n, char *why, size_t whyLength);
	bool setupCommandQueue(char *why, size_t whyLength);
	bool deviceTableCovers(uint32_t deviceID);
	bool command(const uint64_t cmd[4], const char *what);
	bool sync(unsigned int cpu);

	NeoDarwinGICv3 *gic;
	vm_offset_t regs;
	uint64_t itsPhys;
	uint32_t itsID;
	uint64_t typer;
	uint32_t devBits;
	uint32_t eventBits;
	uint32_t ittEntrySize;
	bool pta;
	IOLock *lock;
	NDGICTable queue;
	bool queueFlush;
	uint32_t queueNext;              // byte offset of the next command
	NDGICTable tables[8];            // GITS_BASER<n>'s memory
	bool tableFlush[8];
	// The device table: flat, or two-level (indirect) with level-2 pages
	// allocated as DeviceIDs are mapped.
	int devTable;                    // its BASER index, or -1
	bool devIndirect;
	uint32_t devPageSize;
	uint32_t devEntrySize;
	uint32_t devL1Entries;
	NDGICTable *devL2;
	Device *devices;
	unsigned int deviceCount, deviceCapacity;
	char summary[200];
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
