// SPDX-License-Identifier: BSD-2-Clause
// NeoDarwin-Language: expressibility: IOKit drivers are C++ IOService subclasses (IOBlockStorageDevice).
//
// A virtio 1.x block device on PCI (docs/kernel/storage.md), as an
// IOStorageFamily block storage device: IOBlockStorageDriver matches it and
// publishes its IOMedia, the partition schemes split that, and
// IOMediaBSDClient makes /dev/diskN and /dev/diskNsM.
//
//   IOPCIDevice 1af4:1042 (modern) or 1af4:1001 (transitional)
//     NeoDarwinVirtioBlock (IOBlockStorageDevice)
//       IOBlockStorageDriver
//         IOMedia (whole disk) -> IOGUIDPartitionScheme -> IOMedia (partitions)
//
// Transport: the modern PCI interface only (virtio 1.2 §4.1.4): common,
// notification, ISR and device configuration found through the vendor
// capabilities, in memory BARs. A transitional device (QEMU's
// virtio-blk-pci on a root bus without disable-legacy=on) has them too; its
// legacy I/O BAR is not used. A device without VIRTIO_F_VERSION_1 (legacy
// only) is refused.
//
// One request queue, split, without indirect descriptors or event
// suppression. The descriptor table is cut into fixed chains, one per
// request slot: a header the device reads, up to kMaxSegments data
// segments, and a status byte it writes. IOBlockStorageDriver is told the
// segment and transfer limits, so a request always fits its chain; one that
// finds no free slot waits in a list until a completion frees one.
//
// Interrupts: two MSI-X vectors (configuration changes, and the queue)
// asked for before anything resolves the device's interrupts, or one vector
// for both; without MSIs (nd_pci_msi=0, no ITS), INTx, level and shared,
// through a filter that reads (and so acknowledges) the ISR status.
//
// DMA (NeoDarwinStorageDMA.h): the rings and headers are physically
// contiguous and below dma-address-bits; data buffers go through an
// IODMACommand per slot, which bounces what lies above that limit and does
// the cache maintenance when the device isn't dma-coherent.
//
// Commands: reads (VIRTIO_BLK_T_IN), writes (T_OUT) and cache flushes
// (T_FLUSH, for doSynchronize, when VIRTIO_BLK_F_FLUSH is offered). Read-only
// from VIRTIO_BLK_F_RO; the block size from VIRTIO_BLK_F_BLK_SIZE (sectors
// stay 512 bytes); the write cache from VIRTIO_BLK_F_CONFIG_WCE. Compiled
// into the kernel (patch 0027), matched by IOPCIMatch 0x10421af4 and
// 0x10011af4.

#include <IOKit/IOKitKeys.h>
#include <IOKit/IOLib.h>
#include <IOKit/IOWorkLoop.h>
#include <IOKit/IOCommandGate.h>
#include <IOKit/IOInterruptEventSource.h>
#include <IOKit/IOFilterInterruptEventSource.h>
#include <IOKit/pci/IOPCIDevice.h>
#include <IOKit/storage/IOBlockStorageDevice.h>
#include <IOKit/storage/IOStorageDeviceCharacteristics.h>
#include <IOKit/storage/IOStorageProtocolCharacteristics.h>
#include <kern/queue.h>
#include <pexpert/pexpert.h>

#include "nd_virtio.h"
#include "NeoDarwinStorageDMA.h"

class NeoDarwinVirtioBlock : public IOBlockStorageDevice
{
	OSDeclareDefaultStructors(NeoDarwinVirtioBlock);

public:
	virtual bool start(IOService *provider) APPLE_KEXT_OVERRIDE;
	virtual void stop(IOService *provider) APPLE_KEXT_OVERRIDE;
	virtual void free() APPLE_KEXT_OVERRIDE;
	virtual IOWorkLoop *getWorkLoop() const APPLE_KEXT_OVERRIDE;

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

private:
	static const uint32_t kMaxSegments = 30;        // data segments per request
	static const uint32_t kChain = kMaxSegments + 2; // descriptors per slot
	static const uint32_t kMaxQueue = 256;
	static const uint32_t kMaxSlots = 16;
	static const uint32_t kHeaderStride = 128;      // a slot's header and status: own cache lines

	enum SlotState : uint8_t { kFree, kAsync, kSync, kSyncDone };
	struct Slot {
		IODMACommand *dma;
		IOMemoryDescriptor *buffer;
		IOStorageCompletion completion;
		uint64_t bytes;
		IOReturn result;            // kSync: the outcome, set at completion
		SlotState state;
	};
	// A read or write that found no free slot.
	struct Pending {
		queue_chain_t link;
		IOMemoryDescriptor *buffer;
		UInt64 block, nblks;
		IOStorageCompletion completion;
	};

	// MMIO in the mapped BARs.
	uint8_t rd8(volatile uint8_t *base, uint32_t off) { return *(volatile uint8_t *)(base + off); }
	uint16_t rd16(volatile uint8_t *base, uint32_t off) { return *(volatile uint16_t *)(base + off); }
	uint32_t rd32(volatile uint8_t *base, uint32_t off) { return *(volatile uint32_t *)(base + off); }
	void wr8(volatile uint8_t *base, uint32_t off, uint8_t v) { *(volatile uint8_t *)(base + off) = v; }
	void wr16(volatile uint8_t *base, uint32_t off, uint16_t v) { *(volatile uint16_t *)(base + off) = v; }
	void wr32(volatile uint8_t *base, uint32_t off, uint32_t v) { *(volatile uint32_t *)(base + off) = v; }
	void wr64(volatile uint8_t *base, uint32_t off, uint64_t v)
	{
		wr32(base, off, (uint32_t)v);
		wr32(base, off + 4, (uint32_t)(v >> 32));
	}

