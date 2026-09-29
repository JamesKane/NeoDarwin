// SPDX-License-Identifier: BSD-2-Clause
// NeoDarwin-Language: expressibility: IOKit classes are C++, and the GIC's CPU interface is reached through system registers.
//
// GICv3 IRQ controller (Arm IHI 0069). Vector numbers are GIC INTIDs: the
// device tree gives every interrupt specifier as one cell holding the INTID.
// SGI 0 carries IPIs and SGI 1 deferred IPIs (the CPU node's three-entry
// "interrupts": IPI, PMI, deferred IPI; AppleARMSMP.cpp). FreeBSD's
// sys/arm64/arm64/gic_v3.c was the reference for the register sequences.
//
// SGIs and PPIs (INTIDs 0-31) are banked: every CPU has its own, in its own
// redistributor. Every cpu node names the same SGIs, and AppleARMSMP
// registers them once per CPU, so a cpu nub's INTID below 32 gets a vector
// of that CPU's (BankedCPU) and is enabled in that CPU's redistributor. The
// IRQ loop dispatches an SGI or PPI through the vectors of the CPU taking
// it. A PPI a non-cpu nub names falls back to the shared table, enabled on
// the CPU that registers it.
//
// A secondary CPU's redistributor is programmed twice: when AppleARMSMP
// registers its IPIs, before it runs, and again on the CPU itself as it
// comes up (initCurrentCPU, from pe_init_fiq through patch 0017's hook).
// The second matters with TrustZone firmware: TF-A resets the SGI and PPI
// configuration of a redistributor when PSCI CPU_ON powers its CPU.
//
// SPIs (INTIDs 32-1019: devices, INTx of PCI) are configured in the
// distributor when first registered (initVector): disabled, Group 1
// (GICD_IGROUPR; firmware leaves them in Group 0 on a GIC with one security
// state, DS == 1, where Group 0 is this kernel's FIQ and the timer's, and on
// DS == 0 Non-secure writes to the group are ignored and TF-A has made every
// SPI Group 1 Non-secure already), priority 0x80, level or edge as the
// specifier's second cell says (ACPI flags, bit 0 edge; a one-cell device
// tree specifier is level), routed to the boot CPU (GICD_IROUTER,
// IRM = 0). Level SPIs can be shared (PCI INTx): IOInterruptController
// then puts an IOSharedInterruptController on the vector.

#include "NeoDarwinPlatform.h"
#include <IOKit/IODeviceTreeSupport.h>
#include <IOKit/IOLib.h>
#include <kern/cpu_number.h>
#include <machine/machine_routines.h>
#include <pexpert/arm64/board_config.h>
#include <pexpert/device_tree.h>

// Declared in osfmk-private headers IOKit does not see.
extern "C" vm_offset_t pe_arm_get_soc_base_phys(void);
extern "C" vm_offset_t ml_io_map(vm_offset_t phys_addr, vm_size_t size);

// The virtual timer's INTID and group, from /arm-io/gic timer-ppi and
// timer-group (pexpert/arm/pe_fiq.c, patch 0016). On Group 1 it arrives here
// as an IRQ and goes to the kernel's timer handler (osfmk/arm64/sleh.c),
// which also takes it until this controller is attached.
extern "C" uint32_t pe_gic_timer_intid;
extern "C" uint32_t pe_gic_timer_group;
extern "C" bool sleh_gic_irq_controller_attached;
extern "C" uint64_t sleh_gic_timer_irqs;
extern "C" void sleh_gic_timer_interrupt(void);
// Called at the end of pe_init_fiq() on each CPU as it comes up (patch 0017).
extern "C" void (*pe_gic_cpu_init_hook)(void);

// Barrier domains (Arm ARM C6.2): ISB SY and DSB ISHST.
#define ND_ISB_SY    0xf
#define ND_DSB_ISHST 0xa

