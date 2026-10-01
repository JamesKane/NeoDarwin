// SPDX-License-Identifier: BSD-2-Clause
// NeoDarwin-Language: expressibility: IOKit C++ helpers (IOPCIDevice, IOMemoryMap, IOBufferMemoryDescriptor) for the kernel's virtio drivers.
//
// What every NeoDarwin virtio driver on PCI shares (docs/kernel/storage.md,
// docs/kernel/network.md): the modern PCI transport (virtio 1.2 §4.1.4) and
// split virtqueues (§2.7). NeoDarwinVirtioBlock and NeoDarwinVirtioNet each
// own one NDVirtioPCI and one NDVirtqueue per queue; what goes in a
// queue's descriptors is the driver's business.
//
//   NDVirtioPCI   the vendor capabilities found and their BARs mapped;
//                 reset, status, feature negotiation, the configuration
//                 generation, queue set-up and MSI-X vectors
//   NDVirtqueue   one split ring in physically contiguous memory below
//                 dma-address-bits: descriptors and the available ring on
//                 the first pages, the used ring (the device writes it) on
//                 a page of its own; publishing heads and reading the used
//                 ring, with the barriers or cache maintenance
//                 NeoDarwinStorageDMA.h's policy calls for
//
// Only the modern interface: a transitional device's legacy I/O BAR is not
// used, and a device without VIRTIO_F_VERSION_1 is refused. Header-only,
// every function inline: each driver's object file has its own copy.
#ifndef ND_VIRTIO_PCI_H
#define ND_VIRTIO_PCI_H

#include <IOKit/IOLib.h>
#include <IOKit/pci/IOPCIDevice.h>

#include "nd_virtio.h"
#include "NeoDarwinStorageDMA.h"

struct NDVirtioPCI {
	IOPCIDevice *pci = NULL;
	const char *driver = "";        // the driver's class name, for log lines
	char where[16] = {};            // bus:device.function
	IOMemoryMap *bars[6] = {};
	volatile uint8_t *common = NULL, *notify = NULL, *isr = NULL, *config = NULL;
	uint32_t notifyMultiplier = 0;
	uint32_t configLength = 0;
	uint32_t vectors = 0;           // MSI-X vectors granted; 0: INTx

	static uint8_t rd8(volatile uint8_t *base, uint32_t off) { return *(volatile uint8_t *)(base + off); }
	static uint16_t rd16(volatile uint8_t *base, uint32_t off) { return *(volatile uint16_t *)(base + off); }
	static uint32_t rd32(volatile uint8_t *base, uint32_t off) { return *(volatile uint32_t *)(base + off); }
	static void wr8(volatile uint8_t *base, uint32_t off, uint8_t v) { *(volatile uint8_t *)(base + off) = v; }
	static void wr16(volatile uint8_t *base, uint32_t off, uint16_t v) { *(volatile uint16_t *)(base + off) = v; }
	static void wr32(volatile uint8_t *base, uint32_t off, uint32_t v) { *(volatile uint32_t *)(base + off) = v; }
	static void
	wr64(volatile uint8_t *base, uint32_t off, uint64_t v)
	{
		wr32(base, off, (uint32_t)v);
		wr32(base, off + 4, (uint32_t)(v >> 32));
	}

	uint16_t deviceID() const { return pci->configRead16(kIOPCIConfigDeviceID); }

	// A line in the driver's log, then FAILED in the device status.
	void
	fail(const char *why)
	{
		IOLog("%s: %s: %s\n", driver, where, why);
		if (common != NULL) {
			wr8(common, kNDVirtioDeviceStatus, rd8(common, kNDVirtioDeviceStatus) | kNDVirtioStatusFailed);
		}
	}

	volatile uint8_t *
	mapCapability(uint8_t bar, uint32_t offset, uint32_t length)
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

