// SPDX-License-Identifier: BSD-2-Clause
// NeoDarwin-Language: expressibility: IOKit classes are C++, and PSCI is reached with the SMC or HVC instruction.
//
// IOPMGR over PSCI (Arm DEN 0022): CPU_ON starts a core at the kernel's
// reset vector, CPU_OFF stops the calling one. Idle is left to XNU's WFI.
//
// The conduit is the firmware's, chosen at boot as Linux does
// (drivers/firmware/psci): neoboot reads the FADT's ARM_BOOT_ARCH flags and
// writes /chosen psci-conduit, "smc" or "hvc" (DT-ABI v1, dt-abi.md). SMC
// reaches EL3 firmware such as TF-A, on SBSA boards; HVC reaches a
// hypervisor's PSCI, as on QEMU virt without EL3. Without the property
// there is no PSCI, and neoboot boots one CPU (cpus=1).
//
// The kernel's ISA audit (//kernel:sbsa_isa_audit) forbids HVC everywhere
// else: an HVC at EL1 with no EL2 is UNDEFINED. nd_psci_hvc is its only
// site, and runs only when the firmware says PSCI is behind a hypervisor.

#include "NeoDarwinPlatform.h"
#include <IOKit/IODeviceTreeSupport.h>
#include <IOKit/IOLib.h>
#include <kern/cpu_number.h>
#include <machine/machine_routines.h>

#define super IOPMGR
OSDefineMetaClassAndStructors(NeoDarwinPSCI, IOPMGR);

#define PSCI_VERSION    0x84000000u
#define PSCI_CPU_OFF    0x84000002u
#define PSCI_CPU_ON_64  0xc4000003u

// Return codes (DEN 0022 5.2.2).
#define PSCI_SUCCESS             0
#define PSCI_NOT_SUPPORTED       -1
#define PSCI_INVALID_PARAMETERS  -2
#define PSCI_DENIED              -3
#define PSCI_ALREADY_ON          -4
#define PSCI_ON_PENDING          -5
#define PSCI_INTERNAL_FAILURE    -6
#define PSCI_NOT_PRESENT         -7
#define PSCI_DISABLED            -8
#define PSCI_INVALID_ADDRESS     -9

// SMC Calling Convention: function ID in x0, arguments in x1-x3, result in
// x0; x0-x17 may be clobbered by SMCCC v1.0 firmware.
#define ND_PSCI_CALL(insn)                                                              \
	register uint64_t x0 __asm__("x0") = function;                                  \
	register uint64_t x1 __asm__("x1") = a1;                                        \
	register uint64_t x2 __asm__("x2") = a2;                                        \
	register uint64_t x3 __asm__("x3") = a3;                                        \
	__asm__ volatile (insn : "+r"(x0), "+r"(x1), "+r"(x2), "+r"(x3) : :             \
	    "x4", "x5", "x6", "x7", "x8", "x9", "x10", "x11", "x12", "x13", "x14",      \
	    "x15", "x16", "x17", "memory");                                             \
	return (int64_t)x0

extern "C" __attribute__((noinline)) int64_t
nd_psci_smc(uint64_t function, uint64_t a1, uint64_t a2, uint64_t a3)
{
	ND_PSCI_CALL("smc #0");
}

// The kernel's only HVC (isa_audit/sbsa_release.txt).
extern "C" __attribute__((noinline)) int64_t
nd_psci_hvc(uint64_t function, uint64_t a1, uint64_t a2, uint64_t a3)
{
	ND_PSCI_CALL("hvc #0");
}

static const char *
psci_error(int64_t rc)
{
	switch (rc) {
	case PSCI_NOT_SUPPORTED: return "NOT_SUPPORTED";
	case PSCI_INVALID_PARAMETERS: return "INVALID_PARAMETERS";
	case PSCI_DENIED: return "DENIED";
	case PSCI_ALREADY_ON: return "ALREADY_ON";
	case PSCI_ON_PENDING: return "ON_PENDING";
	case PSCI_INTERNAL_FAILURE: return "INTERNAL_FAILURE";
	case PSCI_NOT_PRESENT: return "NOT_PRESENT";
	case PSCI_DISABLED: return "DISABLED";
	case PSCI_INVALID_ADDRESS: return "INVALID_ADDRESS";
	default: return "unknown error";
	}
}

