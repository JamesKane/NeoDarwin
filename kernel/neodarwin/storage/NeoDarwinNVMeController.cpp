// SPDX-License-Identifier: BSD-2-Clause
// NeoDarwin-Language: expressibility: IOKit drivers are C++ IOService subclasses.
//
// NeoDarwin's NVMe controller driver, written from the NVM Express Base
// Specification 1.4c (docs/kernel/storage.md, "NVMe"). It matches any
// IOPCIDevice of class 010802 (patch 0029) and publishes one
// NeoDarwinNVMeNamespace, an IOBlockStorageDevice, per active namespace.
//
// Bring-up (§7.6.1): MSI-X vectors asked for before anything resolves the
// device's interrupts (one for the admin queue, one per I/O queue pair);
// BAR0 mapped; CAP read (MQES, DSTRD, TO, CSS, MPSMIN); the controller
// disabled (CC.EN = 0, CSTS.RDY = 0 within CAP.TO); the admin queue
// (AQA/ASQ/ACQ) and CC (NVM command set, 4 KiB pages, 64/16-byte entries)
// written and the controller enabled. Then admin commands, each waited for
// on vector 0: Identify Controller, Get Features (Volatile Write Cache),
// Set Features (Number of Queues), Create I/O Completion Queue and Create
// I/O Submission Queue per pair, Identify (active namespace list) and
// Identify Namespace.
//
// I/O: one queue pair per CPU up to the MSI-X vectors granted less the
// admin one (at most 8), else one pair; each has 32 command slots with an
// IODMACommand and a preallocated PRP list, and a request picks the pair of
// the CPU it is submitted on. Transfers are limited by MDTS and 512 KiB.
// Every request runs on the driver's work loop (a command gate, the
// interrupt event sources, a timer).
//
// Interrupts: MSI-X, the admin queue on vector 0 and I/O queue n on vector
// n (all on vector 0 with one vector); without MSIs (nd_pci_msi=0, no ITS)
// INTx, level and shared, through a filter that looks for a completion
// queue entry whose phase tag has flipped and masks the controller's pin
// (INTMS) until the work loop has reaped (INTMC).
//
// Failures: a timer checks CSTS and the commands in flight every second. A
// command older than 30 s, or CSTS.CFS, resets the controller (CC.EN = 0
// stops its DMA), fails the commands that timed out, puts the others back
// in the queue, and brings the queues up again; three resets without a
// completion in between, or a controller that reads all ones, fail
// everything.
//
// Safety (the Radxa Dragon Q8B's only NVMe disk holds other systems): only
// QEMU's controller (vendor 0x1b36) is writable by default. Any other is
// attached read-only: its namespaces are write-protected, the driver sends
// it Identify, Get Features, Read and the queue set-up and nothing else
// (no Write, Flush, Format, DSM or state-changing Set Features), and
// refuses any other opcode in issue(). The boot-arg nd_nvme_rw=1 makes
// every controller writable, nd_nvme_rw=0 makes every one read-only (QEMU's
// too, for the tests), nd_nvme=0 keeps the driver off.

#include <IOKit/IOKitKeys.h>
#include <IOKit/IOLib.h>
#include <IOKit/storage/IOStorage.h>
#include <kern/cpu_number.h>
#include <pexpert/pexpert.h>

#include "NeoDarwinNVMe.h"

extern "C" unsigned int ml_get_cpu_count(void);
extern "C" void dcache_incoherent_io_flush64(addr64_t pa, unsigned int count, unsigned int remaining, unsigned int *res);

#define super IOService
OSDefineMetaClassAndStructors(NeoDarwinNVMeController, IOService);

static inline uint32_t
round4k(uint64_t bytes)
{
	return (uint32_t)((bytes + kNDNVMePage - 1) & ~(uint64_t)(kNDNVMePage - 1));
}

// The ASCII fields of Identify Controller, without their padding.
static void
copyField(char *out, const uint8_t *field, size_t length)
{
	size_t n = length;
	while (n > 0 && (field[n - 1] == ' ' || field[n - 1] == 0)) {
		n--;
	}
	for (size_t i = 0; i < n; i++) {
		out[i] = (field[i] >= 0x20 && field[i] < 0x7f) ? (char)field[i] : '?';
	}
	out[n] = 0;
}

static IOReturn
statusResult(uint16_t status)
{
	uint32_t sct = ND_NVME_STATUS_SCT(status), sc = ND_NVME_STATUS_SC(status);
	if (sct == kNDNVMeSCTGeneric) {
		switch (sc) {
		case kNDNVMeSCSuccess: return kIOReturnSuccess;
		case kNDNVMeSCInvalidOpcode: return kIOReturnUnsupported;
		case kNDNVMeSCInvalidNamespace: return kIOReturnNoDevice;
		case kNDNVMeSCNamespaceWriteProtected: return kIOReturnNotWritable;
		case kNDNVMeSCLBAOutOfRange:
		case kNDNVMeSCCapacityExceeded: return kIOReturnBadArgument;
		case kNDNVMeSCNamespaceNotReady: return kIOReturnNotReady;
		case kNDNVMeSCAbortRequested:
		case kNDNVMeSCAbortedSQDeletion: return kIOReturnAborted;
		default: break;
		}
	}
	return kIOReturnIOError;
}

static const char *
opcodeName(uint8_t opcode)
{
	return opcode == kNDNVMeCmdRead ? "read" : (opcode == kNDNVMeCmdWrite ? "write" : "flush");
}

// MARK: - Policy and set-up

void
NeoDarwinNVMeController::policy()
{
	uint32_t rw = 2;                // 2 (default): QEMU's controller only
	PE_parse_boot_argn("nd_nvme_rw", &rw, sizeof(rw));
	if (rw == 1) {
		writeAllowed = true;
		policyReason = "nd_nvme_rw=1";
	} else if (rw == 0) {
		writeAllowed = false;
		policyReason = "nd_nvme_rw=0";
	} else if (isQEMU) {
		writeAllowed = true;
		policyReason = "QEMU's controller";
	} else {
		writeAllowed = false;
		policyReason = "not QEMU's controller; nd_nvme_rw=1 allows writes";
	}
}

// MSI-X first, before anything resolves the device's interrupts: vector 0
// for the admin queue and one per wanted I/O queue pair, or as many as
// IOPCIFamily grants (at least one). Without them, INTx on source 0.
bool
NeoDarwinNVMeController::setUpInterrupts(uint32_t wanted)
{
	IOReturn ret = pci->configureInterrupts(kIOInterruptTypePCIMessagedX, 1, 1 + wanted, 0);
	int type = 0;
	if (ret == kIOReturnSuccess) {
		while (vectors < 1 + wanted && pci->getInterruptType((int)vectors, &type) == kIOReturnSuccess &&
		    (type & kIOInterruptTypePCIMessagedX) != 0) {
			vectors++;
		}
	}
	if (vectors != 0) {
		for (uint32_t v = 0; v < vectors; v++) {
			sources[v] = IOInterruptEventSource::interruptEventSource(this, &NeoDarwinNVMeController::interrupt, pci, (int)v);
			if (sources[v] == NULL || workLoop->addEventSource(sources[v]) != kIOReturnSuccess) {
				IOLog("NeoDarwinNVMeController: %s: cannot register MSI-X vector %u\n", where, v);
				return false;
			}
		}
	} else {
		// INTx: level and possibly shared. The filter runs in primary
		// interrupt context and masks the pin (INTMS) when a completion
		// queue has a new entry.
		sources[0] = IOFilterInterruptEventSource::filterInterruptEventSource(this,
		    &NeoDarwinNVMeController::interrupt, &NeoDarwinNVMeController::filter, pci, 0);
		if (sources[0] == NULL || workLoop->addEventSource(sources[0]) != kIOReturnSuccess) {
			IOLog("NeoDarwinNVMeController: %s: cannot register INTx\n", where);
			return false;
		}
	}
	return true;
}

