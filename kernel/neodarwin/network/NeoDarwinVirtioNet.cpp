// SPDX-License-Identifier: BSD-2-Clause
// NeoDarwin-Language: expressibility: IOKit drivers are C++ IOService subclasses (IOEthernetController).
//
// A virtio 1.x network device on PCI (docs/kernel/network.md), as an
// IONetworkingFamily Ethernet controller: IOEthernetController attaches an
// IOEthernetInterface, IONetworkStack names it (en0, en1, ... through
// NeoDarwinInterfaceNamer) and attaches it to the BSD stack with
// ifnet_allocate_extended.
//
//   IOPCIDevice 1af4:1041 (modern) or 1af4:1000 (transitional)
//     NeoDarwinVirtioNet (IOEthernetController)
//       IOEthernetInterface   en0
//
// A bring-up driver, compiled into the kernel (patch 0035) until P3-08's
// network dexts replace it, as P1-18's xHCI driver is replaced by P3-07.
//
// Transport and rings: NeoDarwinVirtioPCI.h (kernel/neodarwin/virtio), the
// modern PCI interface shared with NeoDarwinVirtioBlock. One receive queue
// and one transmit queue (no VIRTIO_NET_F_MQ), no control queue.
//
// Features: VERSION_1, MAC, STATUS (link state) and SPEED_DUPLEX when
// offered. No offloads (CSUM, GUEST_CSUM, TSO), no MRG_RXBUF: every
// receive buffer holds a whole frame, and the stack computes checksums.
//
// Receive: 2-descriptor chains, the 12-byte virtio_net_hdr in driver
// memory (its own cache line per buffer) and the frame straight into an
// mbuf cluster, 2 bytes in (so the IP header that follows the 14-byte
// Ethernet header is 4-byte aligned). Completions are handed to the
// interface in a batch (kInputOptionQueuePacket, then flushInputQueue) and
// each buffer is replaced with a fresh mbuf; when none can be had the
// packet is dropped and its mbuf goes back on the ring, so the ring never
// runs dry.
//
// Transmit: an IOGatedOutputQueue calls outputPacket on the work loop.
// Fixed chains of kTxChain descriptors: a header (all zeroes: no offload)
// and up to kTxChain - 1 segments of the mbuf chain, from an
// IOMbufNaturalMemoryCursor, which coalesces longer chains. A packet whose
// memory the device can't reach (above dma-address-bits, or outside the
// mbuf map) is copied into the slot's bounce buffer. A full ring stalls the
// queue until completions free a slot.
//
// DMA (NeoDarwinStorageDMA.h): rings, headers and bounce buffers below
// dma-address-bits, physically contiguous; mbuf data cleaned before the
// device reads it and cleaned and invalidated around what it writes when
// the device isn't dma-coherent.
//
// Interrupts: three MSI-X vectors (configuration changes, receive,
// transmit), or two (configuration; both queues), or one; without MSIs
// (nd_pci_msi=0, no ITS) INTx, level and shared, through a filter that
// reads (and so acknowledges) the ISR status.
//
// Filters: without VIRTIO_NET_F_CTRL_RX the device delivers every frame
// (QEMU's model starts promiscuous), so promiscuous and multicast modes
// and the multicast list are accepted and need nothing. No kernel debugger
// (KDP over Ethernet): IOKernelDebugger is built, but this driver attaches
// no debugger client.
//
// Boot-arg nd_virtio_net=0: the driver doesn't attach.

#include <IOKit/IOBSD.h>
#include <IOKit/IOLib.h>
#include <IOKit/IOWorkLoop.h>
#include <IOKit/IOInterruptEventSource.h>
#include <IOKit/IOFilterInterruptEventSource.h>
#include <IOKit/pci/IOPCIDevice.h>
#include <IOKit/network/IOEthernetController.h>
#include <IOKit/network/IOEthernetInterface.h>
#include <IOKit/network/IOGatedOutputQueue.h>
#include <IOKit/network/IOMbufMemoryCursor.h>
#include <IOKit/network/IONetworkMedium.h>
#include <IOKit/network/IONetworkStats.h>
#include <libkern/c++/OSSerialize.h>
#include <pexpert/pexpert.h>
#include <sys/kpi_mbuf.h>

#include "nd_virtio_net.h"
#include "NeoDarwinStorageDMA.h"
#include "NeoDarwinVirtioPCI.h"

class NeoDarwinVirtioNet : public IOEthernetController
{
	OSDeclareDefaultStructors(NeoDarwinVirtioNet);

public:
	virtual bool start(IOService *provider) APPLE_KEXT_OVERRIDE;
	virtual void stop(IOService *provider) APPLE_KEXT_OVERRIDE;
	virtual void free() APPLE_KEXT_OVERRIDE;
	virtual bool serializeProperties(OSSerialize *s) const APPLE_KEXT_OVERRIDE;