	bool findCapabilities();
	volatile uint8_t *mapCapability(uint8_t bar, uint32_t offset, uint32_t length);
	bool reset();
	bool negotiate();
	void readConfiguration();
	bool setUpQueue();
	bool setUpInterrupts();
	void fail(const char *why);

	static IOReturn submitAction(OSObject *owner, void *arg0, void *arg1, void *arg2, void *arg3);
	static IOReturn synchronizeAction(OSObject *owner, void *arg0, void *arg1, void *arg2, void *arg3);
	IOReturn submit(IOMemoryDescriptor *buffer, UInt64 block, UInt64 nblks, IOStorageCompletion *completion);
	IOReturn issue(uint32_t slot, uint32_t type, uint64_t sector, IOMemoryDescriptor *buffer, uint64_t bytes);
	void post(uint32_t slot, uint32_t descriptors);
	int32_t freeSlot();
	void startPending();

	static bool filter(OSObject *owner, IOFilterInterruptEventSource *source);
	static void queueInterrupt(OSObject *owner, IOInterruptEventSource *source, int count);
	static void configInterrupt(OSObject *owner, IOInterruptEventSource *source, int count);
	void reap();

	IOPCIDevice *pci = NULL;
	char where[16] = {};
	IOMemoryMap *bars[6] = {};
	volatile uint8_t *common = NULL, *notify = NULL, *isr = NULL, *config = NULL;
	uint32_t notifyMultiplier = 0;

	uint64_t features = 0;
	uint64_t capacity = 0;          // 512-byte sectors
	uint32_t blockSize = kNDVirtioBlkSectorSize;
	uint32_t sizeMax = 0;           // bytes per segment, 0: no limit
	uint32_t segments = kMaxSegments;
	bool readOnly = false;

	NDStorageDMA dmaPolicy;
	IOBufferMemoryDescriptor *ring = NULL;      // descriptors, available ring | used ring
	IOBufferMemoryDescriptor *headers = NULL;   // per slot: header, status
	struct nd_virtq_desc *desc = NULL;
	volatile uint8_t *avail = NULL, *used = NULL;
	uint8_t *headerBytes = NULL;
	uint64_t headersPA = 0;
	uint32_t queueSize = 0;
	uint32_t slotCount = 0;
	uint16_t availIdx = 0, usedIdx = 0;
	volatile uint8_t *queueNotify = NULL;
	Slot slots[kMaxSlots] = {};
	queue_head_t pending;
	uint32_t pendingCount = 0;

	IOWorkLoop *workLoop = NULL;
	IOCommandGate *gate = NULL;
	IOInterruptEventSource *queueSource = NULL, *configSource = NULL;
	uint32_t vectors = 0;           // MSI-X vectors: 0 (INTx), 1 or 2
	volatile uint32_t isrBits = 0;  // INTx: what the filter read
	uint64_t completed = 0;
	bool running = false;
};

#define super IOBlockStorageDevice
OSDefineMetaClassAndStructors(NeoDarwinVirtioBlock, IOBlockStorageDevice);

void
NeoDarwinVirtioBlock::fail(const char *why)
{
	IOLog("NeoDarwinVirtioBlock: %s: %s\n", where, why);
	if (common != NULL) {
		wr8(common, kNDVirtioDeviceStatus, rd8(common, kNDVirtioDeviceStatus) | kNDVirtioStatusFailed);
	}
}

volatile uint8_t *
NeoDarwinVirtioBlock::mapCapability(uint8_t bar, uint32_t offset, uint32_t length)
{
	if (bar > 5) {
		return NULL;
	}
	if (bars[bar] == NULL) {
		bars[bar] = pci->mapDeviceMemoryWithRegister(kIOPCIConfigBaseAddress0 + 4 * bar);
		if (bars[bar] == NULL) {
			return NULL;
		}
	}
	if ((uint64_t)offset + length > bars[bar]->getLength()) {
		return NULL;
	}
	return (volatile uint8_t *)bars[bar]->getVirtualAddress() + offset;
}

// The vendor-specific capabilities (ID 0x09) of the modern interface. The
// first capability of each type the driver can use is the one to use.
bool
NeoDarwinVirtioBlock::findCapabilities()
{
	IOByteCount found = 0;
	for (int n = 0; n < 32; n++) {
		// From the capability at `found` on; 0 when there are no more.
		pci->extendedFindPCICapability(kIOPCIVendorSpecificCapability, &found);
		if (found == 0) {
			break;
		}
		uint8_t offset = (uint8_t)found;
		uint8_t type = pci->configRead8(offset + kNDVirtioCapCfgType);
		uint8_t bar = pci->configRead8(offset + kNDVirtioCapBar);
		uint32_t off = pci->configRead32(offset + kNDVirtioCapOffset);
		uint32_t len = pci->configRead32(offset + kNDVirtioCapLength);
		switch (type) {
		case kNDVirtioCapCommon:
			if (common == NULL && len >= kNDVirtioCommonMinLength) {
				common = mapCapability(bar, off, len);
			}
			break;
		case kNDVirtioCapNotify:
			if (notify == NULL && len >= 2) {
				notify = mapCapability(bar, off, len);
				notifyMultiplier = pci->configRead32(offset + kNDVirtioCapNotifyMultiplier);
			}
			break;
		case kNDVirtioCapISR:
			if (isr == NULL && len >= 1) {
				isr = mapCapability(bar, off, len);
			}
			break;
		case kNDVirtioCapDevice:
			if (config == NULL && len >= 24) {
				config = mapCapability(bar, off, len);
			}
			break;
		default:
			break;
		}
	}
	return common != NULL && notify != NULL && isr != NULL && config != NULL;
}