// A queue pair's memory, physically contiguous below dma-address-bits: the
// submission queue, the completion queue, and for an I/O queue each slot's
// PRP list, all on 4 KiB boundaries.
bool
NeoDarwinNVMeController::allocateQueue(Queue *q, uint16_t id, uint32_t entries, bool io)
{
	q->id = id;
	q->entries = entries;
	q->vector = (vectors >= 2) ? id : 0;
	q->slotCount = io ? (entries - 1 < kMaxSlots ? entries - 1 : kMaxSlots) : 0;
	uint32_t sqBytes = round4k(entries * kNDNVMeSQESize);
	uint32_t cqBytes = round4k(entries * kNDNVMeCQESize);
	uint32_t prpBytes = round4k((uint64_t)q->slotCount * prpListBytes);
	q->memory = dmaPolicy.allocate(sqBytes + cqBytes + prpBytes);
	if (q->memory == NULL) {
		return false;
	}
	uint8_t *base = (uint8_t *)q->memory->getBytesNoCopy();
	uint64_t pa = NDStorageDMA::physical(q->memory);
	q->sq = base;
	q->sqPA = pa;
	q->cq = (volatile struct nd_nvme_cqe *)(base + sqBytes);
	q->cqPA = pa + sqBytes;
	q->sqDoorbell = kNDNVMeRegDoorbells + (2 * id) * stride;
	q->cqDoorbell = kNDNVMeRegDoorbells + (2 * id + 1) * stride;
	if ((uint64_t)q->cqDoorbell + 4 > bar->getLength()) {
		IOLog("NeoDarwinNVMeController: %s: queue %u's doorbells are outside BAR0\n", where, id);
		return false;
	}
	for (uint32_t s = 0; s < q->slotCount; s++) {
		Slot *slot = &q->slots[s];
		slot->prpList = (uint64_t *)(base + sqBytes + cqBytes + s * prpListBytes);
		slot->prpListPA = pa + sqBytes + cqBytes + s * prpListBytes;
		slot->dma = dmaPolicy.newPRPCommand(maxTransfer);
		if (slot->dma == NULL) {
			return false;
		}
	}
	resetQueue(q);
	return true;
}

// Empty rings, phase 1, as after a controller reset.
void
NeoDarwinNVMeController::resetQueue(Queue *q)
{
	uint32_t sqBytes = round4k(q->entries * kNDNVMeSQESize);
	uint32_t cqBytes = round4k(q->entries * kNDNVMeCQESize);
	bzero(q->sq, sqBytes);
	bzero((void *)q->cq, cqBytes);
	if (!dmaPolicy.coherent) {
		FlushPoC_DcacheRegion((vm_offset_t)q->sq, sqBytes + cqBytes);
	}
	q->sqTail = 0;
	q->cqHead = 0;
	q->phase = 1;
}

// CSTS.RDY becoming `ready`, within CAP.TO. Waiting for ready fails at once
// on CSTS.CFS; a controller that reads all ones is gone.
bool
NeoDarwinNVMeController::waitReady(bool ready, uint32_t timeoutMs)
{
	for (uint32_t ms = 0; ms <= timeoutMs; ms++) {
		uint32_t csts = rd32(kNDNVMeRegCSTS);
		if (csts == 0xffffffff) {
			return false;
		}
		if (ready && (csts & kNDNVMeCSTSFatal)) {
			return false;
		}
		if (((csts & kNDNVMeCSTSReady) != 0) == ready) {
			return true;
		}
		IOSleep(1);
	}
	return false;
}

// CC.EN = 0 (§7.6.1; the firmware usually left it enabled). A controller
// that is still becoming ready is let finish first, as the specification
// asks.
bool
NeoDarwinNVMeController::disableController()
{
	uint32_t cc = rd32(kNDNVMeRegCC);
	if (cc & kNDNVMeCCEnable) {
		uint32_t csts = rd32(kNDNVMeRegCSTS);
		if (!(csts & kNDNVMeCSTSReady) && !(csts & kNDNVMeCSTSFatal)) {
			waitReady(true, readyTimeoutMs);
		}
		wr32(kNDNVMeRegCC, cc & ~(uint32_t)kNDNVMeCCEnable);
	}
	return waitReady(false, readyTimeoutMs);
}

bool
NeoDarwinNVMeController::enableController()
{
	Queue *a = queues[0];
	resetQueue(a);
	wr32(kNDNVMeRegAQA, (a->entries - 1) << 16 | (a->entries - 1));
	wr64(kNDNVMeRegASQ, a->sqPA);
	wr64(kNDNVMeRegACQ, a->cqPA);
	wr32(kNDNVMeRegCC, kNDNVMeCCIOCQES | kNDNVMeCCIOSQES | kNDNVMeCCAMSRoundRobin | kNDNVMeCCMPS4K |
	    kNDNVMeCCCSSNVM | kNDNVMeCCEnable);
	return waitReady(true, readyTimeoutMs);
}

bool
NeoDarwinNVMeController::createIOQueue(Queue *q, bool poll)
{
	struct nd_nvme_sqe sqe = {};
	uint32_t dw0 = 0;
	sqe.cdw0 = kNDNVMeAdminCreateIOCQ;
	sqe.prp1 = q->cqPA;
	sqe.cdw10 = (q->entries - 1) << 16 | q->id;
	sqe.cdw11 = (uint32_t)q->vector << 16 | kNDNVMeCQInterruptsEnabled | kNDNVMeQueuePhysicallyContiguous;
	if ((poll ? admin(&sqe, &dw0, true) : adminFromClient(&sqe, &dw0)) != kIOReturnSuccess) {
		IOLog("NeoDarwinNVMeController: %s: Create I/O Completion Queue %u failed (status 0x%x)\n", where, q->id, adminStatus >> 1);
		return false;
	}
	sqe = {};
	sqe.cdw0 = kNDNVMeAdminCreateIOSQ;
	sqe.prp1 = q->sqPA;
	sqe.cdw10 = (q->entries - 1) << 16 | q->id;
	sqe.cdw11 = (uint32_t)q->id << 16 | kNDNVMeQueuePhysicallyContiguous;
	if ((poll ? admin(&sqe, &dw0, true) : adminFromClient(&sqe, &dw0)) != kIOReturnSuccess) {
		IOLog("NeoDarwinNVMeController: %s: Create I/O Submission Queue %u failed (status 0x%x)\n", where, q->id, adminStatus >> 1);
		return false;
	}
	return true;
}

