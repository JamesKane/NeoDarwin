// SPDX-License-Identifier: BSD-2-Clause
// NeoDarwin-Language: expressibility: IOKit classes are C++; this subclasses XNU's IODTPlatformExpert.
//
// Matched by the built-in personality patch 0009 adds to
// gIOKernelConfigTables (IONameMatch "NeoDarwin,sbsa").

#include "NeoDarwinPlatform.h"
#include <IOKit/IODeviceTreeSupport.h>
#include <IOKit/IOLib.h>
#include <kern/clock.h>
#include <kern/cpu_data.h>
#include <kern/cpu_number.h>
#include <kern/processor.h>
#include <kern/thread.h>
#include <mach/clock_types.h>
#include <machine/machine_routines.h>
#include <pexpert/arm64/board_config.h>
#include <pexpert/pexpert.h>

// osfmk: cross-calls (osfmk/arm/cpu_common.c) and the CPUs the scheduler
// runs (hw.activecpu).
extern "C" kern_return_t cpu_xcall(int cpu, void (*func)(void *), void *param);
extern "C" uint32_t processor_avail_count;
// osfmk/kern/sched_prim.h, visible only to MACH_KERNEL_PRIVATE.
extern "C" processor_t thread_bind(processor_t processor);

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

static void
ipi_measure_thread(void *param, wait_result_t)
{
	static_cast<NeoDarwinPlatformExpert *>(param)->measureIPIs();
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

	// P1-06's exit: every CPU up, and the IPIs between them timed.
	if (kernel_thread_start(ipi_measure_thread, this, &thread) != KERN_SUCCESS) {
		panic("NeoDarwinPlatformExpert: cannot start the IPI measurement thread");
	}
	thread_deallocate(thread);

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

// The cross-call's work on the target CPU: note when it ran.
static void
ipi_ack(void *param)
{
	__atomic_store_n((uint64_t *)param, mach_absolute_time() | 1, __ATOMIC_RELEASE);
}

#define ND_IPI_SAMPLES 128

// Once AppleARMSMP has started every CPU (IOAllCPUInitialized), time an IPI
// round trip from this CPU to each of the others: a cross-call (SIGPxcall
// through PE_cpu_signal, i.e. an SGI from NeoDarwinGICv3::sendIPI) whose
// handler on the target sets a flag this CPU spins on. The target may be
// idle in WFI, so a sample includes its wake-up.
void
NeoDarwinPlatformExpert::measureIPIs(void)
{
	OSDictionary *matching = IOService::resourceMatching(gIOAllCPUInitializedKey);
	IOService *resource = IOService::waitForMatchingService(matching, UINT64_MAX);
	matching->release();
	OSSafeReleaseNULL(resource);

	const ml_topology_info_t *topology = ml_get_topology_info();
	IOLog("NeoDarwinPlatformExpert: %u of %u CPUs online\n", processor_avail_count, topology->num_cpus);
	if (topology->num_cpus < 2) {
		return;
	}

	// From the boot CPU, with preemption disabled, so that every sample
	// leaves from it; reported afterwards, since IOLog may block.
	processor_t previous = thread_bind(cpu_to_processor(topology->boot_cpu->cpu_id));
	thread_block(THREAD_CONTINUE_NULL);
	uint64_t samples[ND_IPI_SAMPLES];
	uint64_t median[MAX_CPUS] = {}, least[MAX_CPUS] = {};
	unsigned int taken[MAX_CPUS] = {};
	disable_preemption();
	int self = cpu_number();
	for (unsigned int i = 0; i < topology->num_cpus && i < MAX_CPUS; i++) {
		int target = (int)topology->cpus[i].cpu_id;
		if (target == self) {
			continue;
		}
		unsigned int n = 0;
		for (; n < ND_IPI_SAMPLES; n++) {
			uint64_t done = 0;
			uint64_t start = mach_absolute_time();
			if (cpu_xcall(target, ipi_ack, &done) != KERN_SUCCESS) {
				break;
			}
			while (__atomic_load_n(&done, __ATOMIC_ACQUIRE) == 0) {
			}
			samples[n] = mach_absolute_time() - start;
		}
		for (unsigned int a = 1; a < n; a++) {  // insertion sort: n is small
			uint64_t v = samples[a];
			unsigned int b = a;
			for (; b > 0 && samples[b - 1] > v; b--) {
				samples[b] = samples[b - 1];
			}
			samples[b] = v;
		}
		taken[i] = n;
		if (n != 0) {
			median[i] = samples[n / 2];
			least[i] = samples[0];
		}
	}
	enable_preemption();
	thread_bind(previous);

	for (unsigned int i = 0; i < topology->num_cpus && i < MAX_CPUS; i++) {
		int target = (int)topology->cpus[i].cpu_id;
		if (target == self) {
			continue;
		}
		if (taken[i] == 0) {
			IOLog("NeoDarwinPlatformExpert: cpu %d does not take cross-calls\n", target);
			continue;
		}
		uint64_t median_ns, min_ns;
		absolutetime_to_nanoseconds(median[i], &median_ns);
		absolutetime_to_nanoseconds(least[i], &min_ns);
		IOLog("NeoDarwinPlatformExpert: IPI round trip from cpu %d to cpu %d: median %llu ns, min %llu ns (%u samples)\n",
		    self, target, median_ns, min_ns, taken[i]);
	}
}