bool
NeoDarwinVirtioBlock::reset()
{
	wr8(common, kNDVirtioDeviceStatus, 0);
	for (int i = 0; i < 1000; i++) {
		if (rd8(common, kNDVirtioDeviceStatus) == 0) {
			return true;
		}
		IOSleep(1);
	}
	return false;
}

bool
NeoDarwinVirtioBlock::negotiate()
{
	wr32(common, kNDVirtioDeviceFeatureSelect, 0);
	uint64_t offered = rd32(common, kNDVirtioDeviceFeature);
	wr32(common, kNDVirtioDeviceFeatureSelect, 1);
	offered |= (uint64_t)rd32(common, kNDVirtioDeviceFeature) << 32;
	if ((offered & kNDVirtioFVersion1) == 0) {
		fail("the device offers no VIRTIO_F_VERSION_1 (legacy only); not supported");
		return false;
	}
	uint64_t wanted = kNDVirtioFVersion1 | kNDVirtioBlkFSizeMax | kNDVirtioBlkFSegMax | kNDVirtioBlkFRO |
	    kNDVirtioBlkFBlkSize | kNDVirtioBlkFFlush | kNDVirtioBlkFConfigWCE |
	    // The device addresses memory through the platform's translation,
	    // which here is none: device addresses are physical ones.
	    kNDVirtioFAccessPlatform | kNDVirtioFOrderPlatform;
	features = offered & wanted;
	wr32(common, kNDVirtioDriverFeatureSelect, 0);
	wr32(common, kNDVirtioDriverFeature, (uint32_t)features);
	wr32(common, kNDVirtioDriverFeatureSelect, 1);
	wr32(common, kNDVirtioDriverFeature, (uint32_t)(features >> 32));
	wr8(common, kNDVirtioDeviceStatus, rd8(common, kNDVirtioDeviceStatus) | kNDVirtioStatusFeaturesOK);
	if ((rd8(common, kNDVirtioDeviceStatus) & kNDVirtioStatusFeaturesOK) == 0) {
		fail("the device did not accept the features (FEATURES_OK)");
		return false;
	}
	return true;
}

// The device configuration, read until the generation counter is stable.
void
NeoDarwinVirtioBlock::readConfiguration()
{
	for (int tries = 0; tries < 16; tries++) {
		uint8_t generation = rd8(common, kNDVirtioConfigGeneration);
		capacity = (uint64_t)rd32(config, kNDVirtioBlkCapacity) | (uint64_t)rd32(config, kNDVirtioBlkCapacity + 4) << 32;
		sizeMax = (features & kNDVirtioBlkFSizeMax) ? rd32(config, kNDVirtioBlkSizeMax) : 0;
		uint32_t segMax = (features & kNDVirtioBlkFSegMax) ? rd32(config, kNDVirtioBlkSegMax) : kMaxSegments;
		uint32_t size = (features & kNDVirtioBlkFBlkSize) ? rd32(config, kNDVirtioBlkBlkSize) : kNDVirtioBlkSectorSize;
		if (rd8(common, kNDVirtioConfigGeneration) != generation) {
			continue;
		}
		segments = segMax == 0 ? 1 : (segMax < kMaxSegments ? segMax : kMaxSegments);
		// A power of two from 512 bytes to 64 KiB, else 512.
		blockSize = (size >= 512 && size <= 65536 && (size & (size - 1)) == 0) ? size : kNDVirtioBlkSectorSize;
		break;
	}
	readOnly = (features & kNDVirtioBlkFRO) != 0;
}

