// SPDX-License-Identifier: BSD-2-Clause
// NeoDarwin-Language: expressibility: IOKit drivers are C++ IOService subclasses (IOService, IOBlockStorageDevice).
//
// NeoDarwin's NVMe driver (docs/kernel/storage.md, "NVMe"): a controller
// driver on the IOPCIDevice and one IOBlockStorageDevice nub per active
// namespace, which IOBlockStorageDriver matches.
//
//   IOPCIDevice class 010802
//     NeoDarwinNVMeController (IOService): registers, admin queue, I/O queue
//                                          pairs, interrupts, PRPs, recovery
//       NeoDarwinNVMeNamespace (IOBlockStorageDevice), one per namespace
//         IOBlockStorageDriver -> IOMedia -> IOGUIDPartitionScheme -> ...
#ifndef ND_NVME_DRIVER_H
#define ND_NVME_DRIVER_H

#include <IOKit/IOService.h>
#include <IOKit/IOWorkLoop.h>
#include <IOKit/IOCommandGate.h>
#include <IOKit/IOInterruptEventSource.h>
#include <IOKit/IOFilterInterruptEventSource.h>
#include <IOKit/IOTimerEventSource.h>
#include <IOKit/pci/IOPCIDevice.h>
#include <IOKit/storage/IOBlockStorageDevice.h>
#include <kern/queue.h>

#include "nd_nvme.h"
#include "NeoDarwinStorageDMA.h"

class NeoDarwinNVMeNamespace;

class NeoDarwinNVMeController : public IOService
{
	OSDeclareDefaultStructors(NeoDarwinNVMeController);

public:
	virtual bool start(IOService *provider) APPLE_KEXT_OVERRIDE;
	virtual void stop(IOService *provider) APPLE_KEXT_OVERRIDE;
	virtual void free() APPLE_KEXT_OVERRIDE;
	virtual IOWorkLoop *getWorkLoop() const APPLE_KEXT_OVERRIDE;

	// For the namespaces. Reads and writes complete through `completion`,
	// on the work loop; a flush is waited for.
	IOReturn readWrite(NeoDarwinNVMeNamespace *ns, IOMemoryDescriptor *buffer, UInt64 block, UInt64 nblks,
	    bool fua, IOStorageCompletion *completion);
	IOReturn flush(NeoDarwinNVMeNamespace *ns);
	bool writable() const { return writeAllowed; }
	bool volatileWriteCache() const { return vwc; }
	bool writeCacheEnabled() const { return wce; }
	bool running() const { return isRunning; }
	const char *location() const { return where; }
	const char *model() const { return modelString; }
	const char *serial() const { return serialString; }
	const char *firmware() const { return firmwareString; }
	uint64_t maxTransferBytes() const { return maxTransfer; }
	uint8_t addressBits() const { return dmaPolicy.addressBits; }

private:
	static const uint32_t kMaxIOQueues = 8;         // I/O queue pairs, one per CPU up to this
	static const uint32_t kMaxSlots = 32;           // commands in flight per I/O queue
	static const uint32_t kAdminEntries = 32;
	static const uint32_t kIOEntries = 64;          // > kMaxSlots: the SQ never fills
	static const uint32_t kMaxNamespaces = 16;
	static const uint64_t kMaxTransferCap = 512 * 1024;
	static const uint32_t kIOTimeoutMs = 30000;
	static const uint32_t kAdminTimeoutMs = 10000;
	static const uint32_t kTimerMs = 1000;
	static const uint32_t kMaxResets = 3;