// After a reset (on the work loop, so admin commands are polled): the
// admin queue, the same number of I/O queues, the same queue memory.
bool
NeoDarwinNVMeController::bringUp(bool poll, uint32_t *allocated)
{
	if (!disableController() || !enableController()) {
		IOLog("NeoDarwinNVMeController: %s: the controller does not become ready (CSTS 0x%x)\n", where, rd32(kNDNVMeRegCSTS));
		return false;
	}
	struct nd_nvme_sqe sqe = {};
	uint32_t dw0 = 0;
	sqe.cdw0 = kNDNVMeAdminSetFeatures;
	sqe.cdw10 = kNDNVMeFeatureNumberOfQueues;
	sqe.cdw11 = (ioQueues - 1) << 16 | (ioQueues - 1);
	if ((poll ? admin(&sqe, &dw0, true) : adminFromClient(&sqe, &dw0)) != kIOReturnSuccess) {
		IOLog("NeoDarwinNVMeController: %s: Set Features (Number of Queues) failed (status 0x%x)\n", where, adminStatus >> 1);
		return false;
	}
	uint32_t sq = (dw0 & 0xffff) + 1, cq = (dw0 >> 16) + 1;
	*allocated = sq < cq ? sq : cq;
	return true;
}

// MARK: - Identify

bool
NeoDarwinNVMeController::identifyController()
{
	struct nd_nvme_sqe sqe = {};
	uint32_t dw0 = 0;
	sqe.cdw0 = kNDNVMeAdminIdentify;
	sqe.prp1 = identifyPA;
	sqe.cdw10 = kNDNVMeIdentifyController;
	if (adminFromClient(&sqe, &dw0) != kIOReturnSuccess) {
		IOLog("NeoDarwinNVMeController: %s: Identify Controller failed (status 0x%x)\n", where, adminStatus >> 1);
		return false;
	}
	dmaPolicy.fromDevice(identify, kNDNVMePage);
	copyField(modelString, identify + kNDNVMeIdCtrlMN, 40);
	copyField(serialString, identify + kNDNVMeIdCtrlSN, 20);
	copyField(firmwareString, identify + kNDNVMeIdCtrlFR, 8);
	uint8_t mdts = identify[kNDNVMeIdCtrlMDTS];
	mdtsBytes = (mdts != 0 && mdts < 20) ? (uint32_t)kNDNVMePage << mdts : 0;
	uint32_t ver = *(uint32_t *)(identify + kNDNVMeIdCtrlVER);
	if (ver != 0) {
		version = ver;
	}
	namespaceCount = *(uint32_t *)(identify + kNDNVMeIdCtrlNN);
	vwc = (identify[kNDNVMeIdCtrlVWC] & 1) != 0;
	uint8_t sqes = identify[kNDNVMeIdCtrlSQES], cqes = identify[kNDNVMeIdCtrlCQES];
	if ((sqes & 0xf) > 6 || (sqes >> 4) < 6 || (cqes & 0xf) > 4 || (cqes >> 4) < 4) {
		IOLog("NeoDarwinNVMeController: %s: entry sizes SQES 0x%x CQES 0x%x exclude 64 and 16 bytes\n", where, sqes, cqes);
		return false;
	}
	return true;
}

// The active namespace IDs: Identify's list (NVMe 1.1 and later), else
// 1..NN.
uint32_t
NeoDarwinNVMeController::findNamespaces(uint32_t *nsids, uint32_t max)
{
	uint32_t n = 0;
	if (version >= 0x10100) {
		struct nd_nvme_sqe sqe = {};
		uint32_t dw0 = 0;
		sqe.cdw0 = kNDNVMeAdminIdentify;
		sqe.prp1 = identifyPA;
		sqe.cdw10 = kNDNVMeIdentifyActiveNamespaces;
		if (adminFromClient(&sqe, &dw0) == kIOReturnSuccess) {
			dmaPolicy.fromDevice(identify, kNDNVMePage);
			const uint32_t *list = (const uint32_t *)identify;
			for (uint32_t i = 0; i < kNDNVMePage / 4 && list[i] != 0 && n < max; i++) {
				nsids[n++] = list[i];
			}
			return n;
		}
	}
	for (uint32_t id = 1; id <= namespaceCount && n < max; id++) {
		nsids[n++] = id;
	}
	return n;
}

void
NeoDarwinNVMeController::publishNamespace(uint32_t nsid)
{
	struct nd_nvme_sqe sqe = {};
	uint32_t dw0 = 0;
	sqe.cdw0 = kNDNVMeAdminIdentify;
	sqe.nsid = nsid;
	sqe.prp1 = identifyPA;
	sqe.cdw10 = kNDNVMeIdentifyNamespace;
	if (adminFromClient(&sqe, &dw0) != kIOReturnSuccess) {
		IOLog("NeoDarwinNVMeController: %s: Identify Namespace %u failed (status 0x%x)\n", where, nsid, adminStatus >> 1);
		return;
	}
	dmaPolicy.fromDevice(identify, kNDNVMePage);
	uint64_t nsze = *(uint64_t *)(identify + kNDNVMeIdNsNSZE);
	if (nsze == 0) {
		return;                         // allocated but not attached
	}
	uint8_t flbas = identify[kNDNVMeIdNsFLBAS];
	uint8_t format = flbas & 0xf;
	uint32_t lbaf = *(uint32_t *)(identify + kNDNVMeIdNsLBAF + 4 * format);
	uint32_t ms = lbaf & 0xffff, lbads = (lbaf >> 16) & 0xff;
	if (format > identify[kNDNVMeIdNsNLBAF] || lbads < 9 || lbads > 16) {
		IOLog("NeoDarwinNVMeController: %s: namespace %u: LBA format %u (LBADS %u) not supported\n", where, nsid, format, lbads);
		return;
	}
	if (ms != 0) {
		IOLog("NeoDarwinNVMeController: %s: namespace %u: %u bytes of metadata per block (%s) not supported\n", where, nsid, ms,
		    (flbas & 0x10) ? "extended LBA" : "separate buffer");
		return;
	}
	bool nsWriteProtected = (identify[kNDNVMeIdNsNSATTR] & 1) != 0;
	NeoDarwinNVMeNamespace *ns = OSTypeAlloc(NeoDarwinNVMeNamespace);
	if (ns == NULL || !ns->initWith(this, nsid, lbads, nsze, !writeAllowed || nsWriteProtected)) {
		OSSafeReleaseNULL(ns);
		return;
	}
	uint64_t bytes = nsze << lbads;
	IOLog("NeoDarwinNVMeNamespace: %s: namespace %u: %llu %u-byte blocks (%llu MiB); %s%s\n", where, nsid, nsze, 1u << lbads,
	    bytes >> 20, (!writeAllowed || nsWriteProtected) ? "read-only" : "read-write",
	    nsWriteProtected ? " (write protected by the controller)" : "");
	if (ns->attach(this)) {
		if (ns->start(this)) {
			if (published < kMaxNamespaces) {
				namespaces[published++] = ns;
			}
		} else {
			ns->detach(this);
		}
	}
	ns->release();
}

// MARK: - Start and stop

bool
NeoDarwinNVMeController::start(IOService *provider)
{
	pci = OSDynamicCast(IOPCIDevice, provider);
	uint32_t enabled = 1;
	PE_parse_boot_argn("nd_nvme", &enabled, sizeof(enabled));
	if (pci == NULL) {
		return false;
	}
	snprintf(where, sizeof(where), "%02x:%02x.%x", pci->getBusNumber(), pci->getDeviceNumber(), pci->getFunctionNumber());
	if (enabled == 0) {
		IOLog("NeoDarwinNVMeController: %s: off (nd_nvme=0)\n", where);
		return false;
	}
	if (!super::start(provider)) {
		return false;
	}
	if (!startController()) {
		stop(provider);                 // the interrupt sources, the controller disabled
		return false;
	}
	return true;
}

