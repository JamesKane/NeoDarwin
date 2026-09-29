// SPDX-License-Identifier: BSD-2-Clause
// NeoDarwin-Language: expressibility: IOKit drivers are C++ IOService subclasses.
//
// A proof that MSI-X reaches handlers (docs/kernel/gic-its.md, P1-09
// checkpoint 3), on an NVMe controller: a device whose MSI-X vectors a
// driver can make fire one by one without a data path. Before anything reads
// the device's interrupt specifiers it asks IOPCIFamily for two MSI-X
// vectors (IOPCIDevice::configureInterrupts), which become two LPIs, EventIDs
// 0 and 1 of the device's DeviceID. Then it resets the controller, gives it
// an admin queue pair (completions on vector 0), and submits:
//
//  1. Get Features (Number of Queues): its completion raises vector 0;
//  2. Create I/O Completion Queue 1 on vector 1, and Create I/O Submission
//     Queue 1 (vector 0 again);
//  3. Flush on I/O queue 1: its completion raises vector 1.
//
//   MSI-X table entry i (GITS_TRANSLATER, EventID i) -> ITS (DeviceID from
//   the IORT, MAPTI) -> LPI base + i on the boot CPU's redistributor -> IRQ
//   -> IOPCIMessagedInterruptController -> the vector's handler
//
// The handlers consume completions. The driver logs the vectors, LPIs and
// times from each doorbell to its handler, then disables the controller and
// lets go of it. Only a test: P1-10's NVMe driver replaces it, and the
// personality's probe score of 0 lets any real driver win. It drives only
// QEMU's NVMe model (vendor 0x1b36) unless the boot-arg nd_pci_nvme_test=1
// allows any controller, so it never touches a real board's disk by
// default; nd_pci_nvme_test=0 turns it off. Compiled into the kernel with the
// host bridge (patch 0024), matched by class 0x010802.

#include <IOKit/IOService.h>
#include <IOKit/IOLib.h>
#include <IOKit/IOBufferMemoryDescriptor.h>
#include <IOKit/pci/IOPCIDevice.h>
#include <pexpert/pexpert.h>

extern "C" void FlushPoC_DcacheRegion(vm_offset_t va, size_t length);

class NeoDarwinPCINVMeTest : public IOService
{
	OSDeclareDefaultStructors(NeoDarwinPCINVMeTest);

public:
	virtual bool start(IOService *provider) APPLE_KEXT_OVERRIDE;

private:
	// A completion queue and what its vector's handler saw.
	struct Queue {
		volatile uint32_t *entries;     // 16-byte completion entries
		uint32_t head;
		uint32_t phase;
		uint32_t doorbell;              // head doorbell register
		UInt32 count;                   // completions consumed
		uint32_t result, status;        // of the last one
		uint64_t handledAt;             // of the first one since `armed`
		bool armed;
	};

	static void interrupt(OSObject *target, void *refCon, IOService *nub, int source);
	bool waitReady(bool ready, uint32_t timeoutMs);
	// Submits at `sq`'s tail and waits up to a second for `cq` to consume
	// one more completion; the nanoseconds from the doorbell to the
	// handler, or 0.
	uint64_t submit(uint32_t sqIndex, uint32_t *sqe, Queue *cq);
	uint32_t rd32(uint32_t off) { return *(volatile uint32_t *)(regs + off); }
	void wr32(uint32_t off, uint32_t v) { *(volatile uint32_t *)(regs + off) = v; }

	IOPCIDevice *pci;
	IOMemoryMap *map;
	volatile uint8_t *regs;
	uint8_t *mem;                   // ASQ, ACQ, SQ1, CQ1: a 4 KiB page each
	uint64_t memPA;
	bool coherent;
	uint32_t stride;
	uint32_t sqTail[2];
	Queue cqs[2];
};

#define super IOService
OSDefineMetaClassAndStructors(NeoDarwinPCINVMeTest, IOService);