	virtual bool createWorkLoop() APPLE_KEXT_OVERRIDE;
	virtual IOWorkLoop *getWorkLoop() const APPLE_KEXT_OVERRIDE;
	virtual IOOutputQueue *createOutputQueue() APPLE_KEXT_OVERRIDE;
	virtual bool configureInterface(IONetworkInterface *interface) APPLE_KEXT_OVERRIDE;
	virtual IOReturn enable(IONetworkInterface *interface) APPLE_KEXT_OVERRIDE;
	virtual IOReturn disable(IONetworkInterface *interface) APPLE_KEXT_OVERRIDE;
	virtual UInt32 outputPacket(mbuf_t m, void *param) APPLE_KEXT_OVERRIDE;
	virtual IOReturn getHardwareAddress(IOEthernetAddress *address) APPLE_KEXT_OVERRIDE;
	virtual IOReturn setPromiscuousMode(bool active) APPLE_KEXT_OVERRIDE;
	virtual IOReturn setMulticastMode(bool active) APPLE_KEXT_OVERRIDE;
	virtual IOReturn setMulticastList(IOEthernetAddress *addresses, UInt32 count) APPLE_KEXT_OVERRIDE;
	virtual const OSString *newVendorString() const APPLE_KEXT_OVERRIDE;
	virtual const OSString *newModelString() const APPLE_KEXT_OVERRIDE;
	virtual IOReturn selectMedium(const IONetworkMedium *medium) APPLE_KEXT_OVERRIDE;

private:
	static const uint32_t kRxMaxSlots = 128;        // 2 descriptors each
	static const uint32_t kTxChain = 8;             // header + 7 segments
	static const uint32_t kTxMaxSlots = 32;
	static const uint32_t kHeaderStride = 64;       // a header's own cache line
	static const uint32_t kBufferBytes = 2048;      // an mbuf cluster, a bounce buffer
	static const uint32_t kRxAlign = 2;             // ETHER_ALIGN
	static const uint32_t kTxQueueCapacity = 256;

	struct RxSlot {
		mbuf_t m;
		bool bounced;                   // the device writes the slot's bounce buffer
	};
	struct TxSlot {
		mbuf_t m;                       // NULL when free, or when the packet was copied
		bool busy;
	};

	bool negotiate();
	void readConfiguration();
	bool setUpQueues();
	bool setUpInterrupts();
	bool attachRxBuffer(uint32_t slot, mbuf_t m);
	mbuf_t newRxPacket();
	void serviceRx();
	void serviceTx();
	void configChanged(bool log);
	void reportLink(bool log);
	void firstInterrupt(const char *queue, int vector);
	void releaseBuffers();
	void removeSources();

	static bool filter(OSObject *owner, IOFilterInterruptEventSource *source);
	static void interrupt(OSObject *owner, IOInterruptEventSource *source, int count);

	uint64_t reach() const
	{
		return dmaPolicy.addressBits >= 64 ? ~0ULL : (1ULL << dmaPolicy.addressBits) - 1;
	}

	IOPCIDevice *pci = NULL;
	NDVirtioPCI transport;
	const char *where = transport.where;
	NDStorageDMA dmaPolicy;
	uint64_t features = 0;
	IOEthernetAddress mac = {};
	bool linkUp = true;
	uint32_t speedMbps = 0;

	IOWorkLoop *workLoop = NULL;
	IOOutputQueue *transmitQueue = NULL;
	IOEthernetInterface *netif = NULL;
	IONetworkStats *netStats = NULL;
	IOMbufNaturalMemoryCursor *cursor = NULL;
	OSDictionary *media = NULL;
	IOInterruptEventSource *configSource = NULL, *rxSource = NULL, *txSource = NULL;
	volatile uint32_t isrBits = 0;  // INTx: what the filter read
	bool enabled = false;
	bool running = false;

	NDVirtqueue rxq, txq;
	IOBufferMemoryDescriptor *rxHeaders = NULL, *txHeaders = NULL;
	IOBufferMemoryDescriptor *rxBounce = NULL, *txBounce = NULL;   // kBufferBytes per slot
	RxSlot rx[kRxMaxSlots] = {};
	TxSlot tx[kTxMaxSlots] = {};
	uint32_t rxSlots = 0, txSlots = 0, txFree = 0;

	// Counters, published as the "Statistics" registry property.
	uint64_t rxPackets = 0, rxBytes = 0, rxDropped = 0, rxBounced = 0, rxInterrupts = 0;
	uint64_t txPackets = 0, txBytes = 0, txDropped = 0, txBounced = 0, txStalls = 0, txInterrupts = 0;
	bool rxSeen = false, txSeen = false;
};