bool
NeoDarwinNVMeController::startController()
{
	vendorID = pci->configRead16(kIOPCIConfigVendorID);
	deviceID = pci->configRead16(kIOPCIConfigDeviceID);
	isQEMU = vendorID == kNDNVMeQEMUVendor;
	policy();
	queue_init(&pending);
	dmaPolicy = NDStorageDMA::forDevice(pci);
	nanoseconds_to_absolutetime((uint64_t)kIOTimeoutMs * NSEC_PER_MSEC, &timeoutAbs);

	workLoop = IOWorkLoop::workLoop();
	gate = workLoop != NULL ? IOCommandGate::commandGate(this) : NULL;
	timer = IOTimerEventSource::timerEventSource(this, &NeoDarwinNVMeController::timerFired);
	if (gate == NULL || timer == NULL || workLoop->addEventSource(gate) != kIOReturnSuccess ||
	    workLoop->addEventSource(timer) != kIOReturnSuccess) {
		IOLog("NeoDarwinNVMeController: %s: no work loop\n", where);
		return false;
	}

	uint32_t cpus = ml_get_cpu_count();
	uint32_t wanted = cpus == 0 ? 1 : (cpus < kMaxIOQueues ? cpus : kMaxIOQueues);
	if (!setUpInterrupts(wanted)) {
		return false;
	}
	pci->setMemoryEnable(true);
	pci->setBusLeadEnable(true);
	bar = pci->mapDeviceMemoryWithRegister(kIOPCIConfigBaseAddress0);
	if (bar == NULL || bar->getLength() < 0x1008) {
		IOLog("NeoDarwinNVMeController: %s: BAR0 not mapped\n", where);
		return false;
	}
	regs = (volatile uint8_t *)bar->getVirtualAddress();
	cap = rd64(kNDNVMeRegCAP);
	version = rd32(kNDNVMeRegVS);
	if (cap == ~0ULL) {
		IOLog("NeoDarwinNVMeController: %s: the registers read all ones\n", where);
		return false;
	}
	if ((ND_NVME_CAP_CSS(cap) & kNDNVMeCSSNVM) == 0) {
		IOLog("NeoDarwinNVMeController: %s: no NVM command set (CAP.CSS 0x%x)\n", where, ND_NVME_CAP_CSS(cap));
		return false;
	}
	if (ND_NVME_CAP_MPSMIN(cap) != 0) {
		IOLog("NeoDarwinNVMeController: %s: the smallest memory page is %u KiB; only 4 KiB is supported\n", where,
		    4u << ND_NVME_CAP_MPSMIN(cap));
		return false;
	}
	stride = 4u << ND_NVME_CAP_DSTRD(cap);
	readyTimeoutMs = ND_NVME_CAP_TO(cap) != 0 ? ND_NVME_CAP_TO(cap) * 500 : 500;
	uint32_t mqes = ND_NVME_CAP_MQES(cap) + 1;      // 2..65536

	// The admin queue and the Identify buffer; the controller is reset and
	// enabled with them.
	queues[0] = (Queue *)IOMallocZero(sizeof(Queue));
	identifyBuffer = dmaPolicy.allocate(kNDNVMePage);
	if (queues[0] == NULL || identifyBuffer == NULL ||
	    !allocateQueue(queues[0], 0, mqes < kAdminEntries ? mqes : kAdminEntries, false)) {
		IOLog("NeoDarwinNVMeController: %s: no memory for the admin queue\n", where);
		return false;
	}
	identify = (uint8_t *)identifyBuffer->getBytesNoCopy();
	identifyPA = NDStorageDMA::physical(identifyBuffer);
	for (uint32_t v = 0; v < (vectors != 0 ? vectors : 1); v++) {
		sources[v]->enable();
	}
	if (!disableController()) {
		IOLog("NeoDarwinNVMeController: %s: the controller does not reset (CSTS 0x%x)\n", where, rd32(kNDNVMeRegCSTS));
		return false;
	}
	if (!enableController()) {
		IOLog("NeoDarwinNVMeController: %s: the controller does not become ready (CSTS 0x%x)\n", where, rd32(kNDNVMeRegCSTS));
		return false;
	}
	if (!identifyController()) {
		disableController();
		return false;
	}
	maxTransfer = kMaxTransferCap;
	if (mdtsBytes != 0 && mdtsBytes < maxTransfer) {
		maxTransfer = mdtsBytes;
	}
	// The list holds every entry after the first: up to maxTransfer / 4 KiB
	// of them for a buffer that doesn't start on a page. A power of two, so
	// no list crosses a page.
	prpListBytes = 64;
	while (prpListBytes < (maxTransfer / kNDNVMePage + 1) * 8 && prpListBytes < kNDNVMePage) {
		prpListBytes <<= 1;
	}
	if (vwc) {
		struct nd_nvme_sqe sqe = {};
		uint32_t dw0 = 0;
		sqe.cdw0 = kNDNVMeAdminGetFeatures;
		sqe.cdw10 = kNDNVMeFeatureVolatileWriteCache;
		wce = adminFromClient(&sqe, &dw0) == kIOReturnSuccess ? (dw0 & 1) != 0 : true;
	}

	// I/O queue pairs: as many as the vectors allow, then as many as the
	// controller allocates.
	ioQueues = vectors >= 2 ? vectors - 1 : 1;
	if (ioQueues > wanted) {
		ioQueues = wanted;
	}
	struct nd_nvme_sqe sqe = {};
	uint32_t dw0 = 0;
	sqe.cdw0 = kNDNVMeAdminSetFeatures;
	sqe.cdw10 = kNDNVMeFeatureNumberOfQueues;
	sqe.cdw11 = (ioQueues - 1) << 16 | (ioQueues - 1);
	if (adminFromClient(&sqe, &dw0) != kIOReturnSuccess) {
		IOLog("NeoDarwinNVMeController: %s: Set Features (Number of Queues) failed (status 0x%x)\n", where, adminStatus >> 1);
		disableController();
		return false;
	}
	uint32_t sqa = (dw0 & 0xffff) + 1, cqa = (dw0 >> 16) + 1;
	if (sqa < ioQueues) {
		ioQueues = sqa;
	}
	if (cqa < ioQueues) {
		ioQueues = cqa;
	}
	uint32_t ioEntries = mqes < kIOEntries ? mqes : kIOEntries;
	for (uint32_t i = 1; i <= ioQueues; i++) {
		queues[i] = (Queue *)IOMallocZero(sizeof(Queue));
		if (queues[i] == NULL || !allocateQueue(queues[i], (uint16_t)i, ioEntries, true)) {
			IOLog("NeoDarwinNVMeController: %s: no memory for I/O queue %u\n", where, i);
			disableController();
			return false;
		}
		if (!createIOQueue(queues[i], false)) {
			disableController();
			return false;
		}
	}
	isRunning = true;

	char irq[96];
	if (vectors != 0) {
		OSNumber *lpi = OSDynamicCast(OSNumber, pci->getProperty("msi-lpi-base"));
		uint32_t base = lpi != NULL ? lpi->unsigned32BitValue() : 0;
		snprintf(irq, sizeof(irq), "MSI-X %u vector%s (LPIs %u-%u), admin queue on vector 0 (%u completions by interrupt)",
		    vectors, vectors == 1 ? "" : "s", base, base + vectors - 1, adminByInterrupt);
	} else {
		snprintf(irq, sizeof(irq), "INTx (%u admin completions by interrupt)", adminByInterrupt);
	}
	IOLog("NeoDarwinNVMeController: %s: NVMe %u.%u, %04x:%04x, model \"%s\", serial \"%s\", firmware \"%s\"; NN %u; "
	    "MQES %u, DSTRD %u, TO %u ms, MDTS %u KiB\n", where, version >> 16, (version >> 8) & 0xff, vendorID, deviceID,
	    modelString, serialString, firmwareString, namespaceCount, mqes - 1,
	    ND_NVME_CAP_DSTRD(cap), readyTimeoutMs, mdtsBytes / 1024);
	IOLog("NeoDarwinNVMeController: %s: %u I/O queue pair%s of %u entries, %u commands each, transfers up to %llu KiB; %s\n",
	    where, ioQueues, ioQueues == 1 ? "" : "s", ioEntries, queues[1]->slotCount, maxTransfer / 1024, irq);
	IOLog("NeoDarwinNVMeController: %s: write cache %s; DMA %s, %u address bits; %s: %s\n", where,
	    vwc ? (wce ? "on" : "off") : "none", dmaPolicy.coherent ? "coherent" : "not coherent", dmaPolicy.addressBits,
	    writeAllowed ? "read-write" : "read-only", policyReason);

	OSString *s;
	if ((s = OSString::withCString(modelString)) != NULL) {
		setProperty("Model Number", s);
		s->release();
	}
	if ((s = OSString::withCString(serialString)) != NULL) {
		setProperty("Serial Number", s);
		s->release();
	}
	if ((s = OSString::withCString(firmwareString)) != NULL) {
		setProperty("Firmware Revision", s);
		s->release();
	}
	setProperty("NVMe Version", version, 32);
	setProperty("NVMe I/O Queues", ioQueues, 32);
	setProperty("NVMe Writable", writeAllowed);

	uint32_t nsids[kMaxNamespaces];
	uint32_t count = findNamespaces(nsids, kMaxNamespaces);
	for (uint32_t i = 0; i < count; i++) {
		publishNamespace(nsids[i]);
	}
	if (published == 0) {
		IOLog("NeoDarwinNVMeController: %s: no usable namespace\n", where);
	}
	registerService();
	return true;
}

