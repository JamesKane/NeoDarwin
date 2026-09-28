// SPDX-License-Identifier: BSD-2-Clause
// NeoDarwin-Language: expressibility: IOKit classes are C++, and PSCI is reached with the SMC instruction.
//
// IOPMGR over PSCI (Arm DEN 0022): CPU_ON starts a core at the kernel's
// reset vector, CPU_OFF stops the calling one. Idle is left to XNU's WFI.
// The conduit is SMC (SBSA machines with EL3 firmware). Machines whose PSCI
// is HVC (QEMU without virtualization, some hypervisors) need that choice
// read from ACPI's FADT, which comes with P1-04 and SMP (P1-06).

#include "NeoDarwinPlatform.h"
#include <kern/cpu_number.h>
#include <machine/machine_routines.h>

#define super IOPMGR
OSDefineMetaClassAndStructors(NeoDarwinPSCI, IOPMGR);

#define PSCI_CPU_OFF    0x84000002u
#define PSCI_CPU_ON_64  0xc4000003u

static int64_t
psci_smc(uint64_t function, uint64_t a1, uint64_t a2, uint64_t a3)
{
	register uint64_t x0 __asm__("x0") = function;
	register uint64_t x1 __asm__("x1") = a1;
	register uint64_t x2 __asm__("x2") = a2;
	register uint64_t x3 __asm__("x3") = a3;
	__asm__ volatile ("smc #0" : "+r"(x0), "+r"(x1), "+r"(x2), "+r"(x3) : : "memory");
	return (int64_t)x0;
}

void
NeoDarwinPSCI::enableCPUCore(unsigned int cpu_id, uint64_t entry_pa)
{
	if ((int)cpu_id == cpu_number()) {
		return;  // already running: the boot CPU
	}
	const ml_topology_info_t *topology = ml_get_topology_info();
	int64_t rc = psci_smc(PSCI_CPU_ON_64, topology->cpus[cpu_id].phys_id, entry_pa, 0);
	if (rc != 0) {
		panic("NeoDarwinPSCI: CPU_ON for cpu %u failed: %lld", cpu_id, rc);
	}
}

void
NeoDarwinPSCI::disableCPUCore(unsigned int cpu_id)
{
	if ((int)cpu_id == cpu_number()) {
		(void)psci_smc(PSCI_CPU_OFF, 0, 0, 0);
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