#define super IOEthernetController
OSDefineMetaClassAndStructors(NeoDarwinVirtioNet, IOEthernetController);

// MARK: - Bring-up

bool
NeoDarwinVirtioNet::createWorkLoop()
{
	workLoop = IOWorkLoop::workLoop();
	return workLoop != NULL;
}

IOWorkLoop *
NeoDarwinVirtioNet::getWorkLoop() const
{
	return workLoop;
}

IOOutputQueue *
NeoDarwinVirtioNet::createOutputQueue()
{
	return IOGatedOutputQueue::withTarget(this, getWorkLoop(), kTxQueueCapacity);
}

bool
NeoDarwinVirtioNet::negotiate()
{
	uint64_t wanted = kNDVirtioNetFMAC | kNDVirtioNetFStatus | kNDVirtioNetFSpeedDuplex;
	return transport.negotiate(wanted, &features);
}

// MAC, link state and speed, read until the generation counter is stable.
void
NeoDarwinVirtioNet::readConfiguration()
{
	for (int tries = 0; tries < 16; tries++) {
		uint8_t generation = transport.generation();
		if (features & kNDVirtioNetFMAC) {
			for (int i = 0; i < 6; i++) {
				mac.bytes[i] = NDVirtioPCI::rd8(transport.config, kNDVirtioNetConfigMAC + i);
			}
		}
		uint16_t status = (features & kNDVirtioNetFStatus) ?
		    NDVirtioPCI::rd16(transport.config, kNDVirtioNetConfigStatus) : kNDVirtioNetStatusLinkUp;
		uint32_t speed = kNDVirtioNetSpeedUnknown;
		if ((features & kNDVirtioNetFSpeedDuplex) && transport.configLength >= kNDVirtioNetConfigSpeed + 4) {
			speed = NDVirtioPCI::rd32(transport.config, kNDVirtioNetConfigSpeed);
		}
		if (transport.generation() != generation) {
			continue;
		}
		linkUp = (status & kNDVirtioNetStatusLinkUp) != 0;
		speedMbps = speed == kNDVirtioNetSpeedUnknown ? 0 : speed;
		break;
	}
	if ((features & kNDVirtioNetFMAC) == 0) {
		// No address from the device: a locally administered one, from the
		// clock and the device's place on the bus.
		uint32_t r = (uint32_t)mach_absolute_time() ^ ((uint32_t)pci->getBusNumber() << 8 | pci->getDeviceNumber());
		uint8_t a[6] = { 0x02, 0x4e, 0x44, (uint8_t)(r >> 16), (uint8_t)(r >> 8), (uint8_t)r };
		memcpy(mac.bytes, a, 6);
	}
}

bool
NeoDarwinVirtioNet::setUpQueues()
{
	if (!rxq.setUp(transport, kNDVirtioNetQueueRx, dmaPolicy, 2 * kRxMaxSlots, 2) ||
	    !txq.setUp(transport, kNDVirtioNetQueueTx, dmaPolicy, kTxChain * kTxMaxSlots, kTxChain)) {
		return false;
	}
	rxSlots = rxq.size / 2;
	txSlots = txq.size / kTxChain;
	txFree = txSlots;
	rxHeaders = dmaPolicy.allocate(kHeaderStride * rxSlots);
	txHeaders = dmaPolicy.allocate(kHeaderStride * txSlots);
	txBounce = dmaPolicy.allocate(kBufferBytes * txSlots);
	// Receive buffers are mbuf clusters; a bounce buffer per slot only when
	// some memory may be out of the device's reach.
	if (dmaPolicy.addressBits < 64) {
		rxBounce = dmaPolicy.allocate(kBufferBytes * rxSlots);
	}
	cursor = IOMbufNaturalMemoryCursor::withSpecification(PAGE_SIZE, kTxChain - 1);
	if (rxHeaders == NULL || txHeaders == NULL || txBounce == NULL || cursor == NULL ||
	    (dmaPolicy.addressBits < 64 && rxBounce == NULL)) {
		transport.fail("no memory for the queues");
		return false;
	}
	uint64_t rxHeadersPA = NDStorageDMA::physical(rxHeaders);
	uint64_t txHeadersPA = NDStorageDMA::physical(txHeaders);

	// Receive: header -> frame, both written by the device.
	for (uint32_t s = 0; s < rxSlots; s++) {
		struct nd_virtq_desc *d = &rxq.desc[2 * s];
		d[0].addr = rxHeadersPA + s * kHeaderStride;
		d[0].len = kNDVirtioNetHeaderBytes;
		d[0].flags = kNDVirtqDescNext | kNDVirtqDescWrite;
		d[0].next = (uint16_t)(2 * s + 1);
		d[1].flags = kNDVirtqDescWrite;
		d[1].next = 0;
	}
	// Transmit: header -> segments; the links are fixed, a packet marks
	// where its chain ends.
	for (uint32_t s = 0; s < txSlots; s++) {
		uint32_t first = s * kTxChain;
		for (uint32_t i = 0; i < kTxChain; i++) {
			txq.desc[first + i].next = (uint16_t)(first + i + 1);
			txq.desc[first + i].flags = 0;
		}
		txq.desc[first].addr = txHeadersPA + s * kHeaderStride;
		txq.desc[first].len = kNDVirtioNetHeaderBytes;
		txq.desc[first].flags = kNDVirtqDescNext;
	}
	txq.flushDescriptors(0, txq.size);
	return true;
}