bool
NeoDarwinPSCI::init(OSDictionary *dictionary)
{
	if (!super::init(dictionary)) {
		return false;
	}
	conduit = kConduitNone;
	IORegistryEntry *chosen = IORegistryEntry::fromPath("/chosen", gIODTPlane);
	if (chosen != NULL) {
		OSData *method = OSDynamicCast(OSData, chosen->getProperty("psci-conduit"));
		if (method != NULL && method->isEqualTo("smc", sizeof("smc"))) {
			conduit = kConduitSMC;
		} else if (method != NULL && method->isEqualTo("hvc", sizeof("hvc"))) {
			conduit = kConduitHVC;
		}
		chosen->release();
	}
	if (conduit == kConduitNone) {
		IOLog("NeoDarwinPSCI: no PSCI conduit in /chosen; secondary CPUs cannot be started\n");
		return true;
	}
	int64_t version = call(PSCI_VERSION, 0, 0, 0);
	IOLog("NeoDarwinPSCI: PSCI %lld.%lld over %s\n", (version >> 16) & 0x7fff, version & 0xffff,
	    conduit == kConduitSMC ? "SMC" : "HVC");
	return true;
}

int64_t
NeoDarwinPSCI::call(uint64_t function, uint64_t a1, uint64_t a2, uint64_t a3)
{
	switch (conduit) {
	case kConduitSMC:
		return nd_psci_smc(function, a1, a2, a3);
	case kConduitHVC:
		return nd_psci_hvc(function, a1, a2, a3);
	default:
		return PSCI_NOT_SUPPORTED;
	}
}

// From AppleARMSMP's PE_cpu_start_internal() (patch 0017): entry_pa is the
// physical address of LowResetVectorBase, where the CPU finds its cpu_data
// by MPIDR. The context argument (x0) is unused: start.s ignores it.
void
NeoDarwinPSCI::enableCPUCore(unsigned int cpu_id, uint64_t entry_pa)
{
	if ((int)cpu_id == cpu_number()) {
		return;  // already running: the boot CPU
	}
	const ml_topology_info_t *topology = ml_get_topology_info();
	uint64_t mpidr = topology->cpus[cpu_id].phys_id;
	if (conduit == kConduitNone) {
		panic("NeoDarwinPSCI: cannot start cpu %u (MPIDR 0x%llx): the firmware reports no PSCI; boot with cpus=1",
		    cpu_id, mpidr);
	}
	if (entry_pa == 0) {
		panic("NeoDarwinPSCI: cannot start cpu %u: no reset vector address", cpu_id);
	}
	int64_t rc = call(PSCI_CPU_ON_64, mpidr, entry_pa, 0);
	switch (rc) {
	case PSCI_SUCCESS:
		return;
	case PSCI_ON_PENDING:
		// An earlier CPU_ON for this CPU is still completing: it will
		// arrive at the same entry point.
		IOLog("NeoDarwinPSCI: CPU_ON for cpu %u: already pending\n", cpu_id);
		return;
	default:
		// ALREADY_ON: the firmware left the CPU running something that
		// isn't this kernel. Anything else is a bad target or entry
		// point. The kernel would panic anyway once the CPU missed its
		// boot deadline (processor_wait_for_start); say why now.
		panic("NeoDarwinPSCI: CPU_ON(MPIDR 0x%llx, entry 0x%llx) for cpu %u failed: %s (%lld)",
		    mpidr, entry_pa, cpu_id, psci_error(rc), rc);
	}
}

void
NeoDarwinPSCI::disableCPUCore(unsigned int cpu_id)
{
	if ((int)cpu_id == cpu_number()) {
		(void)call(PSCI_CPU_OFF, 0, 0, 0);
	}
}

void
NeoDarwinPSCI::enableCPUCluster(unsigned int cluster_id)
{
	(void)cluster_id;
}

void
NeoDarwinPSCI::disableCPUCluster(unsigned int cluster_id)
{
	(void)cluster_id;
}

void
NeoDarwinPSCI::initCPUIdle(ml_processor_info_t *info)
{
	(void)info;
}

void
NeoDarwinPSCI::enterCPUIdle(UInt64 *newIdleTimeoutTicks)
{
	(void)newIdleTimeoutTicks;
}

void
NeoDarwinPSCI::exitCPUIdle(UInt64 *newIdleTimeoutTicks)
{
	(void)newIdleTimeoutTicks;
}

void
NeoDarwinPSCI::updateCPUIdle(UInt64 *newIdleTimeoutTicks)
{
	(void)newIdleTimeoutTicks;
}