#define super IOInterruptController
OSDefineMetaClassAndStructors(NeoDarwinGICv3, IOInterruptController);

// Distributor and redistributor registers beyond those SBSA.h defines.
#define ND_GICD_CTLR_ENABLEGRP1   0x2
#define ND_GICD_CTLR_ARE          0x10
#define ND_GICD_CTLR_DS           0x40      // reads 0 in the Non-secure view of two security states
#define ND_GICD_CTLR_RWP          0x80000000u
#define ND_GICD_IGROUPR           0x080
#define ND_GICD_ISENABLER         0x100
#define ND_GICD_ICENABLER         0x180
#define ND_GICD_IPRIORITYR        0x400     // a byte per INTID
#define ND_GICD_ICFGR             0xc00     // two bits per INTID; bit 1 set: edge
#define ND_GICD_IROUTER           0x6000    // 64 bits per INTID: Aff3.Aff2.Aff1.Aff0, IRM
#define ND_GIC_FIRST_SPI          32
#define ND_SPEC_EDGE              0x1       // specifier cell 2: ACPI flags (acpi.md)
#define ND_GICR_ICENABLER0        0x10180
#define ND_GICR_IPRIORITYR0       0x10400   // SGI_base + 0x400, a byte per INTID
#define ND_GIC_PRIORITY_DEFAULT   0x80      // below PMR (0xff) in either security view
#define ND_GIC_NUM_INTIDS         1020      // SGIs, PPIs and SPIs; 1020-1023 are special
#define ND_GIC_NUM_BANKED         32        // SGIs 0-15 and PPIs 16-31
#define ND_SGI_IPI                0
#define ND_SGI_DEFERRED_IPI       1

static NeoDarwinGICv3 *gNeoDarwinGIC;

static inline uint32_t
rd32(vm_offset_t a)
{
	return *(volatile uint32_t *)a;
}

static inline uint64_t
rd64(vm_offset_t a)
{
	return *(volatile uint64_t *)a;
}

static inline void
wr32(vm_offset_t a, uint32_t v)
{
	*(volatile uint32_t *)a = v;
}

static inline void
wr8(vm_offset_t a, uint8_t v)
{
	*(volatile uint8_t *)a = v;
}

static inline uint8_t
rd8(vm_offset_t a)
{
	return *(volatile uint8_t *)a;
}

static inline void
wr64(vm_offset_t a, uint64_t v)
{
	*(volatile uint64_t *)a = v;
}

extern "C" void
nd_gic_cpu_init(void)
{
	gNeoDarwinGIC->initCurrentCPU();
}