bool
NeoDarwinVirtioNet::setUpInterrupts()
{
	uint32_t vectors = transport.requestMSIX(3);
	if (vectors == 3) {
		configSource = IOInterruptEventSource::interruptEventSource(this, &NeoDarwinVirtioNet::interrupt, pci, 0);
		rxSource = IOInterruptEventSource::interruptEventSource(this, &NeoDarwinVirtioNet::interrupt, pci, 1);
		txSource = IOInterruptEventSource::interruptEventSource(this, &NeoDarwinVirtioNet::interrupt, pci, 2);
	} else if (vectors == 2) {
		configSource = IOInterruptEventSource::interruptEventSource(this, &NeoDarwinVirtioNet::interrupt, pci, 0);
		rxSource = IOInterruptEventSource::interruptEventSource(this, &NeoDarwinVirtioNet::interrupt, pci, 1);
	} else if (vectors == 1) {
		rxSource = IOInterruptEventSource::interruptEventSource(this, &NeoDarwinVirtioNet::interrupt, pci, 0);
	} else {
		// INTx: level and possibly shared; the filter reads the ISR, which
		// also deasserts the line.
		rxSource = IOFilterInterruptEventSource::filterInterruptEventSource(this,
		    &NeoDarwinVirtioNet::interrupt, &NeoDarwinVirtioNet::filter, pci, 0);
	}
	IOInterruptEventSource *sources[3] = { configSource, rxSource, txSource };
	for (IOInterruptEventSource *s : sources) {
		if (s == NULL) {
			continue;
		}
		if (workLoop->addEventSource(s) != kIOReturnSuccess) {
			transport.fail("cannot register the interrupts");
			return false;
		}
		s->enable();
	}
	if (rxSource == NULL || (vectors >= 2 && configSource == NULL) || (vectors == 3 && txSource == NULL)) {
		transport.fail("cannot register the interrupts");
		return false;
	}
	// Vector numbers: configuration 0; receive 1 and transmit 2 (or both
	// 1, or everything 0); none with INTx.
	uint16_t configVector = vectors == 0 ? kNDVirtioNoVector : 0;
	uint16_t rxVector = vectors == 0 ? kNDVirtioNoVector : (vectors >= 2 ? 1 : 0);
	uint16_t txVector = vectors == 0 ? kNDVirtioNoVector : (vectors == 3 ? 2 : rxVector);
	return transport.setConfigVector(configVector) && transport.setQueueVector(kNDVirtioNetQueueRx, rxVector) &&
	       transport.setQueueVector(kNDVirtioNetQueueTx, txVector);
}

