// SPDX-License-Identifier: BSD-2-Clause
// NeoDarwin-Language: expressibility: IOKit classes are C++; this subclasses XNU's IODTPlatformExpert.
//
// Matched by the built-in personality patch 0009 adds to
// gIOKernelConfigTables (IONameMatch "NeoDarwin,sbsa").

#include "NeoDarwinPlatform.h"
#include <IOKit/IODeviceTreeSupport.h>
#include <kern/thread.h>
#include <mach/clock_types.h>
#include <pexpert/pexpert.h>

#define ND_ISB_SY 0xf

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

	startClock();

	registerService();
	return true;
}

// The counter XNU times with (virtual timer, CNTVCT_EL0).
static uint64_t
counter_now(void)
{
	__builtin_arm_isb(ND_ISB_SY);
	return __builtin_arm_rsr64("CNTVCT_EL0");
}

static uint64_t
chosen_u64(IORegistryEntry *chosen, const char *name)
{
	OSData *data = OSDynamicCast(OSData, chosen->getProperty(name));
	uint64_t value = 0;
	if (data != NULL && data->getLength() == sizeof(value)) {
		memcpy(&value, data->getBytesNoCopy(), sizeof(value));
	}
	return value;
}

// There is no RTC driver: SBBR firmware keeps the clock behind UEFI's
// GetTime(), and the kernel has no runtime services. neoboot reads it and
// passes the time and the counter value it was read at in /chosen
// (neodarwin,utc-seconds and neodarwin,utc-counter). Publishing IORTC lets
// IOKitInitializeTime() go on at once instead of timing out after 30 s.
void
NeoDarwinPlatformExpert::startClock(void)
{
	clockLock = IOSimpleLockAlloc();
	if (clockLock == NULL) {
		panic("NeoDarwinPlatformExpert: cannot allocate the clock lock");
	}
	IORegistryEntry *chosen = IORegistryEntry::fromPath("/chosen", gIODTPlane);
	if (chosen != NULL) {
		clockSecs = (clock_sec_t)chosen_u64(chosen, "neodarwin,utc-seconds");
		clockCounter = chosen_u64(chosen, "neodarwin,utc-counter");
		chosen->release();
	}
	if (clockSecs == 0) {
		IOLog("NeoDarwinPlatformExpert: the loader passed no time of day; the clock starts at 1970\n");
		clockCounter = counter_now();
	}
	publishResource("IORTC", this);
}

void
NeoDarwinPlatformExpert::getUTCTimeOfDay(clock_sec_t *secs, clock_nsec_t *nsecs)
{
	uint64_t hz = gPEClockFrequencyInfo.timebase_frequency_hz;
	IOSimpleLockLock(clockLock);
	uint64_t ticks = counter_now() - clockCounter;
	clock_sec_t s = clockSecs;
	uint64_t ns = clockNsecs;
	IOSimpleLockUnlock(clockLock);
	if (hz != 0) {
		s += (clock_sec_t)(ticks / hz);
		ns += (ticks % hz) * NSEC_PER_SEC / hz;  // < hz * 1e9, well inside 64 bits
	}
	*secs = s + (clock_sec_t)(ns / NSEC_PER_SEC);
	*nsecs = (clock_nsec_t)(ns % NSEC_PER_SEC);
}

// Kept until reboot; nothing writes it back to the firmware yet.
void
NeoDarwinPlatformExpert::setUTCTimeOfDay(clock_sec_t secs, clock_nsec_t nsecs)
{
	IOSimpleLockLock(clockLock);
	clockSecs = secs;
	clockNsecs = nsecs;
	clockCounter = counter_now();
	IOSimpleLockUnlock(clockLock);
}