void
NeoDarwinNVMeController::stop(IOService *provider)
{
	if (gate != NULL && isRunning) {
		isRunning = false;
		gate->runAction(&NeoDarwinNVMeController::failAllAction);
	}
	if (regs != NULL) {
		disableController();
	}
	if (timer != NULL) {
		timer->cancelTimeout();
	}
	for (uint32_t v = 0; v <= kMaxIOQueues; v++) {
		if (sources[v] != NULL) {
			sources[v]->disable();
			if (workLoop != NULL) {
				workLoop->removeEventSource(sources[v]);
			}
			OSSafeReleaseNULL(sources[v]);
		}
	}
	super::stop(provider);
}

IOReturn
NeoDarwinNVMeController::failAllAction(OSObject *owner, void *, void *, void *, void *)
{
	static_cast<NeoDarwinNVMeController *>(owner)->failAll(kIOReturnNotAttached);
	return kIOReturnSuccess;
}

void
NeoDarwinNVMeController::free()
{
	for (uint32_t i = 0; i <= kMaxIOQueues; i++) {
		Queue *q = queues[i];
		if (q == NULL) {
			continue;
		}
		for (uint32_t s = 0; s < kMaxSlots; s++) {
			OSSafeReleaseNULL(q->slots[s].dma);
		}
		if (q->memory != NULL) {
			q->memory->complete();
			OSSafeReleaseNULL(q->memory);
		}
		IOFree(q, sizeof(Queue));
		queues[i] = NULL;
	}
	if (identifyBuffer != NULL) {
		identifyBuffer->complete();
		OSSafeReleaseNULL(identifyBuffer);
	}
	OSSafeReleaseNULL(bar);
	if (workLoop != NULL) {
		if (timer != NULL) {
			workLoop->removeEventSource(timer);
		}
		if (gate != NULL) {
			workLoop->removeEventSource(gate);
		}
	}
	OSSafeReleaseNULL(timer);
	OSSafeReleaseNULL(gate);
	OSSafeReleaseNULL(workLoop);
	super::free();
}

IOWorkLoop *
NeoDarwinNVMeController::getWorkLoop() const
{
	return workLoop;
}

// MARK: - Admin commands

IOReturn
NeoDarwinNVMeController::adminAction(OSObject *owner, void *arg0, void *arg1, void *, void *)
{
	NeoDarwinNVMeController *self = static_cast<NeoDarwinNVMeController *>(owner);
	return self->admin((struct nd_nvme_sqe *)arg0, (uint32_t *)arg1, false);
}

IOReturn
NeoDarwinNVMeController::adminFromClient(struct nd_nvme_sqe *sqe, uint32_t *dw0)
{
	return gate->runAction(&NeoDarwinNVMeController::adminAction, sqe, dw0);
}

// One admin command, in the gate. From a client thread it sleeps until
// vector 0's handler has reaped the completion; on the work loop itself
// (recovery) it polls the admin completion queue.
IOReturn
NeoDarwinNVMeController::admin(struct nd_nvme_sqe *sqe, uint32_t *dw0, bool poll)
{
	Queue *q = queues[0];
	adminCID++;
	sqe->cdw0 = (sqe->cdw0 & 0xffff) | (uint32_t)adminCID << 16;
	adminDone = false;
	adminWaiting = true;
	adminStatus = 0;
	post(q, sqe);
	uint64_t deadline;
	clock_interval_to_deadline(kAdminTimeoutMs, kMillisecondScale, &deadline);
	if (!poll) {
		while (!adminDone && mach_absolute_time() < deadline) {
			gate->commandSleep(&adminDone, deadline, THREAD_UNINT);
		}
		if (adminDone) {
			adminByInterrupt++;
		} else {
			reap(q);
			if (adminDone) {
				IOLog("NeoDarwinNVMeController: %s: the admin completion is in the queue but its interrupt "
				    "(%s) never came: interrupts are not being delivered\n", where, vectors != 0 ? "MSI-X vector 0" : "INTx");
				adminWaiting = false;
				return kIOReturnNotResponding;
			}
		}
	} else {
		while (!adminDone && mach_absolute_time() < deadline) {
			reap(q);
			if (!adminDone) {
				IOSleep(1);
			}
		}
	}
	adminWaiting = false;
	if (!adminDone) {
		IOLog("NeoDarwinNVMeController: %s: admin command 0x%02x timed out (CSTS 0x%x)\n", where, sqe->cdw0 & 0xff,
		    rd32(kNDNVMeRegCSTS));
		return kIOReturnTimeout;
	}
	*dw0 = adminDW0;
	return ((adminStatus >> 1) == 0) ? kIOReturnSuccess : statusResult(adminStatus);
}

// MARK: - Requests