bool
NeoDarwinVirtioNet::start(IOService *provider)
{
	uint32_t on = 1;
	if (PE_parse_boot_argn("nd_virtio_net", &on, sizeof(on)) && on == 0) {
		return false;
	}
	pci = OSDynamicCast(IOPCIDevice, provider);
	if (pci == NULL) {
		return false;
	}
	// The work loop and the output queue (IONetworkController::start calls
	// createWorkLoop and createOutputQueue); it waits for IOBSD first.
	if (!super::start(provider)) {
		return false;
	}
	transmitQueue = getOutputQueue();
	if (transmitQueue == NULL) {
		return false;
	}
	transmitQueue->retain();
	dmaPolicy = NDStorageDMA::forDevice(pci);
	if (!transport.attach(pci, "NeoDarwinVirtioNet", kNDVirtioNetConfigMinLength) || !transport.begin() || !negotiate()) {
		return false;
	}
	readConfiguration();
	if (!setUpQueues() || !setUpInterrupts()) {
		return false;
	}
	transport.enableQueue(kNDVirtioNetQueueRx);
	transport.enableQueue(kNDVirtioNetQueueTx);
	if (!transport.driverOK()) {
		return false;
	}
	running = true;

	// The receive ring, full. The device may use buffers only after
	// DRIVER_OK; until the interface is enabled what arrives is dropped.
	uint32_t posted = 0;
	for (uint32_t s = 0; s < rxSlots; s++) {
		mbuf_t m = newRxPacket();
		if (m == NULL || !attachRxBuffer(s, m)) {
			if (m != NULL) {
				freePacket(m);
			}
			break;
		}
		posted++;
	}
	rxq.publish();
	if (posted == 0) {
		transport.fail("no mbufs for the receive ring");
		return false;
	}

	// One medium: Ethernet, autoselected (the speed if the device says).
	media = OSDictionary::withCapacity(1);
	IONetworkMedium *medium = IONetworkMedium::medium(kIOMediumEthernetAuto, (UInt64)speedMbps * 1000000ULL);
	if (media == NULL || medium == NULL || !IONetworkMedium::addMedium(media, medium) || !publishMediumDictionary(media)) {
		OSSafeReleaseNULL(medium);
		transport.fail("cannot publish the medium");
		return false;
	}
	setSelectedMedium(medium);
	medium->release();

	char irq[64];
	transport.describeInterrupts(irq, sizeof(irq));
	IOLog("NeoDarwinVirtioNet: %s: virtio-net 1af4:%04x: MAC %02x:%02x:%02x:%02x:%02x:%02x%s; features 0x%llx; link %s; "
	    "rx queue %u (%u buffers of %u bytes), tx queue %u (%u slots of %u segments); %s; DMA %s, %u address bits\n",
	    where, transport.deviceID(), mac.bytes[0], mac.bytes[1], mac.bytes[2], mac.bytes[3], mac.bytes[4], mac.bytes[5],
	    (features & kNDVirtioNetFMAC) ? "" : " (random)", features, linkUp ? "up" : "down", rxq.size, posted,
	    kBufferBytes - kRxAlign, txq.size, txSlots, kTxChain - 1, irq,
	    dmaPolicy.coherent ? "coherent" : "not coherent", dmaPolicy.addressBits);

	// The interface: IONetworkStack names it and attaches it to BSD.
	if (!attachInterface((IONetworkInterface **)&netif, true)) {
		transport.fail("cannot attach the network interface");
		return false;
	}
	return true;
}

bool
NeoDarwinVirtioNet::configureInterface(IONetworkInterface *interface)
{
	if (!super::configureInterface(interface)) {
		return false;
	}
	IONetworkData *data = interface->getNetworkData(kIONetworkStatsKey);
	if (data == NULL || (netStats = (IONetworkStats *)data->getBuffer()) == NULL) {
		return false;
	}
	return true;
}

void
NeoDarwinVirtioNet::releaseBuffers()
{
	for (uint32_t s = 0; s < kRxMaxSlots; s++) {
		if (rx[s].m != NULL) {
			freePacket(rx[s].m);
			rx[s].m = NULL;
		}
	}
	for (uint32_t s = 0; s < kTxMaxSlots; s++) {
		if (tx[s].m != NULL) {
			freePacket(tx[s].m);
			tx[s].m = NULL;
		}
		tx[s].busy = false;
	}
}

void
NeoDarwinVirtioNet::stop(IOService *provider)
{
	// A reset stops the device's DMA before its buffers go.
	if (transport.common != NULL) {
		transport.reset();
	}
	running = false;
	if (netif != NULL) {
		detachInterface(netif);
		OSSafeReleaseNULL(netif);
	}
	removeSources();
	releaseBuffers();
	super::stop(provider);
}

void
NeoDarwinVirtioNet::removeSources()
{
	IOInterruptEventSource **sources[3] = { &configSource, &rxSource, &txSource };
	for (IOInterruptEventSource **s : sources) {
		if (*s != NULL) {
			(*s)->disable();
			if (workLoop != NULL) {
				workLoop->removeEventSource(*s);
			}
			OSSafeReleaseNULL(*s);
		}
	}
}

void
NeoDarwinVirtioNet::free()
{
	// After a failed start, too: the device stopped, then everything it had.
	if (transport.common != NULL) {
		transport.reset();
	}
	running = false;
	removeSources();
	releaseBuffers();
	rxq.release();
	txq.release();
	IOBufferMemoryDescriptor **memory[4] = { &rxHeaders, &txHeaders, &rxBounce, &txBounce };
	for (IOBufferMemoryDescriptor **m : memory) {
		if (*m != NULL) {
			(*m)->complete();
			OSSafeReleaseNULL(*m);
		}
	}
	transport.unmap();
	OSSafeReleaseNULL(cursor);
	OSSafeReleaseNULL(media);
	OSSafeReleaseNULL(transmitQueue);
	OSSafeReleaseNULL(workLoop);
	super::free();
}

// MARK: - The interface

IOReturn
NeoDarwinVirtioNet::enable(IONetworkInterface *interface)
{
	(void)interface;
	if (!running) {
		return kIOReturnNotReady;
	}
	enabled = true;
	transmitQueue->setCapacity(kTxQueueCapacity);
	transmitQueue->start();
	reportLink(true);
	return kIOReturnSuccess;
}