bool
NeoDarwinVirtioBlock::setUpQueue()
{
	wr16(common, kNDVirtioQueueSelect, 0);
	uint32_t max = rd16(common, kNDVirtioQueueSize);
	if (max < kChain) {
		fail("the request queue is shorter than one request's chain");
		return false;
	}
	queueSize = max < kMaxQueue ? max : kMaxQueue;
	slotCount = queueSize / kChain < kMaxSlots ? queueSize / kChain : kMaxSlots;
	wr16(common, kNDVirtioQueueSize, (uint16_t)queueSize);

	// Descriptors (16 bytes each) and the available ring on the first
	// pages; the used ring, which the device writes, on its own page.
	size_t driverBytes = 16 * queueSize + kNDVirtqRingEntries + 2 * queueSize + 2;
	size_t usedOffset = (driverBytes + PAGE_SIZE - 1) & ~(size_t)(PAGE_SIZE - 1);
	size_t usedBytes = kNDVirtqRingEntries + kNDVirtqUsedElemSize * queueSize + 2;
	ring = dmaPolicy.allocate(usedOffset + ((usedBytes + PAGE_SIZE - 1) & ~(size_t)(PAGE_SIZE - 1)));
	headers = dmaPolicy.allocate(kHeaderStride * kMaxSlots);
	if (ring == NULL || headers == NULL) {
		fail("no memory for the request queue");
		return false;
	}
	uint8_t *base = (uint8_t *)ring->getBytesNoCopy();
	desc = (struct nd_virtq_desc *)base;
	avail = base + 16 * queueSize;
	used = base + usedOffset;
	headerBytes = (uint8_t *)headers->getBytesNoCopy();
	headersPA = NDStorageDMA::physical(headers);
	uint64_t ringPA = NDStorageDMA::physical(ring);

	// Each slot's chain: header -> data... -> status. The links are fixed;
	// a request marks where its data ends (post()).
	for (uint32_t s = 0; s < slotCount; s++) {
		uint32_t first = s * kChain;
		for (uint32_t i = 0; i < kChain; i++) {
			desc[first + i].next = (uint16_t)(first + i + 1);
		}
		desc[first].addr = headersPA + s * kHeaderStride;
		desc[first].len = sizeof(struct nd_virtio_blk_req_header);
		slots[s].dma = dmaPolicy.newCommand(sizeMax, 0);
		if (slots[s].dma == NULL) {
			fail("no DMA command for a request slot");
			return false;
		}
	}
	dmaPolicy.toDevice(base, driverBytes);

	wr64(common, kNDVirtioQueueDesc, ringPA);
	wr64(common, kNDVirtioQueueDriver, ringPA + 16 * queueSize);
	wr64(common, kNDVirtioQueueDevice, ringPA + usedOffset);
	uint32_t notifyOff = rd16(common, kNDVirtioQueueNotifyOff);
	queueNotify = notify + notifyOff * notifyMultiplier;
	return true;
}

// MSI-X first: two vectors (configuration, queue), else one for both, asked
// for before anything resolves the device's interrupts. Without them, INTx.
bool
NeoDarwinVirtioBlock::setUpInterrupts()
{
	IOReturn ret = pci->configureInterrupts(kIOInterruptTypePCIMessagedX, 1, 2, 0);
	int type = 0;
	if (ret == kIOReturnSuccess && pci->getInterruptType(0, &type) == kIOReturnSuccess && (type & kIOInterruptTypePCIMessagedX) != 0) {
		vectors = (pci->getInterruptType(1, &type) == kIOReturnSuccess && (type & kIOInterruptTypePCIMessagedX) != 0) ? 2 : 1;
	}
	if (vectors != 0) {
		int queueVector = vectors == 2 ? 1 : 0;
		queueSource = IOInterruptEventSource::interruptEventSource(this, &NeoDarwinVirtioBlock::queueInterrupt, pci, queueVector);
		if (vectors == 2) {
			configSource = IOInterruptEventSource::interruptEventSource(this, &NeoDarwinVirtioBlock::configInterrupt, pci, 0);
		}
	} else {
		// INTx: level and possibly shared, so the filter reads the ISR,
		// which also deasserts the line.
		queueSource = IOFilterInterruptEventSource::filterInterruptEventSource(this,
		    &NeoDarwinVirtioBlock::queueInterrupt, &NeoDarwinVirtioBlock::filter, pci, 0);
	}
	if (queueSource == NULL || workLoop->addEventSource(queueSource) != kIOReturnSuccess ||
	    (configSource != NULL && workLoop->addEventSource(configSource) != kIOReturnSuccess)) {
		fail("cannot register the interrupts");
		return false;
	}
	queueSource->enable();
	if (configSource != NULL) {
		configSource->enable();
	}
	// With MSI-X on (registering the source enabled it), the vectors.
	uint16_t configVector = vectors == 2 ? 0 : (vectors == 1 ? 0 : kNDVirtioNoVector);
	uint16_t queueVector = vectors == 2 ? 1 : (vectors == 1 ? 0 : kNDVirtioNoVector);
	wr16(common, kNDVirtioConfigMSIXVector, configVector);
	wr16(common, kNDVirtioQueueSelect, 0);
	wr16(common, kNDVirtioQueueMSIXVector, queueVector);
	if (vectors != 0 && (rd16(common, kNDVirtioConfigMSIXVector) != configVector ||
	    rd16(common, kNDVirtioQueueMSIXVector) != queueVector)) {
		fail("the device refused its MSI-X vectors");
		return false;
	}
	return true;
}