	enum SlotState : uint8_t { kFree, kAsync, kSync, kSyncDone };
	struct Slot {
		IODMACommand *dma;
		uint64_t *prpList;              // this slot's PRP list, in queue memory
		uint64_t prpListPA;
		NeoDarwinNVMeNamespace *ns;
		IOMemoryDescriptor *buffer;     // retained while in flight
		IOStorageCompletion completion;
		UInt64 block, nblks;
		uint64_t bytes;
		uint64_t deadline;              // mach_absolute_time
		IOReturn result;                // kSync: set at completion
		uint8_t opcode;
		bool fua;
		SlotState state;
	};
	struct Queue {
		uint16_t id;                    // 0: admin
		uint16_t vector;                // MSI-X vector of its completion queue
		uint32_t entries;
		IOBufferMemoryDescriptor *memory;   // SQ, CQ, and for I/O queues the PRP lists
		uint8_t *sq;
		volatile struct nd_nvme_cqe *cq;
		uint64_t sqPA, cqPA;
		uint32_t sqTail, cqHead;
		uint16_t phase;
		uint32_t sqDoorbell, cqDoorbell;
		uint32_t slotCount, busy;
		uint64_t completions;
		bool announced;                 // its first completion logged
		Slot slots[kMaxSlots];
	};
	// A read or write that found no free slot, or that a reset put back.
	struct Pending {
		queue_chain_t link;
		NeoDarwinNVMeNamespace *ns;
		IOMemoryDescriptor *buffer;     // retained
		UInt64 block, nblks;
		bool fua;
		IOStorageCompletion completion;
	};
	struct Request {
		NeoDarwinNVMeNamespace *ns;
		IOMemoryDescriptor *buffer;
		UInt64 block, nblks;
		bool fua;
		IOStorageCompletion *completion;
	};

	uint32_t rd32(uint32_t off) const { return *(volatile uint32_t *)(regs + off); }
	void wr32(uint32_t off, uint32_t v) { *(volatile uint32_t *)(regs + off) = v; }
	uint64_t rd64(uint32_t off) const { return (uint64_t)rd32(off) | (uint64_t)rd32(off + 4) << 32; }
	void wr64(uint32_t off, uint64_t v) { wr32(off, (uint32_t)v); wr32(off + 4, (uint32_t)(v >> 32)); }

	bool startController();
	void policy();
	bool setUpInterrupts(uint32_t wanted);
	bool allocateQueue(Queue *q, uint16_t id, uint32_t entries, bool io);
	void resetQueue(Queue *q);
	bool waitReady(bool ready, uint32_t timeoutMs);
	bool disableController();
	bool enableController();
	bool bringUp(bool poll, uint32_t *allocated);
	bool createIOQueue(Queue *q, bool poll);
	bool identifyController();
	uint32_t findNamespaces(uint32_t *nsids, uint32_t max);
	void publishNamespace(uint32_t nsid);

	static IOReturn adminAction(OSObject *owner, void *arg0, void *arg1, void *arg2, void *arg3);
	IOReturn admin(struct nd_nvme_sqe *sqe, uint32_t *dw0, bool poll);
	IOReturn adminFromClient(struct nd_nvme_sqe *sqe, uint32_t *dw0);

	static IOReturn submitAction(OSObject *owner, void *arg0, void *arg1, void *arg2, void *arg3);
	static IOReturn flushAction(OSObject *owner, void *arg0, void *arg1, void *arg2, void *arg3);
	static IOReturn failAllAction(OSObject *owner, void *arg0, void *arg1, void *arg2, void *arg3);
	void submit(NeoDarwinNVMeNamespace *ns, IOMemoryDescriptor *buffer, UInt64 block, UInt64 nblks, bool fua,
	    IOStorageCompletion *completion, bool front);
	bool freeSlot(Queue **q, uint32_t *slot);
	IOReturn issue(Queue *q, uint32_t slot);
	IOReturn buildPRPs(Slot *s, uint64_t *prp1, uint64_t *prp2, bool clean);
	void post(Queue *q, struct nd_nvme_sqe *sqe);
	void startPending();
	void finish(Queue *q, uint32_t slot, IOReturn result);

	static bool filter(OSObject *owner, IOFilterInterruptEventSource *source);
	static void interrupt(OSObject *owner, IOInterruptEventSource *source, int count);
	static void timerFired(OSObject *owner, IOTimerEventSource *sender);
	uint32_t reap(Queue *q);
	void reapAll();
	void checkHealth();
	void recover(const char *why);
	void failAll(IOReturn result);

	IOPCIDevice *pci = NULL;
	char where[24] = {};
	IOMemoryMap *bar = NULL;
	volatile uint8_t *regs = NULL;
	uint64_t cap = 0;
	uint32_t version = 0;
	uint32_t stride = 4;
	uint32_t readyTimeoutMs = 500;
	uint16_t vendorID = 0, deviceID = 0;
	bool isQEMU = false;
	bool writeAllowed = false;
	const char *policyReason = "";