	// Memory and bus mastering on, then the vendor-specific capabilities
	// (ID 0x09) of the modern interface; the first of each type the driver
	// can use is the one to use. False, with a log line, for a device
	// without them (legacy only). configMin: the device configuration's
	// smallest useful length.
	bool
	attach(IOPCIDevice *device, const char *driverName, uint32_t configMin)
	{
		pci = device;
		driver = driverName;
		snprintf(where, sizeof(where), "%02x:%02x.%x", pci->getBusNumber(), pci->getDeviceNumber(), pci->getFunctionNumber());
		pci->setMemoryEnable(true);
		pci->setBusLeadEnable(true);
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
				if (config == NULL && len >= configMin) {
					config = mapCapability(bar, off, len);
					configLength = len;
				}
				break;
			default:
				break;
			}
		}
		if (common == NULL || notify == NULL || isr == NULL || config == NULL) {
			IOLog("%s: %s: 1af4:%04x has no modern (virtio 1.x) PCI capabilities; not supported\n", driver, where, deviceID());
			return false;
		}
		return true;
	}

	void
	unmap()
	{
		for (int b = 0; b < 6; b++) {
			OSSafeReleaseNULL(bars[b]);
		}
		common = notify = isr = config = NULL;
	}

	// Status 0, polled until the device reads it back (§4.1.4.3.2).
	bool
	reset()
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

	// §3.1.1 steps 1-3: reset, ACKNOWLEDGE, DRIVER.
	bool
	begin()
	{
		if (!reset()) {
			fail("the device does not reset");
			return false;
		}
		wr8(common, kNDVirtioDeviceStatus, kNDVirtioStatusAcknowledge);
		wr8(common, kNDVirtioDeviceStatus, kNDVirtioStatusAcknowledge | kNDVirtioStatusDriver);
		return true;
	}

	uint64_t
	offeredFeatures()
	{
		wr32(common, kNDVirtioDeviceFeatureSelect, 0);
		uint64_t offered = rd32(common, kNDVirtioDeviceFeature);
		wr32(common, kNDVirtioDeviceFeatureSelect, 1);
		return offered | (uint64_t)rd32(common, kNDVirtioDeviceFeature) << 32;
	}

	// §3.1.1 steps 4-6: what the device offers of `wanted`, written back,
	// and FEATURES_OK read back. VERSION_1 is required; ACCESS_PLATFORM and
	// ORDER_PLATFORM are always taken when offered (device addresses are
	// physical: there is no IOMMU driver; the barriers are the platform's).
	bool
	negotiate(uint64_t wanted, uint64_t *features)
	{
		uint64_t offered = offeredFeatures();
		if ((offered & kNDVirtioFVersion1) == 0) {
			fail("the device offers no VIRTIO_F_VERSION_1 (legacy only); not supported");
			return false;
		}
		wanted |= kNDVirtioFVersion1 | kNDVirtioFAccessPlatform | kNDVirtioFOrderPlatform;
		*features = offered & wanted;
		wr32(common, kNDVirtioDriverFeatureSelect, 0);
		wr32(common, kNDVirtioDriverFeature, (uint32_t)*features);
		wr32(common, kNDVirtioDriverFeatureSelect, 1);
		wr32(common, kNDVirtioDriverFeature, (uint32_t)(*features >> 32));
		wr8(common, kNDVirtioDeviceStatus, rd8(common, kNDVirtioDeviceStatus) | kNDVirtioStatusFeaturesOK);
		if ((rd8(common, kNDVirtioDeviceStatus) & kNDVirtioStatusFeaturesOK) == 0) {
			fail("the device did not accept the features (FEATURES_OK)");
			return false;
		}
		return true;
	}

	// The configuration generation: read before and after the device
	// configuration, equal when what was read in between is consistent.
	uint8_t generation() { return rd8(common, kNDVirtioConfigGeneration); }

	// The largest queue `queue` can have, 0 if it doesn't exist.
	uint16_t
	queueMax(uint16_t queue)
	{
		if (queue >= rd16(common, kNDVirtioNumQueues)) {
			return 0;
		}
		wr16(common, kNDVirtioQueueSelect, queue);
		return rd16(common, kNDVirtioQueueSize);
	}

	// §4.1.5.1.3: the queue's size and rings; returns its notification
	// address.
	volatile uint8_t *
	setQueue(uint16_t queue, uint16_t size, uint64_t descPA, uint64_t availPA, uint64_t usedPA)
	{
		wr16(common, kNDVirtioQueueSelect, queue);
		wr16(common, kNDVirtioQueueSize, size);
		wr64(common, kNDVirtioQueueDesc, descPA);
		wr64(common, kNDVirtioQueueDriver, availPA);
		wr64(common, kNDVirtioQueueDevice, usedPA);
		return notify + rd16(common, kNDVirtioQueueNotifyOff) * notifyMultiplier;
	}

	void
	enableQueue(uint16_t queue)
	{
		wr16(common, kNDVirtioQueueSelect, queue);
		wr16(common, kNDVirtioQueueEnable, 1);
	}

	// §3.1.1 step 8; false (with a log line) if the device then wants a
	// reset or has failed.
	bool
	driverOK()
	{
		wr8(common, kNDVirtioDeviceStatus, rd8(common, kNDVirtioDeviceStatus) | kNDVirtioStatusDriverOK);
		if (rd8(common, kNDVirtioDeviceStatus) & (kNDVirtioStatusNeedsReset | kNDVirtioStatusFailed)) {
			fail("the device failed after DRIVER_OK");
			return false;
		}
		return true;
	}

	// MSI-X, asked for before anything resolves the device's interrupts
	// (docs/kernel/gic-its.md, "For P1-10"): up to `wanted` vectors, at
	// least one. The vectors granted are counted; 0 means INTx on source 0
	// (nd_pci_msi=0, no ITS). Vector n is interrupt source n.
	uint32_t
	requestMSIX(uint32_t wanted)
	{
		vectors = 0;
		IOReturn ret = pci->configureInterrupts(kIOInterruptTypePCIMessagedX, 1, wanted, 0);
		if (ret != kIOReturnSuccess) {
			return 0;
		}
		for (uint32_t v = 0; v < wanted; v++) {
			int type = 0;
			if (pci->getInterruptType((int)v, &type) != kIOReturnSuccess || (type & kIOInterruptTypePCIMessagedX) == 0) {
				break;
			}
			vectors = v + 1;
		}
		return vectors;
	}

	// The configuration change vector, and each queue's, written once the
	// interrupt sources are registered (which enables MSI-X) and read back.
	// With INTx every one is kNDVirtioNoVector.
	bool
	setConfigVector(uint16_t vector)
	{
		wr16(common, kNDVirtioConfigMSIXVector, vector);
		if (vector != kNDVirtioNoVector && rd16(common, kNDVirtioConfigMSIXVector) != vector) {
			fail("the device refused its configuration MSI-X vector");
			return false;
		}
		return true;
	}

	bool
	setQueueVector(uint16_t queue, uint16_t vector)
	{
		wr16(common, kNDVirtioQueueSelect, queue);
		wr16(common, kNDVirtioQueueMSIXVector, vector);
		if (vector != kNDVirtioNoVector && rd16(common, kNDVirtioQueueMSIXVector) != vector) {
			fail("the device refused a queue's MSI-X vector");
			return false;
		}
		return true;
	}

	// INTx: reading the ISR status acknowledges it and deasserts the line.
	uint8_t readISR() { return rd8(isr, 0); }

	// "MSI-X 2 vectors (LPIs 8192-8193)", "MSI-X 1 vector (LPI 8192)" or
	// "INTx", for the driver's log line.
	void
	describeInterrupts(char *buffer, size_t size)
	{
		if (vectors == 0) {
			snprintf(buffer, size, "INTx");
			return;
		}
		OSNumber *lpi = OSDynamicCast(OSNumber, pci->getProperty("msi-lpi-base"));
		uint32_t base = lpi != NULL ? lpi->unsigned32BitValue() : 0;
		if (vectors == 1) {
			snprintf(buffer, size, "MSI-X 1 vector (LPI %u)", base);
		} else {
			snprintf(buffer, size, "MSI-X %u vectors (LPIs %u-%u)", vectors, base, base + vectors - 1);
		}
	}
};