bool
NeoDarwinVirtioBlock::start(IOService *provider)
{
	pci = OSDynamicCast(IOPCIDevice, provider);
	if (pci == NULL || !super::start(provider)) {
		return false;
	}
	snprintf(where, sizeof(where), "%02x:%02x.%x", pci->getBusNumber(), pci->getDeviceNumber(), pci->getFunctionNumber());
	uint16_t deviceID = pci->configRead16(kIOPCIConfigDeviceID);
	queue_init(&pending);
	dmaPolicy = NDStorageDMA::forDevice(pci);

	workLoop = IOWorkLoop::workLoop();
	gate = workLoop != NULL ? IOCommandGate::commandGate(this) : NULL;
	if (gate == NULL || workLoop->addEventSource(gate) != kIOReturnSuccess) {
		fail("no work loop");
		return false;
	}
	pci->setMemoryEnable(true);
	pci->setBusLeadEnable(true);
	if (!findCapabilities()) {
		IOLog("NeoDarwinVirtioBlock: %s: 1af4:%04x has no modern (virtio 1.x) PCI capabilities; not supported\n", where, deviceID);
		return false;
	}
	if (!reset()) {
		fail("the device does not reset");
		return false;
	}
	wr8(common, kNDVirtioDeviceStatus, kNDVirtioStatusAcknowledge);
	wr8(common, kNDVirtioDeviceStatus, kNDVirtioStatusAcknowledge | kNDVirtioStatusDriver);
	if (!negotiate()) {
		return false;
	}
	readConfiguration();
	if (!setUpQueue() || !setUpInterrupts()) {
		return false;
	}
	wr16(common, kNDVirtioQueueSelect, 0);
	wr16(common, kNDVirtioQueueEnable, 1);
	wr8(common, kNDVirtioDeviceStatus, rd8(common, kNDVirtioDeviceStatus) | kNDVirtioStatusDriverOK);
	if (rd8(common, kNDVirtioDeviceStatus) & (kNDVirtioStatusNeedsReset | kNDVirtioStatusFailed)) {
		fail("the device failed after DRIVER_OK");
		return false;
	}
	running = true;

	// What IOBlockStorageDriver needs to split requests into chains: at
	// most `segments` segments, so at most segments - 1 pages of data
	// (a buffer that doesn't start on a page boundary spans one more).
	uint64_t maxBytes = (uint64_t)(segments > 1 ? segments - 1 : 1) * PAGE_SIZE;
	if (sizeMax != 0 && segments == 1 && sizeMax < maxBytes) {
		maxBytes = sizeMax & ~(uint64_t)(blockSize - 1);
	}
	setProperty(kIOMaximumSegmentCountReadKey, segments, 64);
	setProperty(kIOMaximumSegmentCountWriteKey, segments, 64);
	setProperty(kIOMaximumByteCountReadKey, maxBytes, 64);
	setProperty(kIOMaximumByteCountWriteKey, maxBytes, 64);
	if (sizeMax != 0) {
		setProperty(kIOMaximumSegmentByteCountReadKey, sizeMax, 64);
		setProperty(kIOMaximumSegmentByteCountWriteKey, sizeMax, 64);
	}
	setProperty(kIOMaximumSegmentAddressableBitCountKey, dmaPolicy.addressBits, 64);
	OSDictionary *protocol = OSDictionary::withCapacity(2);
	if (protocol != NULL) {
		OSString *type = OSString::withCString(kIOPropertyPhysicalInterconnectTypeVirtual);
		OSString *location = OSString::withCString(kIOPropertyInternalKey);
		protocol->setObject(kIOPropertyPhysicalInterconnectTypeKey, type);
		protocol->setObject(kIOPropertyPhysicalInterconnectLocationKey, location);
		OSSafeReleaseNULL(type);
		OSSafeReleaseNULL(location);
		setProperty(kIOPropertyProtocolCharacteristicsKey, protocol);
		protocol->release();
	}
	OSDictionary *device = OSDictionary::withCapacity(3);
	if (device != NULL) {
		OSString *vendor = OSString::withCString(getVendorString());
		OSString *product = OSString::withCString(getProductString());
		OSNumber *logical = OSNumber::withNumber(blockSize, 32);
		device->setObject(kIOPropertyVendorNameKey, vendor);
		device->setObject(kIOPropertyProductNameKey, product);
		device->setObject(kIOPropertyLogicalBlockSizeKey, logical);
		OSSafeReleaseNULL(vendor);
		OSSafeReleaseNULL(product);
		OSSafeReleaseNULL(logical);
		setProperty(kIOPropertyDeviceCharacteristicsKey, device);
		device->release();
	}

	char irq[64];
	if (vectors != 0) {
		OSNumber *lpi = OSDynamicCast(OSNumber, pci->getProperty("msi-lpi-base"));
		uint32_t base = lpi != NULL ? lpi->unsigned32BitValue() : 0;
		if (vectors == 2) {
			snprintf(irq, sizeof(irq), "MSI-X 2 vectors (LPIs %u-%u)", base, base + 1);
		} else {
			snprintf(irq, sizeof(irq), "MSI-X 1 vector (LPI %u)", base);
		}
	} else {
		snprintf(irq, sizeof(irq), "INTx");
	}
	bool cache = false;
	getWriteCacheState(&cache);
	uint64_t mib = capacity * kNDVirtioBlkSectorSize >> 20;
	IOLog("NeoDarwinVirtioBlock: %s: virtio-blk 1af4:%04x: %llu %u-byte blocks (%llu MiB)%s; features 0x%llx; "
	    "queue %u, %u slots of %u segments; %s; write cache %s; DMA %s, %u address bits\n", where, deviceID,
	    capacity * kNDVirtioBlkSectorSize / blockSize, blockSize, mib, readOnly ? ", read-only" : "", features,
	    queueSize, slotCount, segments, irq, cache ? "on" : "off", dmaPolicy.coherent ? "coherent" : "not coherent",
	    dmaPolicy.addressBits);

	registerService();
	return true;
}

