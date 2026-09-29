// SPDX-License-Identifier: BSD-2-Clause
// NeoDarwin-Language: expressibility: IOKit classes are C++, and the GIC's CPU interface is reached through system registers.
//
// GICv3 IRQ controller (Arm IHI 0069). Vector numbers are GIC INTIDs: the
// device tree gives every interrupt specifier as one cell holding the INTID.
// SGI 0 carries IPIs and SGI 1 deferred IPIs (the CPU node's three-entry
// "interrupts": IPI, PMI, deferred IPI; AppleARMSMP.cpp). FreeBSD's
// sys/arm64/arm64/gic_v3.c was the reference for the register sequences.

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

// Barrier domains (Arm ARM C6.2): ISB SY and DSB ISHST.
#define ND_ISB_SY    0xf
#define ND_DSB_ISHST 0xa

#define super IOInterruptController
OSDefineMetaClassAndStructors(NeoDarwinGICv3, IOInterruptController);

// Distributor and redistributor registers beyond those SBSA.h defines.
#define ND_GICD_CTLR_ENABLEGRP1   0x2
#define ND_GICD_CTLR_ARE          0x10
#define ND_GICD_ISENABLER         0x100
#define ND_GICD_ICENABLER         0x180
#define ND_GICR_ICENABLER0        0x10180
#define ND_GIC_NUM_INTIDS         1020    // SGIs, PPIs and SPIs; 1020-1023 are special
#define ND_SGI_IPI                0
#define ND_SGI_DEFERRED_IPI       1

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

	// Affinity routing and Group 1, alongside the Group 0 pe_fiq.c enabled.
	wr32(gicd + GICD_CTLR, rd32(gicd + GICD_CTLR) | ND_GICD_CTLR_ARE | ND_GICD_CTLR_ENABLEGRP1);
	initCPUInterface();

	// Publish under the name device-tree interrupt specifiers resolve to.
	const OSSymbol *name = IODTInterruptControllerName(gicNode);
	if (name == NULL) {
		return false;
	}
	IOReturn ret = getPlatform()->registerInterruptController((OSSymbol *)name, this);
	name->release();
	return ret == kIOReturnSuccess;
}

// The redistributor whose GICR_TYPER affinity matches this CPU's MPIDR.
vm_offset_t
NeoDarwinGICv3::redistributorForCurrentCPU(void)
{
	uint64_t mpidr = __builtin_arm_rsr64("MPIDR_EL1");
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

// Group 1 on this CPU: pe_fiq.c has already woken the redistributor, put the
// timer on the group the device tree names (Group 0 by default), SGIs and
// other PPIs on Group 1, and set PMR.
void
NeoDarwinGICv3::initCPUInterface(void)
{
	__builtin_arm_wsr64("ICC_BPR1_EL1", 0);
	__builtin_arm_wsr64("ICC_IGRPEN1_EL1", 1);
	__builtin_arm_isb(ND_ISB_SY);
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

IOReturn
NeoDarwinGICv3::getInterruptType(IOService *nub, int source, int *interruptType)
{
	(void)nub; (void)source;
	if (interruptType == NULL) {
		return kIOReturnBadArgument;
	}
	*interruptType = kIOInterruptTypeLevel;
	return kIOReturnSuccess;
}

IOInterruptAction
NeoDarwinGICv3::getInterruptHandlerAddress(void)
{
	return OSMemberFunctionCast(IOInterruptAction, this, &NeoDarwinGICv3::handleInterrupt);
}

IOReturn
NeoDarwinGICv3::handleInterrupt(void *refCon, IOService *nub, int source)
{
	(void)refCon; (void)nub; (void)source;
	for (;;) {
		uint64_t iar = __builtin_arm_rsr64("ICC_IAR1_EL1");
		uint32_t intid = (uint32_t)(iar & 0xffffff);
		if (intid >= ND_GIC_NUM_INTIDS) {
			break;  // 1023: nothing pending
		}
		if (intid == pe_gic_timer_intid && pe_gic_timer_group == 1) {
			sleh_gic_timer_interrupt();
		} else {
			IOInterruptVector *vector = &vectors[intid];
			vector->interruptActive = 1;
			if (vector->interruptRegistered && !vector->interruptDisabledHard) {
				vector->handler(vector->target, vector->refCon, vector->nub, vector->source);
			}
			vector->interruptActive = 0;
		}
		__builtin_arm_wsr64("ICC_EOIR1_EL1", iar);
		__builtin_arm_isb(ND_ISB_SY);
	}
	return kIOReturnSuccess;
}

bool
NeoDarwinGICv3::vectorCanBeShared(IOInterruptVectorNumber vectorNumber, IOInterruptVector *vector)
{
	(void)vectorNumber; (void)vector;
	return false;
}

void
NeoDarwinGICv3::initVector(IOInterruptVectorNumber vectorNumber, IOInterruptVector *vector)
{
	(void)vectorNumber; (void)vector;
}

int
NeoDarwinGICv3::getVectorType(IOInterruptVectorNumber vectorNumber, IOInterruptVector *vector)
{
	(void)vectorNumber; (void)vector;
	return kIOInterruptTypeLevel;
}

// SGIs and PPIs are per CPU (redistributor); SPIs are global (distributor).
void
NeoDarwinGICv3::disableVectorHard(IOInterruptVectorNumber vectorNumber, IOInterruptVector *vector)
{
	(void)vector;
	uint32_t bit = 1u << (vectorNumber % 32);
	if (vectorNumber < 32) {
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
	if (vectorNumber < 32) {
		wr32(redistributorForCurrentCPU() + GICR_ISENABLER0, bit);
	} else {
		wr32(gicd + ND_GICD_ISENABLER + 4 * (vectorNumber / 32), bit);
	}
}

// ICC_SGI1R_EL1: Aff3[55:48], INTID[27:24], Aff2[39:32], Aff1[23:16],
// TargetList[15:0] (a bit per Aff0 value).
void
NeoDarwinGICv3::sendIPI(unsigned int cpu_id, bool deferred)
{
	const ml_topology_info_t *topology = ml_get_topology_info();
	if (cpu_id >= topology->num_cpus) {
		return;
	}
	uint64_t mpidr = topology->cpus[cpu_id].phys_id;
	uint64_t sgi = (uint64_t)(deferred ? ND_SGI_DEFERRED_IPI : ND_SGI_IPI) << 24 |
	    ((mpidr >> 16) & 0xff) << 32 |
	    ((mpidr >> 8) & 0xff) << 16 |
	    (1ull << (mpidr & 0xf));
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