// Controller registers (NVMe 1.4 §3.1).
enum {
	kNVMeCAP  = 0x00,
	kNVMeVS   = 0x08,
	kNVMeCC   = 0x14,
	kNVMeCSTS = 0x1c,
	kNVMeAQA  = 0x24,
	kNVMeASQ  = 0x28,
	kNVMeACQ  = 0x30,
	kNVMeDoorbells = 0x1000,
};
enum {
	kCCEnable = 0x1,
	kCCIOSQES = 6u << 16,           // 64-byte submission entries
	kCCIOCQES = 4u << 20,           // 16-byte completion entries
	kCSTSReady = 0x1,
	kCSTSFatal = 0x2,
};
enum {
	kOpFlush = 0x00,                // NVM command set
	kOpCreateIOSQ = 0x01,
	kOpCreateIOCQ = 0x05,
	kOpGetFeatures = 0x0a,
};
static const uint32_t kQueueEntries = 4;
static const uint32_t kFeatureNumberOfQueues = 0x07;
static const uint32_t kPage = 4096;

// Primary interrupt context: consume every completion whose phase bit says
// the controller wrote it, then ring the head doorbell.
void
NeoDarwinPCINVMeTest::interrupt(OSObject *target, void *refCon, IOService *nub, int source)
{
	(void)nub; (void)source;
	NeoDarwinPCINVMeTest *self = static_cast<NeoDarwinPCINVMeTest *>(target);
	Queue *q = &self->cqs[(uintptr_t)refCon];
	uint64_t now = mach_absolute_time();
	bool any = false;
	for (;;) {
		volatile uint32_t *e = q->entries + 4 * q->head;
		uint32_t dw3 = e[3];
		if (((dw3 >> 16) & 1) != q->phase) {
			break;
		}
		q->result = e[0];
		q->status = dw3 >> 17;
		if (++q->head == kQueueEntries) {
			q->head = 0;
			q->phase ^= 1;
		}
		any = true;
		__atomic_add_fetch(&q->count, 1, __ATOMIC_RELEASE);
	}
	if (any) {
		if (q->armed) {
			q->handledAt = now;
			q->armed = false;
		}
		self->wr32(q->doorbell, q->head);
	}
}

bool
NeoDarwinPCINVMeTest::waitReady(bool ready, uint32_t timeoutMs)
{
	for (uint32_t i = 0; i <= timeoutMs; i++) {
		uint32_t csts = rd32(kNVMeCSTS);
		if (csts & kCSTSFatal) {
			return false;
		}
		if (((csts & kCSTSReady) != 0) == ready) {
			return true;
		}
		IOSleep(1);
	}
	return false;
}

uint64_t
NeoDarwinPCINVMeTest::submit(uint32_t sqIndex, uint32_t *sqe, Queue *cq)
{
	uint8_t *sq = mem + (sqIndex == 0 ? 0 : 2 * kPage);
	uint32_t slot = sqTail[sqIndex];
	memcpy(sq + 64 * slot, sqe, 64);
	sqTail[sqIndex] = (slot + 1) % kQueueEntries;
	if (coherent) {
		__builtin_arm_dsb(0xb);        // DSB ISH: the entry before the doorbell
	} else {
		FlushPoC_DcacheRegion((vm_offset_t)(sq + 64 * slot), 64);
	}
	UInt32 before = __atomic_load_n(&cq->count, __ATOMIC_ACQUIRE);
	cq->armed = true;
	uint64_t rang = mach_absolute_time();
	wr32(kNVMeDoorbells + 2 * sqIndex * stride, sqTail[sqIndex]);
	for (int i = 0; i < 1000 && __atomic_load_n(&cq->count, __ATOMIC_ACQUIRE) == before; i++) {
		IOSleep(1);
	}
	if (__atomic_load_n(&cq->count, __ATOMIC_ACQUIRE) == before) {
		return 0;
	}
	uint64_t ns;
	absolutetime_to_nanoseconds(cq->handledAt - rang, &ns);
	return ns != 0 ? ns : 1;
}