IOReturn
NeoDarwinVirtioNet::disable(IONetworkInterface *interface)
{
	(void)interface;
	enabled = false;
	transmitQueue->stop();
	transmitQueue->setCapacity(0);
	transmitQueue->flush();
	setLinkStatus(kIONetworkLinkValid, NULL);
	return kIOReturnSuccess;
}

IOReturn
NeoDarwinVirtioNet::getHardwareAddress(IOEthernetAddress *address)
{
	*address = mac;
	return kIOReturnSuccess;
}

// The device filters nothing (no VIRTIO_NET_F_CTRL_RX): every mode is
// already in effect.
IOReturn
NeoDarwinVirtioNet::setPromiscuousMode(bool active)
{
	(void)active;
	return kIOReturnSuccess;
}

IOReturn
NeoDarwinVirtioNet::setMulticastMode(bool active)
{
	(void)active;
	return kIOReturnSuccess;
}

IOReturn
NeoDarwinVirtioNet::setMulticastList(IOEthernetAddress *addresses, UInt32 count)
{
	(void)addresses; (void)count;
	return kIOReturnSuccess;
}

IOReturn
NeoDarwinVirtioNet::selectMedium(const IONetworkMedium *medium)
{
	return medium != NULL && medium->getType() == kIOMediumEthernetAuto ? kIOReturnSuccess : kIOReturnUnsupported;
}

const OSString *
NeoDarwinVirtioNet::newVendorString() const
{
	return OSString::withCString("VirtIO");
}

const OSString *
NeoDarwinVirtioNet::newModelString() const
{
	return OSString::withCString("Network Device");
}

// The link as the device last said (always up without VIRTIO_NET_F_STATUS).
void
NeoDarwinVirtioNet::reportLink(bool log)
{
	if (!enabled) {
		return;
	}
	const IONetworkMedium *medium = getSelectedMedium();
	if (linkUp) {
		setLinkStatus(kIONetworkLinkValid | kIONetworkLinkActive, medium, (UInt64)speedMbps * 1000000ULL);
	} else {
		setLinkStatus(kIONetworkLinkValid, NULL);
	}
	if (log) {
		const char *name = "";
		OSString *bsd = netif != NULL ? OSDynamicCast(OSString, netif->getProperty(kIOBSDNameKey)) : NULL;
		if (bsd != NULL) {
			name = bsd->getCStringNoCopy();
		}
		IOLog("NeoDarwinVirtioNet: %s: %s%slink %s\n", where, name, *name ? ": " : "", linkUp ? "up" : "down");
	}
}

void
NeoDarwinVirtioNet::configChanged(bool log)
{
	bool was = linkUp;
	readConfiguration();
	if (NDVirtioPCI::rd8(transport.common, kNDVirtioDeviceStatus) & kNDVirtioStatusNeedsReset) {
		IOLog("NeoDarwinVirtioNet: %s: the device needs a reset; not supported\n", where);
	}
	if (was != linkUp) {
		reportLink(log);
	}
}

// MARK: - Receive

mbuf_t
NeoDarwinVirtioNet::newRxPacket()
{
	mbuf_t m = allocatePacket(kBufferBytes);
	if (m != NULL) {
		mbuf_adj(m, kRxAlign);          // the IP header 4-byte aligned
	}
	return m;
}

// The slot's frame descriptor on m's data (or on the slot's bounce
// buffer, if the device can't reach it), made available.
bool
NeoDarwinVirtioNet::attachRxBuffer(uint32_t slot, mbuf_t m)
{
	struct IOPhysicalSegment seg;
	uint32_t length = (uint32_t)mbuf_len(m);
	struct nd_virtq_desc *d = &rxq.desc[2 * slot + 1];
	rx[slot].m = m;
	rx[slot].bounced = false;
	if (cursor->getPhysicalSegments(m, &seg, 1) == 1 && seg.location != 0 &&
	    seg.length == length && seg.location + length - 1 <= reach()) {
		// Nothing dirty may be written back over what the device writes.
		dmaPolicy.fromDevice(mbuf_data(m), length);
		d->addr = seg.location;
		d->len = length;
	} else if (rxBounce != NULL) {
		rx[slot].bounced = true;
		d->addr = NDStorageDMA::physical(rxBounce, slot * kBufferBytes);
		d->len = kBufferBytes;
	} else {
		rx[slot].m = NULL;
		return false;
	}
	rxq.flushDescriptors(2 * slot, 2);
	rxq.push((uint16_t)(2 * slot));
	return true;
}