IOReturn
NeoDarwinNVMeController::readWrite(NeoDarwinNVMeNamespace *ns, IOMemoryDescriptor *buffer, UInt64 block, UInt64 nblks,
    bool fua, IOStorageCompletion *completion)
{
	Request r = { ns, buffer, block, nblks, fua, completion };
	return gate->runAction(&NeoDarwinNVMeController::submitAction, &r);
}

IOReturn
NeoDarwinNVMeController::submitAction(OSObject *owner, void *arg0, void *, void *, void *)
{
	NeoDarwinNVMeController *self = static_cast<NeoDarwinNVMeController *>(owner);
	Request *r = (Request *)arg0;
	self->submit(r->ns, r->buffer, r->block, r->nblks, r->fua, r->completion, false);
	return kIOReturnSuccess;
}

// A free slot, starting with the queue pair of the CPU submitting.
bool
NeoDarwinNVMeController::freeSlot(Queue **queue, uint32_t *slot)
{
	uint32_t first = (uint32_t)cpu_number() % ioQueues;
	for (uint32_t k = 0; k < ioQueues; k++) {
		Queue *q = queues[1 + (first + k) % ioQueues];
		if (q->busy >= q->slotCount) {
			continue;
		}
		for (uint32_t s = 0; s < q->slotCount; s++) {
			if (q->slots[s].state == kFree) {
				*queue = q;
				*slot = s;
				return true;
			}
		}
	}
	return false;
}

// In the gate: issue a read or write, or keep it until a slot is free.
void
NeoDarwinNVMeController::submit(NeoDarwinNVMeNamespace *ns, IOMemoryDescriptor *buffer, UInt64 block, UInt64 nblks,
    bool fua, IOStorageCompletion *completion, bool front)
{
	if (!isRunning) {
		IOStorage::complete(completion, kIOReturnNotReady, 0);
		return;
	}
	Queue *q;
	uint32_t slot;
	if (!freeSlot(&q, &slot)) {
		Pending *p = IOMallocType(Pending);
		p->ns = ns;
		p->buffer = buffer;
		p->block = block;
		p->nblks = nblks;
		p->fua = fua;
		p->completion = *completion;
		buffer->retain();
		if (front) {
			enqueue_head(&pending, &p->link);
		} else {
			enqueue_tail(&pending, &p->link);
		}
		pendingCount++;
		return;
	}
	Slot *s = &q->slots[slot];
	s->state = kAsync;
	s->ns = ns;
	s->buffer = buffer;
	s->completion = *completion;
	s->block = block;
	s->nblks = nblks;
	s->bytes = nblks << ns->lbaShift;
	s->fua = fua;
	s->opcode = buffer->getDirection() == kIODirectionIn ? kNDNVMeCmdRead : kNDNVMeCmdWrite;
	buffer->retain();
	IOReturn r = issue(q, slot);
	if (r != kIOReturnSuccess) {
		s->state = kFree;
		s->buffer = NULL;
		buffer->release();
		IOStorage::complete(completion, r, 0);
	}
}

// The PRPs of a prepared slot (§4.3): PRP1 the first byte, then one entry
// per 4 KiB page, in PRP2 if there is one more, else in the slot's list.
// kIOReturnNotAligned: the segments break the rules (a segment after the
// first not starting on a page, or one before the last not ending on one).
// `clean`: write back and invalidate each segment (a double buffer on a
// device that isn't coherent).
IOReturn
NeoDarwinNVMeController::buildPRPs(Slot *s, uint64_t *prp1, uint64_t *prp2, bool clean)
{
	uint32_t capacity = prpListBytes / 8;
	uint32_t n = 0;                 // entries so far; entry i > 0 is list[i - 1]
	uint64_t first = 0, end = 0;
	uint32_t index = 0;
	UInt64 offset = 0;
	while (offset < s->bytes) {
		IODMACommand::Segment64 seg[8];
		UInt32 count = 8;
		IOReturn r = s->dma->gen64IOVMSegments(&offset, seg, &count);
		if (r != kIOReturnSuccess) {
			return r;
		}
		if (count == 0) {
			return kIOReturnUnderrun;
		}
		for (UInt32 i = 0; i < count; i++, index++) {
			uint64_t a = seg[i].fIOVMAddr, len = seg[i].fLength;
			if (len == 0) {
				continue;
			}
			if ((a & 3) != 0 || (index > 0 && ((a & (kNDNVMePage - 1)) != 0 || (end & (kNDNVMePage - 1)) != 0))) {
				return kIOReturnNotAligned;
			}
			if (clean) {
				unsigned int res = 0;
				dcache_incoherent_io_flush64(a, (unsigned int)len, (unsigned int)len, &res);
			}
			end = a + len;
			while (a < end) {
				if (n == 0) {
					first = a;
				} else if (n - 1 < capacity) {
					s->prpList[n - 1] = a;
				} else {
					return kIOReturnNoResources;
				}
				n++;
				a = (a & ~(uint64_t)(kNDNVMePage - 1)) + kNDNVMePage;
			}
		}
	}
	*prp1 = first;
	if (n <= 1) {
		*prp2 = 0;
	} else if (n == 2) {
		*prp2 = s->prpList[0];
	} else {
		*prp2 = s->prpListPA;
		dmaPolicy.toDevice(s->prpList, (n - 1) * 8);
	}
	return kIOReturnSuccess;
}

// Puts a command in a submission queue and rings its tail doorbell.
void
NeoDarwinNVMeController::post(Queue *q, struct nd_nvme_sqe *sqe)
{
	uint8_t *entry = q->sq + q->sqTail * kNDNVMeSQESize;
	memcpy(entry, sqe, kNDNVMeSQESize);
	dmaPolicy.toDevice(entry, kNDNVMeSQESize);
	q->sqTail = (q->sqTail + 1) % q->entries;
	__builtin_arm_dsb(0xf);         // DSB SY: the entry in memory before the doorbell
	wr32(q->sqDoorbell, q->sqTail);
}

// Builds and posts the command of a slot whose fields are set. The caller
// holds the gate.
IOReturn
NeoDarwinNVMeController::issue(Queue *q, uint32_t slot)
{
	Slot *s = &q->slots[slot];
	if (s->opcode != kNDNVMeCmdRead && !writeAllowed) {
		// The write policy, whatever asked: nothing but reads reaches a
		// read-only controller.
		return kIOReturnNotWritable;
	}
	struct nd_nvme_sqe sqe = {};
	sqe.cdw0 = s->opcode | (uint32_t)slot << 16;
	sqe.nsid = s->ns->nsid;
	if (s->buffer != NULL) {
		IOReturn r = s->dma->setMemoryDescriptor(s->buffer, false);
		if (r == kIOReturnSuccess) {
			r = s->dma->prepare(0, s->bytes);
			if (r != kIOReturnSuccess) {
				s->dma->clearMemoryDescriptor();
			}
		}
		if (r == kIOReturnSuccess) {
			r = buildPRPs(s, &sqe.prp1, &sqe.prp2, false);
			if (r == kIOReturnNotAligned) {
				// Segments PRPs can't describe: through a page-aligned
				// copy.
				r = s->dma->synchronize(IODMACommand::kForceDoubleBuffer);
				if (r == kIOReturnSuccess) {
					r = buildPRPs(s, &sqe.prp1, &sqe.prp2, !dmaPolicy.coherent);
				}
			}
			if (r != kIOReturnSuccess) {
				s->dma->complete();
				s->dma->clearMemoryDescriptor();
			}
		}
		if (r != kIOReturnSuccess) {
			IOLog("NeoDarwinNVMeController: %s: %s of %llu bytes: no DMA (0x%x)\n", where, opcodeName(s->opcode), s->bytes, r);
			return r;
		}
		sqe.cdw10 = (uint32_t)s->block;
		sqe.cdw11 = (uint32_t)(s->block >> 32);
		sqe.cdw12 = (uint32_t)(s->nblks - 1) | (s->fua ? kNDNVMeRWForceUnitAccess : 0);
	}
	s->deadline = mach_absolute_time() + timeoutAbs;
	q->busy++;
	inFlight++;
	post(q, &sqe);
	if (!timerArmed) {
		timerArmed = true;
		timer->setTimeoutMS(kTimerMs);
	}
	return kIOReturnSuccess;
}