bool
NeoDarwinPCINVMeTest::start(IOService *provider)
{
	pci = OSDynamicCast(IOPCIDevice, provider);
	uint32_t enabled = 2;   // 2 (default): QEMU's NVMe only; 1: any controller; 0: off
	PE_parse_boot_argn("nd_pci_nvme_test", &enabled, sizeof(enabled));
	if (enabled == 0 || pci == NULL) {
		return false;
	}
	if (enabled != 1 && pci->configRead16(kIOPCIConfigVendorID) != 0x1b36) {
		return false;
	}
	if (!super::start(provider)) {
		return false;
	}
	char where[16];
	snprintf(where, sizeof(where), "%02x:%02x.%x", pci->getBusNumber(), pci->getDeviceNumber(), pci->getFunctionNumber());

	// Two MSI-X vectors, before anything resolves the device's interrupts
	// (IOPCIFamily would give an MSI-X device one shared vector by
	// default). Their specifiers are then sources 0 and 1; no INTx.
	IOReturn ret = pci->configureInterrupts(kIOInterruptTypePCIMessagedX, 2, 2, 0);
	int type = 0;
	if (ret != kIOReturnSuccess || pci->getInterruptType(1, &type) != kIOReturnSuccess || (type & kIOInterruptTypePCIMessagedX) == 0) {
		IOLog("NeoDarwinPCINVMeTest: %s: no two MSI-X vectors (0x%x); not tested\n", where, ret);
		return true;
	}

	pci->setMemoryEnable(true);
	pci->setBusLeadEnable(true);
	map = pci->mapDeviceMemoryWithRegister(kIOPCIConfigBaseAddress0);
	if (map == NULL) {
		IOLog("NeoDarwinPCINVMeTest: %s: BAR0 not mapped\n", where);
		return true;
	}
	regs = (volatile uint8_t *)map->getVirtualAddress();
	uint64_t cap = (uint64_t)rd32(kNVMeCAP) | (uint64_t)rd32(kNVMeCAP + 4) << 32;
	uint32_t vs = rd32(kNVMeVS);
	uint32_t timeoutMs = (uint32_t)((cap >> 24) & 0xff) * 500 + 500;
	stride = 4u << ((cap >> 32) & 0xf);
	if (((cap >> 48) & 0xf) != 0) {
		IOLog("NeoDarwinPCINVMeTest: %s: the controller's smallest page is above 4 KiB; not tested\n", where);
		return true;
	}

	// Four 4 KiB queues, physically contiguous, within the root complex's
	// DMA address bits (IORT).
	OSNumber *bits = OSDynamicCast(OSNumber, pci->getProperty("dma-address-bits"));
	uint32_t dmaBits = bits != NULL && bits->unsigned32BitValue() < 64 ? bits->unsigned32BitValue() : 48;
	mach_vm_address_t mask = ((1ULL << dmaBits) - 1) & ~(mach_vm_address_t)(kPage - 1);
	IOBufferMemoryDescriptor *queues = IOBufferMemoryDescriptor::inTaskWithPhysicalMask(kernel_task,
	    kIODirectionInOut | kIOMemoryHostPhysicallyContiguous | kIOMemoryMapperNone, 4 * kPage, mask);
	if (queues == NULL || queues->prepare() != kIOReturnSuccess) {
		OSSafeReleaseNULL(queues);
		IOLog("NeoDarwinPCINVMeTest: %s: no memory for the queues\n", where);
		return true;
	}
	mem = (uint8_t *)queues->getBytesNoCopy();
	memPA = queues->getPhysicalSegment(0, NULL, kIOMemoryMapperNone);
	bzero(mem, 4 * kPage);
	coherent = pci->getProperty("dma-coherent") == kOSBooleanTrue;
	if (!coherent) {
		FlushPoC_DcacheRegion((vm_offset_t)mem, 4 * kPage);
	}
	for (int i = 0; i < 2; i++) {
		cqs[i].entries = (volatile uint32_t *)(mem + (2 * i + 1) * kPage);
		cqs[i].phase = 1;
		cqs[i].doorbell = kNVMeDoorbells + (2 * i + 1) * stride;
	}

	const char *failure = NULL;
	uint64_t ns0 = 0, ns1 = 0;
	uint32_t queuesResult = 0;
	bool registered[2] = {};
	wr32(kNVMeCC, rd32(kNVMeCC) & ~kCCEnable);
	if (!waitReady(false, timeoutMs)) {
		failure = "does not reset";
	}
	if (failure == NULL) {
		wr32(kNVMeAQA, (kQueueEntries - 1) << 16 | (kQueueEntries - 1));
		wr32(kNVMeASQ, (uint32_t)memPA);
		wr32(kNVMeASQ + 4, (uint32_t)(memPA >> 32));
		wr32(kNVMeACQ, (uint32_t)(memPA + kPage));
		wr32(kNVMeACQ + 4, (uint32_t)((memPA + kPage) >> 32));
		for (int v = 0; v < 2 && failure == NULL; v++) {
			if (pci->registerInterrupt(v, this, &NeoDarwinPCINVMeTest::interrupt, (void *)(uintptr_t)v) != kIOReturnSuccess) {
				failure = "an MSI-X vector did not register";
			} else {
				registered[v] = true;
				pci->enableInterrupt(v);
			}
		}
	}
	if (failure == NULL) {
		wr32(kNVMeCC, kCCIOSQES | kCCIOCQES | kCCEnable);
		if (!waitReady(true, timeoutMs)) {
			failure = "does not become ready";
		}
	}
	uint32_t sqe[16];
	if (failure == NULL) {
		bzero(sqe, sizeof(sqe));
		sqe[0] = kOpGetFeatures | 1u << 16;
		sqe[10] = kFeatureNumberOfQueues;
		ns0 = submit(0, sqe, &cqs[0]);
		queuesResult = cqs[0].result;
		if (ns0 == 0 || cqs[0].status != 0) {
			failure = "Get Features: no completion on vector 0";
		}
	}
	if (failure == NULL) {
		// I/O completion queue 1: physically contiguous, interrupts on,
		// vector 1. Then submission queue 1 feeding it.
		bzero(sqe, sizeof(sqe));
		sqe[0] = kOpCreateIOCQ | 2u << 16;
		sqe[6] = (uint32_t)(memPA + 3 * kPage);
		sqe[7] = (uint32_t)((memPA + 3 * kPage) >> 32);
		sqe[10] = (kQueueEntries - 1) << 16 | 1;
		sqe[11] = 1u << 16 | 0x2 | 0x1;
		if (submit(0, sqe, &cqs[0]) == 0 || cqs[0].status != 0) {
			failure = "Create I/O Completion Queue failed";
		}
	}
	if (failure == NULL) {
		bzero(sqe, sizeof(sqe));
		sqe[0] = kOpCreateIOSQ | 3u << 16;
		sqe[6] = (uint32_t)(memPA + 2 * kPage);
		sqe[7] = (uint32_t)((memPA + 2 * kPage) >> 32);
		sqe[10] = (kQueueEntries - 1) << 16 | 1;
		sqe[11] = 1u << 16 | 0x1;
		if (submit(0, sqe, &cqs[0]) == 0 || cqs[0].status != 0) {
			failure = "Create I/O Submission Queue failed";
		}
	}
	if (failure == NULL) {
		bzero(sqe, sizeof(sqe));
		sqe[0] = kOpFlush | 4u << 16;
		sqe[1] = 1;                     // namespace 1
		ns1 = submit(1, sqe, &cqs[1]);
		if (ns1 == 0) {
			failure = "Flush: no completion on vector 1";
		}
	}
	IOByteCount msix = 0;
	UInt16 msixControl = pci->extendedFindPCICapability(kIOPCIMSIXCapability, &msix) ? pci->configRead16(msix + 2) : 0;

	// Let go: controller disabled, vectors unregistered (MSI-X off again).
	wr32(kNVMeCC, rd32(kNVMeCC) & ~kCCEnable);
	waitReady(false, timeoutMs);
	for (int v = 0; v < 2; v++) {
		if (registered[v]) {
			pci->disableInterrupt(v);
			pci->unregisterInterrupt(v);
		}
	}
	queues->complete();
	queues->release();

	OSNumber *lpi = OSDynamicCast(OSNumber, pci->getProperty("msi-lpi-base"));
	OSNumber *dev = OSDynamicCast(OSNumber, pci->getProperty("msi-device-id"));
	uint32_t base = lpi ? lpi->unsigned32BitValue() : 0;
	if (failure != NULL) {
		IOLog("NeoDarwinPCINVMeTest: %s: NVMe %u.%u: MSI-X on LPIs %u-%u: %s (CSTS 0x%x)\n", where, vs >> 16,
		    (vs >> 8) & 0xff, base, base + 1, failure, rd32(kNVMeCSTS));
	} else {
		IOLog("NeoDarwinPCINVMeTest: %s: NVMe %u.%u: MSI-X 2 of %u vectors on LPIs %u-%u (DeviceID 0x%x): "
		    "vector 0 (admin) in %llu us, vector 1 (I/O queue 1) in %llu us; queues 0x%08x\n", where, vs >> 16,
		    (vs >> 8) & 0xff, (msixControl & 0x7ff) + 1, base, base + 1, dev ? dev->unsigned32BitValue() : 0,
		    ns0 / 1000, ns1 / 1000, queuesResult);
	}
	return true;
}