// On the work loop: every frame the device has written, handed to the
// interface in one batch; each slot gets a new mbuf.
void
NeoDarwinVirtioNet::serviceRx()
{
	uint32_t head, written, count = 0;
	while (rxq.nextUsed(&head, &written)) {
		uint32_t slot = head / 2;
		if (head % 2 != 0 || slot >= rxSlots || rx[slot].m == NULL) {
			IOLog("NeoDarwinVirtioNet: %s: the device returned descriptor %u, which is not a receive buffer\n", where, head);
			continue;
		}
		mbuf_t m = rx[slot].m;
		uint32_t frame = written > kNDVirtioNetHeaderBytes ? written - kNDVirtioNetHeaderBytes : 0;
		uint32_t room = (uint32_t)mbuf_len(m);
		mbuf_t fresh = (enabled && frame >= 14 && frame <= room) ? newRxPacket() : NULL;
		if (fresh == NULL) {
			// Dropped: not enabled, a runt, or no mbuf to replace it; the
			// same buffer goes back on the ring.
			rxDropped++;
			if (enabled && netStats != NULL) {
				netStats->inputErrors++;
			}
			attachRxBuffer(slot, m);
			count++;
			continue;
		}
		if (rx[slot].bounced) {
			uint8_t *bounce = (uint8_t *)rxBounce->getBytesNoCopy() + slot * kBufferBytes;
			dmaPolicy.fromDevice(bounce, frame);
			mbuf_copyback(m, 0, frame, bounce, MBUF_DONTWAIT);
			rxBounced++;
		} else {
			dmaPolicy.fromDevice(mbuf_data(m), frame);
		}
		mbuf_setlen(m, frame);
		mbuf_pkthdr_setlen(m, frame);
		rxPackets++;
		rxBytes += frame;
		netif->inputPacket(m, frame, IONetworkInterface::kInputOptionQueuePacket);
		if (!attachRxBuffer(slot, fresh)) {
			freePacket(fresh);
		}
		count++;
	}
	if (count != 0) {
		rxq.publish();
		if (netif != NULL) {
			netif->flushInputQueue();
		}
	}
}

// MARK: - Transmit

// On the work loop (IOGatedOutputQueue).
UInt32
NeoDarwinVirtioNet::outputPacket(mbuf_t m, void *param)
{
	(void)param;
	if (!enabled) {
		freePacket(m);
		return kIOReturnOutputDropped;
	}
	if (txFree == 0) {
		serviceTx();
		if (txFree == 0) {
			txStalls++;
			return kIOReturnOutputStall;
		}
	}
	uint32_t slot = 0;
	while (tx[slot].busy) {
		slot++;
	}
	size_t length = mbuf_pkthdr_len(m);
	uint32_t first = slot * kTxChain;
	struct nd_virtq_desc *d = &txq.desc[first];
	struct IOPhysicalSegment segs[kTxChain - 1];
	UInt32 n = cursor->getPhysicalSegmentsWithCoalesce(m, segs, kTxChain - 1);
	bool reachable = n != 0;
	for (UInt32 i = 0; i < n && reachable; i++) {
		reachable = segs[i].location != 0 && segs[i].location + segs[i].length - 1 <= reach();
	}
	if (reachable) {
		for (mbuf_t p = m; p != NULL; p = mbuf_next(p)) {
			if (mbuf_len(p) != 0) {
				dmaPolicy.toDevice(mbuf_data(p), mbuf_len(p));
			}
		}
		for (UInt32 i = 0; i < n; i++) {
			d[1 + i].addr = segs[i].location;
			d[1 + i].len = (uint32_t)segs[i].length;
			d[1 + i].flags = kNDVirtqDescNext;
		}
		tx[slot].m = m;
	} else if (length <= kBufferBytes) {
		// Copied into the slot's bounce buffer, below the address limit.
		uint8_t *bounce = (uint8_t *)txBounce->getBytesNoCopy() + slot * kBufferBytes;
		mbuf_copydata(m, 0, length, bounce);
		dmaPolicy.toDevice(bounce, length);
		d[1].addr = NDStorageDMA::physical(txBounce, slot * kBufferBytes);
		d[1].len = (uint32_t)length;
		d[1].flags = kNDVirtqDescNext;
		n = 1;
		tx[slot].m = NULL;
		freePacket(m);
		txBounced++;
	} else {
		txDropped++;
		if (netStats != NULL) {
			netStats->outputErrors++;
		}
		freePacket(m);
		return kIOReturnOutputDropped;
	}
	d[n].flags = 0;                         // the last segment ends the chain
	tx[slot].busy = true;
	txFree--;
	txBytes += length;
	txq.flushDescriptors(first, 1 + n);
	txq.push((uint16_t)first);
	txq.publish();
	return kIOReturnOutputSuccess;
}