bool
NeoDarwinGICv3::initWithNode(IORegistryEntry *gicNode)
{
	if (!super::init()) {
		return false;
	}
	OSData *reg = OSDynamicCast(OSData, gicNode->getProperty("reg"));
	if (reg == NULL || reg->getLength() < 4 * sizeof(uint64_t)) {
		return false;
	}
	const uint64_t *r = (const uint64_t *)reg->getBytesNoCopy();
	vm_offset_t soc = pe_arm_get_soc_base_phys();
	// At most one redistributor frame per possible CPU: firmware may reserve
	// far more address space than there are CPUs.
	gicrSize = r[3] < (vm_size_t)MAX_CPUS * GICR_PE_SIZE ? r[3] : (vm_size_t)MAX_CPUS * GICR_PE_SIZE;
	gicd = ml_io_map(soc + r[0], r[1]);
	gicr = ml_io_map(soc + r[2], gicrSize);
	if (gicd == 0 || gicr == 0) {
		return false;
	}
	node = gicNode;

	vectors = IONewZero(IOInterruptVector, ND_GIC_NUM_INTIDS);
	if (vectors == NULL) {
		return false;
	}
	for (int i = 0; i < ND_GIC_NUM_INTIDS; i++) {
		vectors[i].interruptLock = IOLockAlloc();
		if (vectors[i].interruptLock == NULL) {
			return false;
		}
	}

	// The CPUs the kernel runs, each with its redistributor.
	const ml_topology_info_t *topology = ml_get_topology_info();
	bankedCount = topology->max_cpu_id + 1;
	banked = IONewZero(BankedCPU, bankedCount);
	bankedLock = IOLockAlloc();
	if (banked == NULL || bankedLock == NULL) {
		return false;
	}
	for (unsigned int i = 0; i < topology->num_cpus; i++) {
		const ml_topology_cpu_t *cpu = &topology->cpus[i];
		banked[cpu->cpu_id].rd = redistributorFor(cpu->phys_id);
	}
	// SPIs go to the boot CPU: GICD_IROUTER has the MPIDR's affinity
	// fields where the MPIDR has them (Aff3 in bits 39:32), IRM clear.
	spiLock = IOLockAlloc();
	if (spiLock == NULL) {
		return false;
	}
	uint64_t boot = topology->boot_cpu != NULL ? topology->boot_cpu->phys_id : topology->cpus[0].phys_id;
	spiRoute = boot & 0xff00ffffffULL;

	// Affinity routing and Group 1, alongside the Group 0 pe_fiq.c enabled.
	wr32(gicd + GICD_CTLR, rd32(gicd + GICD_CTLR) | ND_GICD_CTLR_ARE | ND_GICD_CTLR_ENABLEGRP1);
	initCPUInterface();

	// Secondary CPUs finish their GIC setup through us as they come up.
	gNeoDarwinGIC = this;
	__atomic_store_n(&pe_gic_cpu_init_hook, &nd_gic_cpu_init, __ATOMIC_RELEASE);

	// Publish under the name device-tree interrupt specifiers resolve to.
	const OSSymbol *name = IODTInterruptControllerName(gicNode);
	if (name == NULL) {
		return false;
	}
	IOReturn ret = getPlatform()->registerInterruptController((OSSymbol *)name, this);
	name->release();
	return ret == kIOReturnSuccess;
}

// The redistributor whose GICR_TYPER affinity (Aff3.Aff2.Aff1.Aff0) matches
// `mpidr`. Any CPU may read any redistributor's registers.
vm_offset_t
NeoDarwinGICv3::redistributorFor(uint64_t mpidr)
{
	uint32_t aff = (uint32_t)(((mpidr >> 32) & 0xff) << 24 | (mpidr & 0xffffff));
	for (vm_offset_t off = 0; off + GICR_PE_SIZE <= gicrSize; off += GICR_PE_SIZE) {
		uint64_t typer = rd64(gicr + off + GICR_TYPER);
		if ((uint32_t)(typer >> GICR_TYPER_AFFINITY_VALUE_SHIFT) == aff) {
			return gicr + off;
		}
		if (typer & GICR_TYPER_LAST) {
			break;
		}
	}
	panic("NeoDarwinGICv3: no redistributor for MPIDR 0x%llx", mpidr);
}

vm_offset_t
NeoDarwinGICv3::redistributorForCurrentCPU(void)
{
	unsigned int cpu = (unsigned int)cpu_number();
	if (banked != NULL && cpu < bankedCount && banked[cpu].rd != 0) {
		return banked[cpu].rd;
	}
	return redistributorFor(__builtin_arm_rsr64("MPIDR_EL1"));
}

// Group 1 on this CPU: pe_fiq.c has already woken the redistributor, put the
// timer on the group the device tree names (Group 0 by default), SGIs and
// other PPIs on Group 1, enabled system-register access and set PMR.
void
NeoDarwinGICv3::initCPUInterface(void)
{
	__builtin_arm_wsr64("ICC_BPR1_EL1", 0);
	__builtin_arm_wsr64("ICC_IGRPEN1_EL1", 1);
	__builtin_arm_isb(ND_ISB_SY);
}