// A cache flush of one namespace, waited for.
IOReturn
NeoDarwinNVMeController::flush(NeoDarwinNVMeNamespace *ns)
{
	return gate->runAction(&NeoDarwinNVMeController::flushAction, ns);
}

IOReturn
NeoDarwinNVMeController::flushAction(OSObject *owner, void *arg0, void *, void *, void *)
{
	NeoDarwinNVMeController *self = static_cast<NeoDarwinNVMeController *>(owner);
	Queue *q;
	uint32_t slot;
	for (;;) {
		if (!self->isRunning) {
			return kIOReturnNotReady;
		}
		if (self->freeSlot(&q, &slot)) {
			break;
		}
		self->gate->commandSleep(&self->pending, THREAD_UNINT);
	}
	Slot *s = &q->slots[slot];
	s->state = kSync;
	s->ns = (NeoDarwinNVMeNamespace *)arg0;
	s->buffer = NULL;
	s->bytes = 0;
	s->block = 0;
	s->nblks = 0;
	s->fua = false;
	s->opcode = kNDNVMeCmdFlush;
	IOReturn r = self->issue(q, slot);
	if (r != kIOReturnSuccess) {
		s->state = kFree;
		return r;
	}
	while (s->state != kSyncDone) {
		self->gate->commandSleep(s, THREAD_UNINT);
	}
	r = s->result;
	s->state = kFree;
	self->startPending();
	self->gate->commandWakeup(&self->pending);
	return r;
}

// Starts waiting requests while there are free slots.
void
NeoDarwinNVMeController::startPending()
{
	Queue *q;
	uint32_t slot;
	while (pendingCount != 0 && isRunning && freeSlot(&q, &slot)) {
		Pending *p = (Pending *)dequeue_head(&pending);
		pendingCount--;
		submit(p->ns, p->buffer, p->block, p->nblks, p->fua, &p->completion, false);
		p->buffer->release();           // submit took its own reference
		IOFreeType(p, Pending);
	}
}

// A slot's command is over: its buffer's DMA completed, its client told.
void
NeoDarwinNVMeController::finish(Queue *q, uint32_t slot, IOReturn result)
{
	Slot *s = &q->slots[slot];
	if (s->buffer != NULL) {
		s->dma->complete();
		s->dma->clearMemoryDescriptor();
	}
	q->busy--;
	inFlight--;
	q->completions++;
	if (result == kIOReturnSuccess) {
		resets = 0;
	}
	if (s->state == kSync) {
		s->result = result;
		s->state = kSyncDone;
		gate->commandWakeup(s);
		return;
	}
	IOStorageCompletion completion = s->completion;
	IOMemoryDescriptor *buffer = s->buffer;
	uint64_t bytes = result == kIOReturnSuccess ? s->bytes : 0;
	s->buffer = NULL;
	s->state = kFree;
	buffer->release();
	IOStorage::complete(&completion, result, bytes);
}

// MARK: - Completions

// INTx, in primary interrupt context: is there a new entry in any
// completion queue? Then mask the pin until the work loop has reaped.
bool
NeoDarwinNVMeController::filter(OSObject *owner, IOFilterInterruptEventSource *source)
{
	(void)source;
	NeoDarwinNVMeController *self = static_cast<NeoDarwinNVMeController *>(owner);
	for (uint32_t i = 0; i <= self->ioQueues; i++) {
		Queue *q = self->queues[i];
		if (q == NULL || q->memory == NULL) {
			continue;
		}
		volatile struct nd_nvme_cqe *e = &q->cq[q->cqHead];
		self->dmaPolicy.fromDevice(e, kNDNVMeCQESize);
		if ((e->status & 1) == q->phase) {
			self->wr32(kNDNVMeRegINTMS, 1);
			return true;
		}
	}
	return false;
}

void
NeoDarwinNVMeController::interrupt(OSObject *owner, IOInterruptEventSource *source, int count)
{
	(void)count;
	NeoDarwinNVMeController *self = static_cast<NeoDarwinNVMeController *>(owner);
	if (self->vectors == 0) {
		self->reapAll();
		self->wr32(kNDNVMeRegINTMC, 1);
	} else {
		uint32_t v = (uint32_t)source->getIntIndex();
		if (v == 0) {
			self->reap(self->queues[0]);
			if (self->vectors == 1) {
				self->reap(self->queues[1]);
			}
		} else if (v <= self->ioQueues) {
			self->reap(self->queues[v]);
		}
	}
	// Each I/O queue's first completion, once: its interrupt works.
	for (uint32_t i = 1; i <= self->ioQueues; i++) {
		Queue *q = self->queues[i];
		if (q != NULL && !q->announced && q->completions != 0) {
			q->announced = true;
			if (self->vectors != 0) {
				OSNumber *lpi = OSDynamicCast(OSNumber, self->pci->getProperty("msi-lpi-base"));
				IOLog("NeoDarwinNVMeController: %s: I/O queue %u: first completion by its interrupt (MSI-X vector %u, LPI %u)\n",
				    self->where, i, q->vector, (lpi != NULL ? lpi->unsigned32BitValue() : 0) + q->vector);
			} else {
				IOLog("NeoDarwinNVMeController: %s: I/O queue %u: first completion by its interrupt (INTx)\n", self->where, i);
			}
		}
	}
	self->startPending();
	self->gate->commandWakeup(&self->pending);
}

void
NeoDarwinNVMeController::reapAll()
{
	for (uint32_t i = 0; i <= ioQueues; i++) {
		reap(queues[i]);
	}
}