// Every chain the device has sent: its mbuf freed and its slot free again;
// a stalled output queue is restarted.
void
NeoDarwinVirtioNet::serviceTx()
{
	uint32_t head, written, count = 0;
	while (txq.nextUsed(&head, &written)) {
		uint32_t slot = head / kTxChain;
		if (head % kTxChain != 0 || slot >= txSlots || !tx[slot].busy) {
			IOLog("NeoDarwinVirtioNet: %s: the device returned descriptor %u, which is not a packet\n", where, head);
			continue;
		}
		if (tx[slot].m != NULL) {
			freePacket(tx[slot].m, kDelayFree);
			tx[slot].m = NULL;
		}
		tx[slot].busy = false;
		txFree++;
		txPackets++;
		count++;
	}
	if (count != 0) {
		releaseFreePackets();
		if (netStats != NULL) {
			netStats->outputPackets += count;
		}
		if (enabled) {
			transmitQueue->service(IOBasicOutputQueue::kServiceAsync);
		}
	}
}

// MARK: - Interrupts

bool
NeoDarwinVirtioNet::filter(OSObject *owner, IOFilterInterruptEventSource *source)
{
	(void)source;
	NeoDarwinVirtioNet *self = static_cast<NeoDarwinVirtioNet *>(owner);
	uint8_t bits = self->transport.readISR();   // reading acknowledges
	if (bits == 0) {
		return false;                           // another device's
	}
	__atomic_or_fetch(&self->isrBits, bits, __ATOMIC_RELAXED);
	return true;
}

// The proof that a queue's interrupt works: logged once per queue.
void
NeoDarwinVirtioNet::firstInterrupt(const char *queue, int vector)
{
	char how[48];
	if (transport.vectors == 0) {
		snprintf(how, sizeof(how), "INTx");
	} else {
		OSNumber *lpi = OSDynamicCast(OSNumber, pci->getProperty("msi-lpi-base"));
		snprintf(how, sizeof(how), "MSI-X vector %d, LPI %u", vector, (lpi != NULL ? lpi->unsigned32BitValue() : 0) + vector);
	}
	IOLog("NeoDarwinVirtioNet: %s: %s queue: first completion by its interrupt (%s)\n", where, queue, how);
}

void
NeoDarwinVirtioNet::interrupt(OSObject *owner, IOInterruptEventSource *source, int count)
{
	(void)count;
	NeoDarwinVirtioNet *self = static_cast<NeoDarwinVirtioNet *>(owner);
	if (!self->running) {
		return;
	}
	bool rx = false, tx = false, config = false;
	if (self->transport.vectors == 0) {
		uint32_t bits = __atomic_exchange_n(&self->isrBits, 0, __ATOMIC_RELAXED);
		config = (bits & kNDVirtioISRConfig) != 0;
		rx = tx = (bits & kNDVirtioISRQueue) != 0;
	} else if (source == self->configSource) {
		config = true;
	} else if (source == self->rxSource) {
		rx = true;
		tx = self->txSource == NULL;
		config = self->configSource == NULL;   // one vector for everything
	} else if (source == self->txSource) {
		tx = true;
	}
	if (config) {
		self->configChanged(true);
	}
	if (rx) {
		uint16_t before = self->rxq.usedIdx;
		self->rxInterrupts++;
		self->serviceRx();
		if (!self->rxSeen && self->rxq.usedIdx != before) {
			self->rxSeen = true;
			self->firstInterrupt("rx", self->transport.vectors >= 2 ? 1 : 0);
		}
	}
	if (tx) {
		uint16_t before = self->txq.usedIdx;
		self->txInterrupts++;
		self->serviceTx();
		if (!self->txSeen && self->txq.usedIdx != before) {
			self->txSeen = true;
			self->firstInterrupt("tx", self->transport.vectors == 3 ? 2 : (self->transport.vectors == 2 ? 1 : 0));
		}
	}
}

// MARK: - Registry

// The driver's counters, current whenever the registry is read (ioreg).
bool
NeoDarwinVirtioNet::serializeProperties(OSSerialize *s) const
{
	OSDictionary *stats = OSDictionary::withCapacity(12);
	if (stats != NULL) {
		const struct {
			const char *key;
			uint64_t value;
		} counters[] = {
			{ "rx packets", rxPackets }, { "rx bytes", rxBytes }, { "rx dropped", rxDropped },
			{ "rx bounced", rxBounced }, { "rx interrupts", rxInterrupts },
			{ "tx packets", txPackets }, { "tx bytes", txBytes }, { "tx dropped", txDropped },
			{ "tx bounced", txBounced }, { "tx stalls", txStalls }, { "tx interrupts", txInterrupts },
		};
		for (const auto &c : counters) {
			OSNumber *n = OSNumber::withNumber(c.value, 64);
			if (n != NULL) {
				stats->setObject(c.key, n);
				n->release();
			}
		}
		const_cast<NeoDarwinVirtioNet *>(this)->setProperty("Statistics", stats);
		stats->release();
	}
	return super::serializeProperties(s);
}