void
NeoDarwinVirtioBlock::stop(IOService *provider)
{
	if (common != NULL) {
		reset();
	}
	running = false;
	if (queueSource != NULL) {
		queueSource->disable();
		workLoop->removeEventSource(queueSource);
		OSSafeReleaseNULL(queueSource);
	}
	if (configSource != NULL) {
		configSource->disable();
		workLoop->removeEventSource(configSource);
		OSSafeReleaseNULL(configSource);
	}
	super::stop(provider);
}

void
NeoDarwinVirtioBlock::free()
{
	for (uint32_t s = 0; s < kMaxSlots; s++) {
		OSSafeReleaseNULL(slots[s].dma);
	}
	if (ring != NULL) {
		ring->complete();
		OSSafeReleaseNULL(ring);
	}
	if (headers != NULL) {
		headers->complete();
		OSSafeReleaseNULL(headers);
	}
	for (int b = 0; b < 6; b++) {
		OSSafeReleaseNULL(bars[b]);
	}
	if (gate != NULL && workLoop != NULL) {
		workLoop->removeEventSource(gate);
	}
	OSSafeReleaseNULL(gate);
	OSSafeReleaseNULL(workLoop);
	super::free();
}

IOWorkLoop *
NeoDarwinVirtioBlock::getWorkLoop() const
{
	return workLoop;
}

// MARK: - Requests

int32_t
NeoDarwinVirtioBlock::freeSlot()
{
	for (uint32_t s = 0; s < slotCount; s++) {
		if (slots[s].state == kFree) {
			return (int32_t)s;
		}
	}
	return -1;
}

// Puts slot's chain of `descriptors` (header, data, status) on the
// available ring and notifies the device.
void
NeoDarwinVirtioBlock::post(uint32_t slot, uint32_t descriptors)
{
	uint32_t first = slot * kChain;
	struct nd_virtq_desc *last = &desc[first + descriptors - 1];
	last->flags = kNDVirtqDescWrite;        // the status byte ends the chain
	dmaPolicy.toDevice(&desc[first], sizeof(desc[0]) * descriptors);
	volatile uint16_t *entry = (volatile uint16_t *)(avail + kNDVirtqRingEntries + 2 * (availIdx % queueSize));
	*entry = (uint16_t)first;
	dmaPolicy.toDevice(entry, 2);
	availIdx++;
	*(volatile uint16_t *)(avail + kNDVirtqRingIdx) = availIdx;
	dmaPolicy.toDevice(avail + kNDVirtqRingIdx, 2);
	__builtin_arm_dsb(0xf);                 // DSB SY: the ring in memory before the doorbell
	*(volatile uint16_t *)queueNotify = 0;  // queue 0
}

// Fills slot's header, data and status descriptors and posts them. The
// caller holds the gate and has set the slot's state.
IOReturn
NeoDarwinVirtioBlock::issue(uint32_t slot, uint32_t type, uint64_t sector, IOMemoryDescriptor *buffer, uint64_t bytes)
{
	Slot *s = &slots[slot];
	uint32_t first = slot * kChain;
	uint8_t *hdr = headerBytes + slot * kHeaderStride;
	struct nd_virtio_blk_req_header header = { .type = type, .reserved = 0, .sector = sector };
	memcpy(hdr, &header, sizeof(header));
	hdr[16] = 0xff;                         // the status, until the device writes it
	dmaPolicy.toDevice(hdr, kHeaderStride);

	uint32_t n = 1;
	desc[first].flags = kNDVirtqDescNext;
	if (buffer != NULL) {
		IOReturn r = s->dma->setMemoryDescriptor(buffer, false);
		if (r == kIOReturnSuccess) {
			r = s->dma->prepare(0, bytes);
		}
		if (r != kIOReturnSuccess) {
			s->dma->clearMemoryDescriptor();
			return r;
		}
		UInt64 offset = 0;
		uint16_t flags = kNDVirtqDescNext | (type == kNDVirtioBlkTIn ? kNDVirtqDescWrite : 0);
		while (offset < bytes) {
			IODMACommand::Segment64 seg[8];
			UInt32 count = 8;
			r = s->dma->gen64IOVMSegments(&offset, seg, &count);
			if (r != kIOReturnSuccess || count == 0) {
				break;
			}
			for (UInt32 i = 0; i < count; i++) {
				if (n > segments) {
					r = kIOReturnNoResources;
					break;
				}
				desc[first + n].addr = seg[i].fIOVMAddr;
				desc[first + n].len = (uint32_t)seg[i].fLength;
				desc[first + n].flags = flags;
				n++;
			}
			if (r != kIOReturnSuccess) {
				break;
			}
		}
		if (r != kIOReturnSuccess || offset < bytes) {
			IOLog("NeoDarwinVirtioBlock: %s: %llu bytes do not fit %u segments (0x%x)\n", where, bytes, segments, r);
			s->dma->complete();
			s->dma->clearMemoryDescriptor();
			return r != kIOReturnSuccess ? r : kIOReturnNoResources;
		}
	}
	desc[first + n].addr = headersPA + slot * kHeaderStride + 16;
	desc[first + n].len = 1;
	s->buffer = buffer;
	s->bytes = bytes;
	post(slot, n + 1);
	return kIOReturnSuccess;
}