// On the work loop: every completion the controller has posted, then the
// head doorbell.
uint32_t
NeoDarwinNVMeController::reap(Queue *q)
{
	if (q == NULL || q->memory == NULL) {
		return 0;
	}
	uint32_t n = 0;
	for (;;) {
		volatile struct nd_nvme_cqe *e = &q->cq[q->cqHead];
		dmaPolicy.fromDevice(e, kNDNVMeCQESize);
		uint16_t status = e->status;
		if ((status & 1) != q->phase) {
			break;
		}
		__builtin_arm_dmb(0x1);         // DMB OSHLD: the phase tag before the rest
		uint16_t cid = e->cid;
		uint32_t dw0 = e->dw0;
		if (++q->cqHead == q->entries) {
			q->cqHead = 0;
			q->phase ^= 1;
		}
		n++;
		if (q->id == 0) {
			if (adminWaiting && cid == adminCID) {
				adminStatus = status;
				adminDW0 = dw0;
				adminDone = true;
				gate->commandWakeup(&adminDone);
			} else {
				IOLog("NeoDarwinNVMeController: %s: a stray admin completion (command %u)\n", where, cid);
			}
			continue;
		}
		if (cid >= q->slotCount || q->slots[cid].state == kFree || q->slots[cid].state == kSyncDone) {
			IOLog("NeoDarwinNVMeController: %s: I/O queue %u: a completion for command %u, which is not in flight\n",
			    where, q->id, cid);
			continue;
		}
		IOReturn result = (status >> 1) == 0 ? kIOReturnSuccess : statusResult(status);
		if (result != kIOReturnSuccess) {
			Slot *s = &q->slots[cid];
			IOLog("NeoDarwinNVMeController: %s: namespace %u: %s of %llu blocks at %llu failed: status type %u code 0x%02x%s\n",
			    where, s->ns->nsid, opcodeName(s->opcode), s->nblks, s->block, ND_NVME_STATUS_SCT(status),
			    ND_NVME_STATUS_SC(status), ND_NVME_STATUS_DNR(status) ? " (do not retry)" : "");
		}
		finish(q, cid, result);
	}
	if (n != 0) {
		wr32(q->cqDoorbell, q->cqHead);
	}
	return n;
}

// MARK: - Timeouts and recovery

void
NeoDarwinNVMeController::timerFired(OSObject *owner, IOTimerEventSource *sender)
{
	NeoDarwinNVMeController *self = static_cast<NeoDarwinNVMeController *>(owner);
	self->checkHealth();
	if (self->isRunning && self->inFlight != 0) {
		sender->setTimeoutMS(kTimerMs);
	} else {
		self->timerArmed = false;
	}
}

void
NeoDarwinNVMeController::checkHealth()
{
	if (!isRunning) {
		return;
	}
	uint32_t csts = rd32(kNDNVMeRegCSTS);
	if (csts == 0xffffffff) {
		IOLog("NeoDarwinNVMeController: %s: the controller reads all ones: gone\n", where);
		isRunning = false;
		dead = true;
		failAll(kIOReturnNoDevice);
		return;
	}
	if (csts & kNDNVMeCSTSFatal) {
		recover("controller fatal status (CSTS.CFS)");
		return;
	}
	uint64_t now = mach_absolute_time();
	bool expired = false;
	for (uint32_t i = 1; i <= ioQueues && !expired; i++) {
		for (uint32_t s = 0; s < queues[i]->slotCount; s++) {
			SlotState st = queues[i]->slots[s].state;
			if ((st == kAsync || st == kSync) && queues[i]->slots[s].deadline < now) {
				expired = true;
				break;
			}
		}
	}
	if (!expired) {
		return;
	}
	// A lost interrupt looks like a timeout: look at the queues first.
	if (vectors == 0) {
		reapAll();
	} else {
		for (uint32_t i = 1; i <= ioQueues; i++) {
			if (reap(queues[i]) != 0) {
				IOLog("NeoDarwinNVMeController: %s: I/O queue %u had completions its interrupt (vector %u) never reported\n",
				    where, i, queues[i]->vector);
			}
		}
	}
	for (uint32_t i = 1; i <= ioQueues; i++) {
		for (uint32_t s = 0; s < queues[i]->slotCount; s++) {
			Slot *slot = &queues[i]->slots[s];
			if ((slot->state == kAsync || slot->state == kSync) && slot->deadline < now) {
				IOLog("NeoDarwinNVMeController: %s: namespace %u: %s of %llu blocks at %llu (I/O queue %u, command %u) "
				    "timed out after %u s\n", where, slot->ns->nsid, opcodeName(slot->opcode), slot->nblks, slot->block, i, s,
				    kIOTimeoutMs / 1000);
				recover("command timeout");
				return;
			}
		}
	}
	startPending();
	gate->commandWakeup(&pending);
}

// On the work loop: reset the controller, which stops its DMA; fail what
// timed out, put the rest back; bring the queues up again.
void
NeoDarwinNVMeController::recover(const char *why)
{
	resets++;
	IOLog("NeoDarwinNVMeController: %s: %s; resetting the controller (%u of %u)\n", where, why, resets, kMaxResets);
	if (resets > kMaxResets) {
		IOLog("NeoDarwinNVMeController: %s: giving up on the controller\n", where);
		isRunning = false;
		dead = true;
		disableController();
		failAll(kIOReturnNotResponding);
		return;
	}
	disableController();
	uint64_t now = mach_absolute_time();
	for (uint32_t i = 1; i <= ioQueues; i++) {
		Queue *q = queues[i];
		for (uint32_t k = 0; k < q->slotCount; k++) {
			Slot *s = &q->slots[k];
			if (s->state != kAsync && s->state != kSync) {
				continue;
			}
			if (s->buffer != NULL) {
				s->dma->complete();
				s->dma->clearMemoryDescriptor();
			}
			q->busy--;
			inFlight--;
			bool expired = s->deadline < now;
			if (s->state == kSync) {
				s->result = expired ? kIOReturnTimeout : kIOReturnAborted;
				s->state = kSyncDone;
				gate->commandWakeup(s);
			} else if (expired) {
				IOStorageCompletion completion = s->completion;
				IOMemoryDescriptor *buffer = s->buffer;
				s->buffer = NULL;
				s->state = kFree;
				buffer->release();
				IOStorage::complete(&completion, kIOReturnTimeout, 0);
			} else {
				Pending *p = IOMallocType(Pending);
				p->ns = s->ns;
				p->buffer = s->buffer;          // the slot's reference
				p->block = s->block;
				p->nblks = s->nblks;
				p->fua = s->fua;
				p->completion = s->completion;
				enqueue_head(&pending, &p->link);
				pendingCount++;
				s->buffer = NULL;
				s->state = kFree;
			}
		}
		resetQueue(q);
	}
	uint32_t allocated = 0;
	bool ok = bringUp(true, &allocated) && allocated >= ioQueues;
	for (uint32_t i = 1; ok && i <= ioQueues; i++) {
		ok = createIOQueue(queues[i], true);
	}
	if (!ok) {
		IOLog("NeoDarwinNVMeController: %s: the controller did not come back; giving up\n", where);
		isRunning = false;
		dead = true;
		disableController();
		failAll(kIOReturnNotResponding);
		return;
	}
	IOLog("NeoDarwinNVMeController: %s: the controller is back; %u requests to resubmit\n", where, pendingCount);
	startPending();
	gate->commandWakeup(&pending);
}

// Every request in flight or waiting, failed with `result`.
void
NeoDarwinNVMeController::failAll(IOReturn result)
{
	for (uint32_t i = 1; i <= ioQueues; i++) {
		Queue *q = queues[i];
		if (q == NULL) {
			continue;
		}
		for (uint32_t k = 0; k < q->slotCount; k++) {
			Slot *s = &q->slots[k];
			if (s->state == kAsync || s->state == kSync) {
				finish(q, k, result);
			}
		}
	}
	while (pendingCount != 0) {
		Pending *p = (Pending *)dequeue_head(&pending);
		pendingCount--;
		IOStorage::complete(&p->completion, result, 0);
		p->buffer->release();
		IOFreeType(p, Pending);
	}
	gate->commandWakeup(&pending);
}
