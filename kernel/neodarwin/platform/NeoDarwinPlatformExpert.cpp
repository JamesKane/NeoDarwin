// SPDX-License-Identifier: BSD-2-Clause
// NeoDarwin-Language: expressibility: IOKit classes are C++; this subclasses XNU's IODTPlatformExpert.
//
// Matched by the built-in personality patch 0009 adds to
// gIOKernelConfigTables (IONameMatch "NeoDarwin,sbsa").

#include "NeoDarwinPlatform.h"
#include <IOKit/IODeviceTreeSupport.h>
#include <kern/thread.h>

#define super IODTPlatformExpert
OSDefineMetaClassAndStructors(NeoDarwinPlatformExpert, IODTPlatformExpert);

const char *
NeoDarwinPlatformExpert::deleteList(void)
{
	return "('packages')";
}

// Nodes that are data, not devices.
const char *
NeoDarwinPlatformExpert::excludeList(void)
{
	return "('chosen', 'memory', 'options', 'aliases', 'defaults', 'cpus')";
}

static void
gic_attach_thread(void *param, wait_result_t)
{
	static_cast<NeoDarwinGICv3 *>(param)->attachToCPUController();
}

bool
NeoDarwinPlatformExpert::start(IOService *provider)
{
	if (!super::start(provider)) {
		return false;
	}

	IORegistryEntry *gicNode = IORegistryEntry::fromPath("/arm-io/gic", gIODTPlane);
	if (gicNode == NULL) {
		panic("NeoDarwinPlatformExpert: no /arm-io/gic in the device tree");
	}
	gic = OSTypeAlloc(NeoDarwinGICv3);
	if (gic == NULL || !gic->initWithNode(gicNode)) {
		panic("NeoDarwinPlatformExpert: GICv3 initialisation failed");
	}
	gicNode->release();
	gic->attach(this);
	gic->registerService();

	// XNU's cpu_boot_thread creates the PassthruInterruptController once
	// this platform expert is registered; the GIC must then register as its
	// child. That wait blocks, so it runs on its own thread.
	thread_t thread;
	if (kernel_thread_start(gic_attach_thread, gic, &thread) != KERN_SUCCESS) {
		panic("NeoDarwinPlatformExpert: cannot start the GIC attach thread");
	}
	thread_deallocate(thread);

	psci = OSTypeAlloc(NeoDarwinPSCI);
	if (psci == NULL || !psci->init()) {
		panic("NeoDarwinPlatformExpert: PSCI power manager initialisation failed");
	}
	psci->attach(this);
	psci->registerService();

	registerService();
	return true;
}