// On a CPU that has just come up, from pe_init_fiq() (patch 0017), before
// it enables interrupts and sends itself its first IPI
// (AppleARMSMP.cpp PE_cpu_machine_init). Re-applies the SGIs and PPIs
// registered for it, since PSCI firmware may have reset its redistributor
// when it powered the CPU on. Their group is already Group 1 Non-secure:
// pe_init_fiq() wrote GICR_IGROUPR0 when DS == 1, and the Secure firmware
// makes them so when DS == 0.
void
NeoDarwinGICv3::initCurrentCPU(void)
{
	unsigned int cpu = (unsigned int)cpu_number();
	if (cpu < bankedCount && banked[cpu].rd != 0) {
		vm_offset_t rd = banked[cpu].rd;
		uint32_t enabled = __atomic_load_n(&banked[cpu].enabled, __ATOMIC_ACQUIRE);
		for (unsigned int intid = 0; intid < ND_GIC_NUM_BANKED; intid++) {
			if (enabled & (1u << intid)) {
				wr8(rd + ND_GICR_IPRIORITYR0 + intid, ND_GIC_PRIORITY_DEFAULT);
			}
		}
		__builtin_arm_dsb(ND_DSB_ISHST);
		wr32(rd + GICR_ISENABLER0, enabled);
	}
	initCPUInterface();
}

void
NeoDarwinGICv3::attachToCPUController(void)
{
	// Blocks until XNU's cpu_boot_thread has created the passthru controller.
	IOInterruptController *cpuController = getPlatform()->lookUpInterruptController(
		(OSSymbol *)gPlatformInterruptControllerName);
	cpuController->registerInterrupt(this, 0, this,
	    (IOInterruptHandler)getInterruptHandlerAddress(), NULL);
	// sleh_irq may now reach handleInterrupt through PE_handle_ext_interrupt().
	__atomic_store_n(&sleh_gic_irq_controller_attached, true, __ATOMIC_RELEASE);
	if (pe_gic_timer_group == 1) {
		IOLog("NeoDarwinGICv3: timer PPI %u on Group 1 (IRQ); %llu timer interrupts taken as IRQs so far\n",
		    pe_gic_timer_intid, __atomic_load_n(&sleh_gic_timer_irqs, __ATOMIC_RELAXED));
	} else {
		IOLog("NeoDarwinGICv3: timer PPI %u on Group 0 (FIQ)\n", pe_gic_timer_intid);
	}
}

// The banked vector a cpu nub's SGI or PPI source names, and whose CPU it
// is; NULL for any other source.
IOInterruptVector *
NeoDarwinGICv3::bankedVector(IOService *nub, int source, unsigned int *cpu, IOInterruptVectorNumber *intid)
{
	OSData *vectorData = nub->_interruptSources[source].vectorData;
	IOInterruptVectorNumber n = *(const IOInterruptVectorNumber *)vectorData->getBytesNoCopy();
	*intid = n;
	if (n < 0 || n >= ND_GIC_NUM_BANKED) {
		return NULL;
	}
	OSData *type = OSDynamicCast(OSData, nub->getProperty("device_type"));
	OSData *reg = OSDynamicCast(OSData, nub->getProperty("reg"));
	if (type == NULL || !type->isEqualTo("cpu", sizeof("cpu")) || reg == NULL || reg->getLength() < sizeof(uint32_t)) {
		return NULL;
	}
	int logical = ml_get_cpu_number(*(const uint32_t *)reg->getBytesNoCopy());
	if (logical < 0 || (unsigned int)logical >= bankedCount || banked[logical].rd == 0) {
		return NULL;  // a CPU the kernel doesn't run (cpus=, cpumask=)
	}
	*cpu = (unsigned int)logical;
	return &banked[logical].vectors[n];
}