// One split virtqueue (§2.7). The driver fills descriptors itself (desc[],
// then dma.toDevice over what it wrote), makes chains available with
// push() and publish(), and reads completions with nextUsed().
struct NDVirtqueue {
	static const uint32_t kMaxSize = 256;

	uint16_t index = 0;
	uint32_t size = 0;              // a power of two
	NDStorageDMA dma;
	IOBufferMemoryDescriptor *ring = NULL;
	struct nd_virtq_desc *desc = NULL;
	volatile uint8_t *avail = NULL, *used = NULL;
	volatile uint8_t *notifyAt = NULL;
	uint16_t availIdx = 0, usedIdx = 0, deviceIdx = 0;

	// The queue's memory, at most `limit` entries (and the device's
	// maximum), rounded down to a power of two, and its registration with
	// the device. False with a log line on failure.
	bool
	setUp(NDVirtioPCI &transport, uint16_t queue, const NDStorageDMA &policy, uint32_t limit, uint32_t minimum)
	{
		index = queue;
		dma = policy;
		uint32_t max = transport.queueMax(queue);
		if (max > limit) {
			max = limit;
		}
		if (max > kMaxSize) {
			max = kMaxSize;
		}
		size = 1;
		while (size * 2 <= max) {
			size *= 2;
		}
		if (max == 0 || size < minimum) {
			transport.fail("a queue is missing or too short");
			return false;
		}
		// Descriptors (16 bytes each) and the available ring on the first
		// pages; the used ring, which the device writes, on its own page.
		size_t driverBytes = 16 * size + kNDVirtqRingEntries + 2 * size + 2;
		size_t usedOffset = (driverBytes + PAGE_SIZE - 1) & ~(size_t)(PAGE_SIZE - 1);
		size_t usedBytes = kNDVirtqRingEntries + kNDVirtqUsedElemSize * size + 2;
		ring = dma.allocate(usedOffset + ((usedBytes + PAGE_SIZE - 1) & ~(size_t)(PAGE_SIZE - 1)));
		if (ring == NULL) {
			transport.fail("no memory for a queue");
			return false;
		}
		uint8_t *base = (uint8_t *)ring->getBytesNoCopy();
		desc = (struct nd_virtq_desc *)base;
		avail = base + 16 * size;
		used = base + usedOffset;
		uint64_t pa = NDStorageDMA::physical(ring);
		notifyAt = transport.setQueue(queue, (uint16_t)size, pa, pa + 16 * size, pa + usedOffset);
		return true;
	}