IOReturn
NeoDarwinVirtioBlock::submit(IOMemoryDescriptor *buffer, UInt64 block, UInt64 nblks, IOStorageCompletion *completion)
{
	uint64_t bytes = nblks * blockSize;
	int32_t slot = freeSlot();
	if (slot < 0) {
		Pending *p = IOMallocType(Pending);
		p->buffer = buffer;
		p->block = block;
		p->nblks = nblks;
		p->completion = *completion;
		buffer->retain();
		enqueue_tail(&pending, &p->link);
		pendingCount++;
		return kIOReturnSuccess;
	}
	Slot *s = &slots[slot];
	s->state = kAsync;
	s->completion = *completion;
	uint32_t type = buffer->getDirection() == kIODirectionIn ? kNDVirtioBlkTIn : kNDVirtioBlkTOut;
	buffer->retain();
	IOReturn r = issue((uint32_t)slot, type, block * (blockSize / kNDVirtioBlkSectorSize), buffer, bytes);
	if (r != kIOReturnSuccess) {
		s->state = kFree;
		buffer->release();
		IOStorage::complete(completion, r, 0);
	}
	return kIOReturnSuccess;
}

IOReturn
NeoDarwinVirtioBlock::submitAction(OSObject *owner, void *arg0, void *arg1, void *arg2, void *arg3)
{
	NeoDarwinVirtioBlock *self = static_cast<NeoDarwinVirtioBlock *>(owner);
	return self->submit((IOMemoryDescriptor *)arg0, *(UInt64 *)arg1, *(UInt64 *)arg2, (IOStorageCompletion *)arg3);
}

IOReturn
NeoDarwinVirtioBlock::doAsyncReadWrite(IOMemoryDescriptor *buffer, UInt64 block, UInt64 nblks,
    IOStorageAttributes *attributes, IOStorageCompletion *completion)
{
	(void)attributes;
	if (!running) {
		return kIOReturnNotReady;
	}
	if (block + nblks < block || (block + nblks) * blockSize > capacity * kNDVirtioBlkSectorSize) {
		return kIOReturnBadArgument;
	}
	if (buffer->getDirection() != kIODirectionIn && readOnly) {
		return kIOReturnNotWritable;
	}
	return gate->runAction(&NeoDarwinVirtioBlock::submitAction, buffer, &block, &nblks, completion);
}

// A cache flush, waited for: a slot with a header and a status only.
IOReturn
NeoDarwinVirtioBlock::synchronizeAction(OSObject *owner, void *, void *, void *, void *)
{
	NeoDarwinVirtioBlock *self = static_cast<NeoDarwinVirtioBlock *>(owner);
	int32_t slot;
	while ((slot = self->freeSlot()) < 0) {
		self->gate->commandSleep(&self->slots, THREAD_UNINT);
	}
	Slot *s = &self->slots[slot];
	s->state = kSync;
	IOReturn r = self->issue((uint32_t)slot, kNDVirtioBlkTFlush, 0, NULL, 0);
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
	self->gate->commandWakeup(&self->slots);
	return r;
}

IOReturn
NeoDarwinVirtioBlock::doSynchronize(UInt64 block, UInt64 nblks, IOStorageSynchronizeOptions options)
{
	(void)block; (void)nblks; (void)options;
	if (!running) {
		return kIOReturnNotReady;
	}
	if ((features & kNDVirtioBlkFFlush) == 0) {
		return kIOReturnSuccess;        // no volatile cache to flush
	}
	return gate->runAction(&NeoDarwinVirtioBlock::synchronizeAction);
}

// Starts waiting requests while there are free slots.
void
NeoDarwinVirtioBlock::startPending()
{
	while (pendingCount != 0 && freeSlot() >= 0) {
		Pending *p = (Pending *)dequeue_head(&pending);
		pendingCount--;
		submit(p->buffer, p->block, p->nblks, &p->completion);
		p->buffer->release();           // submit took its own reference
		IOFreeType(p, Pending);
	}
}

// MARK: - Completions

bool
NeoDarwinVirtioBlock::filter(OSObject *owner, IOFilterInterruptEventSource *source)
{
	(void)source;
	NeoDarwinVirtioBlock *self = static_cast<NeoDarwinVirtioBlock *>(owner);
	uint8_t bits = self->rd8(self->isr, 0);     // reading acknowledges
	if (bits == 0) {
		return false;                           // another device's
	}
	__atomic_or_fetch(&self->isrBits, bits, __ATOMIC_RELAXED);
	return true;
}

void
NeoDarwinVirtioBlock::configInterrupt(OSObject *owner, IOInterruptEventSource *source, int count)
{
	(void)source; (void)count;
	NeoDarwinVirtioBlock *self = static_cast<NeoDarwinVirtioBlock *>(owner);
	IOLog("NeoDarwinVirtioBlock: %s: configuration changed (status 0x%x)\n", self->where,
	    self->rd8(self->common, kNDVirtioDeviceStatus));
}

void
NeoDarwinVirtioBlock::queueInterrupt(OSObject *owner, IOInterruptEventSource *source, int count)
{
	(void)source; (void)count;
	NeoDarwinVirtioBlock *self = static_cast<NeoDarwinVirtioBlock *>(owner);
	if (self->vectors == 0) {
		uint32_t bits = __atomic_exchange_n(&self->isrBits, 0, __ATOMIC_RELAXED);
		if (bits & kNDVirtioISRConfig) {
			configInterrupt(owner, source, count);
		}
	}
	self->reap();
}