IOReturn
NeoDarwinGICv3::registerInterrupt(IOService *nub, int source, void *target,
    IOInterruptHandler handler, void *refCon)
{
	unsigned int cpu;
	IOInterruptVectorNumber intid;
	IOInterruptVector *vector = bankedVector(nub, source, &cpu, &intid);
	if (vector == NULL) {
		// An SPI's trigger, for initVector, vectorCanBeShared and
		// getVectorType, which see only the INTID.
		bool edge = specifierEdge(nub, source, &intid);
		bool shared = false;
		if (intid >= ND_GIC_FIRST_SPI && intid < ND_GIC_NUM_INTIDS) {
			uint32_t bit = 1u << (intid % 32);
			if (edge) {
				__atomic_or_fetch(&spiEdge[intid / 32], bit, __ATOMIC_RELAXED);
			} else {
				__atomic_and_fetch(&spiEdge[intid / 32], ~bit, __ATOMIC_RELAXED);
			}
			shared = vectors[intid].interruptRegistered != 0;
		}
		IOReturn ret = super::registerInterrupt(nub, source, target, handler, refCon);
		if (shared) {
			IOLog("NeoDarwinGICv3: SPI %d: %s for %s\n", (int)intid,
			    ret == kIOReturnSuccess ? "shared" : "already registered, not shared", nub->getName());
		}
		return ret;
	}
	IOLockLock(bankedLock);
	if (vector->interruptRegistered) {
		IOLockUnlock(bankedLock);
		return kIOReturnNoResources;
	}
	vector->handler = handler;
	vector->nub = nub;
	vector->source = source;
	vector->target = target;
	vector->refCon = refCon;
	vector->interruptDisabledHard = 1;
	vector->interruptDisabledSoft = 1;
	__atomic_store_n(&vector->interruptRegistered, 1, __ATOMIC_RELEASE);
	IOLockUnlock(bankedLock);
	return kIOReturnSuccess;
}

IOReturn
NeoDarwinGICv3::unregisterInterrupt(IOService *nub, int source)
{
	unsigned int cpu;
	IOInterruptVectorNumber intid;
	IOInterruptVector *vector = bankedVector(nub, source, &cpu, &intid);
	if (vector == NULL) {
		return super::unregisterInterrupt(nub, source);
	}
	IOLockLock(bankedLock);
	if (vector->interruptRegistered) {
		vector->interruptDisabledSoft = 1;
		__atomic_and_fetch(&banked[cpu].enabled, ~(1u << intid), __ATOMIC_RELEASE);
		wr32(banked[cpu].rd + ND_GICR_ICENABLER0, 1u << intid);
		while (vector->interruptActive) {
		}
		vector->interruptRegistered = 0;
		vector->interruptDisabledHard = 0;
		vector->interruptDisabledSoft = 0;
		vector->nub = NULL;
		vector->handler = NULL;
		vector->target = NULL;
		vector->refCon = NULL;
	}
	IOLockUnlock(bankedLock);
	return kIOReturnSuccess;
}

// Enabled in the owning CPU's redistributor, which needn't be this CPU's,
// and remembered for initCurrentCPU().
void
NeoDarwinGICv3::enableBanked(unsigned int cpu, IOInterruptVectorNumber intid)
{
	vm_offset_t rd = banked[cpu].rd;
	__atomic_or_fetch(&banked[cpu].enabled, 1u << intid, __ATOMIC_RELEASE);
	wr8(rd + ND_GICR_IPRIORITYR0 + intid, ND_GIC_PRIORITY_DEFAULT);
	__builtin_arm_dsb(ND_DSB_ISHST);
	wr32(rd + GICR_ISENABLER0, 1u << intid);
}

IOReturn
NeoDarwinGICv3::enableInterrupt(IOService *nub, int source)
{
	unsigned int cpu;
	IOInterruptVectorNumber intid;
	IOInterruptVector *vector = bankedVector(nub, source, &cpu, &intid);
	if (vector == NULL) {
		return super::enableInterrupt(nub, source);
	}
	if (vector->interruptDisabledSoft) {
		vector->interruptDisabledSoft = 0;
		OSMemoryBarrier();
		if (vector->interruptDisabledHard) {
			vector->interruptDisabledHard = 0;
			OSSynchronizeIO();
			enableBanked(cpu, intid);
		}
	}
	return kIOReturnSuccess;
}