	void
	release()
	{
		if (ring != NULL) {
			ring->complete();
			OSSafeReleaseNULL(ring);
		}
		desc = NULL;
	}

	// Descriptors [first, first + count) written: in memory for the device.
	void
	flushDescriptors(uint32_t first, uint32_t count)
	{
		dma.toDevice(&desc[first], sizeof(desc[0]) * count);
	}

	// Asks the device not to interrupt when it uses buffers (a hint, §2.7.7).
	void
	setNoInterrupt(bool quiet)
	{
		*(volatile uint16_t *)(avail + kNDVirtqRingFlags) = quiet ? kNDVirtqAvailNoInterrupt : 0;
		dma.toDevice(avail + kNDVirtqRingFlags, 2);
	}

	// A chain's head in the next available entry; the device sees it at
	// the next publish().
	void
	push(uint16_t head)
	{
		volatile uint16_t *entry = (volatile uint16_t *)(avail + kNDVirtqRingEntries + 2 * (availIdx % size));
		*entry = head;
		dma.toDevice(entry, 2);
		availIdx++;
	}

	// The available index, then the notification (§2.7.13): descriptors
	// and entries in memory before the index, the index before the
	// doorbell.
	void
	publish()
	{
		*(volatile uint16_t *)(avail + kNDVirtqRingIdx) = availIdx;
		dma.toDevice(avail + kNDVirtqRingIdx, 2);
		__builtin_arm_dsb(0xf);                 // DSB SY
		*(volatile uint16_t *)notifyAt = index;
	}

	// The next used element: its chain's head and the bytes the device
	// wrote. False when the device has used nothing more.
	bool
	nextUsed(uint32_t *head, uint32_t *written)
	{
		if (usedIdx == deviceIdx) {
			volatile uint16_t *idx = (volatile uint16_t *)(used + kNDVirtqRingIdx);
			dma.fromDevice(idx, 2);
			deviceIdx = *idx;
			if (usedIdx == deviceIdx) {
				return false;
			}
		}
		volatile uint8_t *elem = used + kNDVirtqRingEntries + kNDVirtqUsedElemSize * (usedIdx % size);
		dma.fromDevice(elem, kNDVirtqUsedElemSize);
		*head = *(volatile uint32_t *)elem;
		*written = *(volatile uint32_t *)(elem + 4);
		usedIdx++;
		return true;
	}
};

#endif