// On the work loop: every chain the device has put on the used ring.
void
NeoDarwinVirtioBlock::reap()
{
	for (;;) {
		volatile uint16_t *idx = (volatile uint16_t *)(used + kNDVirtqRingIdx);
		dmaPolicy.fromDevice(idx, 2);
		uint16_t deviceIdx = *idx;
		if (deviceIdx == usedIdx) {
			break;
		}
		while (usedIdx != deviceIdx) {
			volatile uint8_t *elem = used + kNDVirtqRingEntries + kNDVirtqUsedElemSize * (usedIdx % queueSize);
			dmaPolicy.fromDevice(elem, kNDVirtqUsedElemSize);
			uint32_t head = *(volatile uint32_t *)elem;
			usedIdx++;
			uint32_t slot = head / kChain;
			if (head % kChain != 0 || slot >= slotCount || slots[slot].state == kFree) {
				IOLog("NeoDarwinVirtioBlock: %s: the device returned descriptor %u, which is not a request\n", where, head);
				continue;
			}
			Slot *s = &slots[slot];
			uint8_t *status = headerBytes + slot * kHeaderStride + 16;
			dmaPolicy.fromDevice(status, 1);
			IOReturn result = *status == kNDVirtioBlkSOK ? kIOReturnSuccess :
			    (*status == kNDVirtioBlkSUnsupp ? kIOReturnUnsupported : kIOReturnIOError);
			if (s->buffer != NULL) {
				s->dma->complete();
				s->dma->clearMemoryDescriptor();
			}
			completed++;
			if (s->state == kSync) {
				s->result = result;
				s->state = kSyncDone;
				gate->commandWakeup(s);
				continue;
			}
			IOStorageCompletion completion = s->completion;
			IOMemoryDescriptor *buffer = s->buffer;
			uint64_t bytes = result == kIOReturnSuccess ? s->bytes : 0;
			if (result != kIOReturnSuccess) {
				IOLog("NeoDarwinVirtioBlock: %s: request failed with status %u\n", where, *status);
			}
			s->buffer = NULL;
			s->state = kFree;
			buffer->release();
			IOStorage::complete(&completion, result, bytes);
		}
	}
	startPending();
	gate->commandWakeup(&slots);
}

// MARK: - What the device is

IOReturn
NeoDarwinVirtioBlock::doEjectMedia(void)
{
	return kIOReturnUnsupported;
}

IOReturn
NeoDarwinVirtioBlock::doFormatMedia(UInt64 byteCapacity)
{
	(void)byteCapacity;
	return kIOReturnUnsupported;
}

UInt32
NeoDarwinVirtioBlock::doGetFormatCapacities(UInt64 *capacities, UInt32 capacitiesMaxCount) const
{
	if (capacities != NULL && capacitiesMaxCount > 0) {
		capacities[0] = capacity * kNDVirtioBlkSectorSize;
	}
	return 1;
}

char *
NeoDarwinVirtioBlock::getVendorString(void)
{
	return (char *)"VirtIO";
}

char *
NeoDarwinVirtioBlock::getProductString(void)
{
	return (char *)"Block Device";
}

char *
NeoDarwinVirtioBlock::getRevisionString(void)
{
	return (char *)"1.0";
}

char *
NeoDarwinVirtioBlock::getAdditionalDeviceInfoString(void)
{
	return where;
}

IOReturn
NeoDarwinVirtioBlock::reportBlockSize(UInt64 *size)
{
	*size = blockSize;
	return kIOReturnSuccess;
}

IOReturn
NeoDarwinVirtioBlock::reportEjectability(bool *isEjectable)
{
	*isEjectable = false;
	return kIOReturnSuccess;
}

IOReturn
NeoDarwinVirtioBlock::reportMaxValidBlock(UInt64 *maxBlock)
{
	uint64_t blocks = capacity * kNDVirtioBlkSectorSize / blockSize;
	*maxBlock = blocks != 0 ? blocks - 1 : 0;
	return kIOReturnSuccess;
}

IOReturn
NeoDarwinVirtioBlock::reportMediaState(bool *mediaPresent, bool *changedState)
{
	*mediaPresent = capacity != 0;
	if (changedState != NULL) {
		*changedState = false;
	}
	return kIOReturnSuccess;
}

IOReturn
NeoDarwinVirtioBlock::reportRemovability(bool *isRemovable)
{
	*isRemovable = false;
	return kIOReturnSuccess;
}

IOReturn
NeoDarwinVirtioBlock::reportWriteProtection(bool *isWriteProtected)
{
	*isWriteProtected = readOnly;
	return kIOReturnSuccess;
}

// A volatile write cache exists when the device takes flushes; with
// CONFIG_WCE its mode is readable and writable.
IOReturn
NeoDarwinVirtioBlock::getWriteCacheState(bool *enabled)
{
	if ((features & kNDVirtioBlkFFlush) == 0) {
		*enabled = false;
	} else if (features & kNDVirtioBlkFConfigWCE) {
		*enabled = rd8(config, kNDVirtioBlkWriteback) != 0;
	} else {
		*enabled = true;
	}
	return kIOReturnSuccess;
}

IOReturn
NeoDarwinVirtioBlock::setWriteCacheState(bool enabled)
{
	if ((features & kNDVirtioBlkFConfigWCE) == 0) {
		return kIOReturnUnsupported;
	}
	wr8(config, kNDVirtioBlkWriteback, enabled ? 1 : 0);
	return kIOReturnSuccess;
}