IOReturn
NeoDarwinGICv3::disableInterrupt(IOService *nub, int source)
{
	unsigned int cpu;
	IOInterruptVectorNumber intid;
	IOInterruptVector *vector = bankedVector(nub, source, &cpu, &intid);
	if (vector == NULL) {
		return super::disableInterrupt(nub, source);
	}
	// As IOInterruptController does: soft only; the IRQ loop disables it in
	// hardware if it fires.
	vector->interruptDisabledSoft = 1;
	OSMemoryBarrier();
	if (!getPlatform()->atInterruptLevel()) {
		while (vector->interruptActive) {
		}
	}
	return kIOReturnSuccess;
}

// Bit 0 of a specifier's second cell (ACPI flags); level without one.
bool
NeoDarwinGICv3::specifierEdge(IOService *nub, int source, IOInterruptVectorNumber *intid)
{
	OSData *vectorData = nub->_interruptSources[source].vectorData;
	const uint32_t *cells = (const uint32_t *)vectorData->getBytesNoCopy();
	*intid = (IOInterruptVectorNumber)cells[0];
	return vectorData->getLength() >= 2 * sizeof(uint32_t) && (cells[1] & ND_SPEC_EDGE) != 0;
}

bool
NeoDarwinGICv3::spiIsEdge(IOInterruptVectorNumber intid)
{
	return intid >= ND_GIC_FIRST_SPI && intid < ND_GIC_NUM_INTIDS &&
	       (__atomic_load_n(&spiEdge[intid / 32], __ATOMIC_RELAXED) & (1u << (intid % 32))) != 0;
}

IOReturn
NeoDarwinGICv3::getInterruptType(IOService *nub, int source, int *interruptType)
{
	if (interruptType == NULL || nub == NULL || nub->_interruptSources == NULL) {
		return kIOReturnBadArgument;
	}
	IOInterruptVectorNumber intid;
	*interruptType = specifierEdge(nub, source, &intid) ? kIOInterruptTypeEdge : kIOInterruptTypeLevel;
	return kIOReturnSuccess;
}

IOInterruptAction
NeoDarwinGICv3::getInterruptHandlerAddress(void)
{
	return OSMemberFunctionCast(IOInterruptAction, this, &NeoDarwinGICv3::handleInterrupt);
}

// Runs a vector's handler, or disables a source its driver has disabled.
// `rd` is this CPU's redistributor, for SGIs and PPIs.
void
NeoDarwinGICv3::dispatch(IOInterruptVector *vector, IOInterruptVectorNumber intid, vm_offset_t rd)
{
	vector->interruptActive = 1;
	if (vector->interruptRegistered) {
		if (vector->interruptDisabledSoft) {
			if (!vector->interruptDisabledHard) {
				vector->interruptDisabledHard = 1;
				if (intid < ND_GIC_NUM_BANKED) {
					if (vector != &vectors[intid]) {
						__atomic_and_fetch(&banked[cpu_number()].enabled, ~(1u << intid), __ATOMIC_RELEASE);
					}
					wr32(rd + ND_GICR_ICENABLER0, 1u << intid);
				} else {
					disableVectorHard(intid, vector);
				}
			}
		} else {
			vector->handler(vector->target, vector->refCon, vector->nub, vector->source);
		}
	}
	vector->interruptActive = 0;
}