	char modelString[41] = {};
	char serialString[21] = {};
	char firmwareString[9] = {};
	uint32_t mdtsBytes = 0;             // 0: no limit
	uint64_t maxTransfer = 0;
	uint32_t prpListBytes = 0;
	uint32_t namespaceCount = 0;        // NN
	bool vwc = false, wce = false;

	NDStorageDMA dmaPolicy;
	IOBufferMemoryDescriptor *identifyBuffer = NULL;
	uint8_t *identify = NULL;
	uint64_t identifyPA = 0;

	Queue *queues[1 + kMaxIOQueues] = {};   // [0] admin
	uint32_t ioQueues = 0;
	uint32_t vectors = 0;               // MSI-X vectors: 0 (INTx) or more
	IOInterruptEventSource *sources[1 + kMaxIOQueues] = {};

	// The one admin command in flight.
	uint16_t adminCID = 0;
	bool adminWaiting = false, adminDone = false;
	uint16_t adminStatus = 0;
	uint32_t adminDW0 = 0;
	uint32_t adminByInterrupt = 0;

	queue_head_t pending;
	uint32_t pendingCount = 0;
	uint32_t inFlight = 0;

	IOWorkLoop *workLoop = NULL;
	IOCommandGate *gate = NULL;
	IOTimerEventSource *timer = NULL;
	bool timerArmed = false;
	bool isRunning = false;
	bool dead = false;
	uint32_t resets = 0;
	uint64_t timeoutAbs = 0;

	NeoDarwinNVMeNamespace *namespaces[kMaxNamespaces] = {};
	uint32_t published = 0;
};

class NeoDarwinNVMeNamespace : public IOBlockStorageDevice
{
	OSDeclareDefaultStructors(NeoDarwinNVMeNamespace);

public:
	bool initWith(NeoDarwinNVMeController *controller, uint32_t nsid, uint32_t lbaShift, uint64_t blocks,
	    bool writeProtected);
	virtual bool start(IOService *provider) APPLE_KEXT_OVERRIDE;

	virtual IOReturn doEjectMedia(void) APPLE_KEXT_OVERRIDE;
	virtual IOReturn doFormatMedia(UInt64 byteCapacity) APPLE_KEXT_OVERRIDE;
	virtual UInt32 doGetFormatCapacities(UInt64 *capacities, UInt32 capacitiesMaxCount) const APPLE_KEXT_OVERRIDE;
	virtual char *getVendorString(void) APPLE_KEXT_OVERRIDE;
	virtual char *getProductString(void) APPLE_KEXT_OVERRIDE;
	virtual char *getRevisionString(void) APPLE_KEXT_OVERRIDE;
	virtual char *getAdditionalDeviceInfoString(void) APPLE_KEXT_OVERRIDE;
	virtual IOReturn reportBlockSize(UInt64 *blockSize) APPLE_KEXT_OVERRIDE;
	virtual IOReturn reportEjectability(bool *isEjectable) APPLE_KEXT_OVERRIDE;
	virtual IOReturn reportMaxValidBlock(UInt64 *maxBlock) APPLE_KEXT_OVERRIDE;
	virtual IOReturn reportMediaState(bool *mediaPresent, bool *changedState = 0) APPLE_KEXT_OVERRIDE;
	virtual IOReturn reportRemovability(bool *isRemovable) APPLE_KEXT_OVERRIDE;
	virtual IOReturn reportWriteProtection(bool *isWriteProtected) APPLE_KEXT_OVERRIDE;
	virtual IOReturn getWriteCacheState(bool *enabled) APPLE_KEXT_OVERRIDE;
	virtual IOReturn setWriteCacheState(bool enabled) APPLE_KEXT_OVERRIDE;
	virtual IOReturn doAsyncReadWrite(IOMemoryDescriptor *buffer, UInt64 block, UInt64 nblks,
	    IOStorageAttributes *attributes, IOStorageCompletion *completion) APPLE_KEXT_OVERRIDE;
	virtual IOReturn doSynchronize(UInt64 block, UInt64 nblks, IOStorageSynchronizeOptions options = 0) APPLE_KEXT_OVERRIDE;

	uint32_t nsid = 0;
	uint32_t lbaShift = 9;
	uint64_t blocks = 0;
	bool writeProtected = true;

private:
	NeoDarwinNVMeController *controller = NULL;
	char info[40] = {};
};

#endif