IOReturn
NeoDarwinGICv3::handleInterrupt(void *refCon, IOService *nub, int source)
{
	(void)refCon; (void)nub; (void)source;
	unsigned int cpu = (unsigned int)cpu_number();
	BankedCPU *mine = cpu < bankedCount ? &banked[cpu] : NULL;
	for (;;) {
		uint64_t iar = __builtin_arm_rsr64("ICC_IAR1_EL1");
		uint32_t intid = (uint32_t)(iar & 0xffffff);
		if (intid >= ND_GIC_NUM_INTIDS) {
			break;  // 1023: nothing pending
		}
		if (intid == pe_gic_timer_intid && pe_gic_timer_group == 1) {
			sleh_gic_timer_interrupt();
		} else if (intid < ND_GIC_NUM_BANKED && mine != NULL && mine->vectors[intid].interruptRegistered) {
			dispatch(&mine->vectors[intid], (IOInterruptVectorNumber)intid, mine->rd);
		} else {
			dispatch(&vectors[intid], (IOInterruptVectorNumber)intid,
			    intid < ND_GIC_NUM_BANKED ? redistributorForCurrentCPU() : 0);
		}
		__builtin_arm_wsr64("ICC_EOIR1_EL1", iar);
		__builtin_arm_isb(ND_ISB_SY);
	}
	return kIOReturnSuccess;
}

// Level SPIs may be shared (PCI INTx lines are); nothing else.
bool
NeoDarwinGICv3::vectorCanBeShared(IOInterruptVectorNumber vectorNumber, IOInterruptVector *vector)
{
	(void)vector;
	return vectorNumber >= ND_GIC_FIRST_SPI && vectorNumber < ND_GIC_NUM_INTIDS && !spiIsEdge(vectorNumber);
}

// First registration of a vector: SPIs are configured in the distributor.
void
NeoDarwinGICv3::initVector(IOInterruptVectorNumber vectorNumber, IOInterruptVector *vector)
{
	(void)vector;
	if (vectorNumber >= ND_GIC_FIRST_SPI && vectorNumber < ND_GIC_NUM_INTIDS) {
		configureSPI(vectorNumber);
	}
}

int
NeoDarwinGICv3::getVectorType(IOInterruptVectorNumber vectorNumber, IOInterruptVector *vector)
{
	(void)vector;
	return spiIsEdge(vectorNumber) ? kIOInterruptTypeEdge : kIOInterruptTypeLevel;
}

// GICD_CTLR.RWP: the effect of an earlier ICENABLER write (and of CTLR's
// own) is visible once it reads 0 (Arm IHI 0069 12.9.4).
void
NeoDarwinGICv3::waitForRWP(void)
{
	for (int i = 0; i < 1000000 && (rd32(gicd + GICD_CTLR) & ND_GICD_CTLR_RWP); i++) {
	}
}

// Disabled while it is changed (changing ICFGR of an enabled SPI is
// UNPREDICTABLE), then left disabled: enableVector enables it. The values
// read back are logged, so the boot log shows what the distributor holds.
void
NeoDarwinGICv3::configureSPI(IOInterruptVectorNumber intid)
{
	uint32_t word = 4 * ((uint32_t)intid / 32), bit = 1u << (intid % 32);
	vm_offset_t cfg = gicd + ND_GICD_ICFGR + 4 * ((uint32_t)intid / 16);
	uint32_t cfgBit = 2u << (2 * (intid % 16));
	bool edge = spiIsEdge(intid);

	IOLockLock(spiLock);
	wr32(gicd + ND_GICD_ICENABLER + word, bit);
	waitForRWP();
	wr32(gicd + ND_GICD_IGROUPR + word, rd32(gicd + ND_GICD_IGROUPR + word) | bit);
	wr8(gicd + ND_GICD_IPRIORITYR + intid, ND_GIC_PRIORITY_DEFAULT);
	uint32_t c = rd32(cfg);
	wr32(cfg, edge ? (c | cfgBit) : (c & ~cfgBit));
	wr64(gicd + ND_GICD_IROUTER + 8 * (uint32_t)intid, spiRoute);
	__builtin_arm_dsb(ND_DSB_ISHST);

	// With two security states (DS == 0) GICD_IGROUPR is RAZ/WI to
	// Non-secure accesses: the group is the Secure firmware's to set.
	bool ds = (rd32(gicd + GICD_CTLR) & ND_GICD_CTLR_DS) != 0;
	bool group1 = (rd32(gicd + ND_GICD_IGROUPR + word) & bit) != 0;
	uint8_t priority = rd8(gicd + ND_GICD_IPRIORITYR + intid);
	bool edgeNow = (rd32(cfg) & cfgBit) != 0;
	uint64_t route = rd64(gicd + ND_GICD_IROUTER + 8 * (uint32_t)intid);
	IOLockUnlock(spiLock);

	IOLog("NeoDarwinGICv3: SPI %d: %s, priority 0x%02x, %s, routed to 0x%llx\n", (int)intid,
	    !ds ? "Group 1 Non-secure as the Secure firmware set it (DS=0)" :
	    group1 ? "Group 1" : "still Group 0: GICD_IGROUPR did not take the write",
	    priority, edgeNow ? "edge" : "level", route);
}

// For the shared table: an SGI or PPI there is the current CPU's; SPIs are
// global (distributor).
void
NeoDarwinGICv3::disableVectorHard(IOInterruptVectorNumber vectorNumber, IOInterruptVector *vector)
{
	(void)vector;
	uint32_t bit = 1u << (vectorNumber % 32);
	if (vectorNumber < ND_GIC_NUM_BANKED) {
		wr32(redistributorForCurrentCPU() + ND_GICR_ICENABLER0, bit);
	} else {
		wr32(gicd + ND_GICD_ICENABLER + 4 * (vectorNumber / 32), bit);
	}
}

void
NeoDarwinGICv3::enableVector(IOInterruptVectorNumber vectorNumber, IOInterruptVector *vector)
{
	(void)vector;
	uint32_t bit = 1u << (vectorNumber % 32);
	if (vectorNumber < ND_GIC_NUM_BANKED) {
		wr32(redistributorForCurrentCPU() + GICR_ISENABLER0, bit);
	} else {
		wr32(gicd + ND_GICD_ISENABLER + 4 * (vectorNumber / 32), bit);
	}
}

// ICC_SGI1R_EL1 (Arm IHI 0069 12.11.12): Aff3[55:48], RS[47:44],
// Aff2[39:32], INTID[27:24], Aff1[23:16], TargetList[15:0]. TargetList has
// a bit per Aff0 value within the range 16 * RS to 16 * RS + 15. The
// topology's phys_id is the MPIDR's Aff2.Aff1.Aff0 (the cpu node's reg).
// The SGI is Group 1 of the sender's security state, Non-secure here:
// every SGI a target CPU takes is Non-secure Group 1 (pe_fiq.c with DS == 1,
// the Secure firmware with DS == 0).
void
NeoDarwinGICv3::sendIPI(unsigned int cpu_id, bool deferred)
{
	const ml_topology_info_t *topology = ml_get_topology_info();
	if (cpu_id >= topology->num_cpus) {
		return;
	}
	uint64_t mpidr = topology->cpus[cpu_id].phys_id;
	uint64_t aff0 = mpidr & 0xff;
	uint64_t sgi = (uint64_t)(deferred ? ND_SGI_DEFERRED_IPI : ND_SGI_IPI) << 24 |
	    ((mpidr >> 16) & 0xff) << 32 |
	    (aff0 >> 4) << 44 |
	    ((mpidr >> 8) & 0xff) << 16 |
	    (1ull << (aff0 & 0xf));
	__builtin_arm_dsb(ND_DSB_ISHST);
	__builtin_arm_wsr64("ICC_SGI1R_EL1", sgi);
	__builtin_arm_isb(ND_ISB_SY);
}

// An SGI cannot be withdrawn once sent; the handler tolerates a stale one.
void
NeoDarwinGICv3::cancelDeferredIPI(unsigned int cpu_id)
{
	(void)cpu_id;
}
