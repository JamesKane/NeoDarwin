// SPDX-License-Identifier: BSD-2-Clause
// NeoDarwin-Language: expressibility: IOKit drivers are C++ IOService subclasses (IOEthernetController).
//
// The Radxa Dragon Q8B's Ethernet: the Toshiba TC956x (PCI 1179:0220, two
// functions, one MAC each), as an IONetworkingFamily Ethernet controller
// (docs/kernel/network.md, "Checkpoint 3").
//
//   IOPCIDevice 1179:0220 function 0 or 1      (segment 4, behind a TC9563 switch)
//     NeoDarwinTC956x (IOEthernetController)
//       IOEthernetInterface   en0, en1
//
// The hardware logic is nd_tc956x.h, ported from FreeBSD's tcx driver and
// tested on the host against a register model (test/tc956x_sim_test.c);
// this file is the IOKit side: matching, the BARs, DMA memory and mbufs,
// the MSI, the timers, and what IONetworkingFamily is told. A bring-up
// driver, compiled into the kernel (patch 0037) until P3-08's network dexts
// replace it, as NeoDarwinVirtioNet is.
//
// As FreeBSD's tcx: function 0 does the chip-wide set-up (the DMA window's
// translation table, the MSI generators' clock and reset); each function
// then reads the station address firmware left, restarts its MAC, SerDes
// and PCS from reset (cold init, the default) and polls its QCA8081 PHY;
// one TX and one RX DMA channel; a single MSI through the chip's MSI
// generator, with FreeBSD's interrupt moderation; IPv4/IPv6 TCP and UDP
// checksum offload; the 64-bin multicast hash filter. Function 1 starting
// before function 0 waits for it (FreeBSD's tcx refuses to attach).
//
// Not ported: TSO (IONetworkingFamily hands the driver only the MSS, so
// the headers would have to be parsed here; deferred), jumbo frames (MTU
// 1500), FreeBSD's sysctls (the counters are the "Statistics" property).
//
// DMA: the MAC sees host memory through a 64 GiB window at 0x10_0000_0000
// (nd_tc956x_dma_addr), so rings and buffers are below 36 bits (or below
// the host bridge's dma-address-bits if fewer); mbufs out of reach are
// copied through bounce buffers. The descriptor rings share cache lines
// with the device, so the driver requires dma-coherent (the Q8B's PCIe
// host bridges are, _CCA 1) and refuses a non-coherent function.
//
// Boot-args: nd_tc956x=0 keeps the driver off; nd_tc956x_cold=0 keeps a
// MAC that firmware left running (FreeBSD's hw.tcx.cold_init=0);
// nd_tc956x_csum=0 turns checksum offload off; nd_tc956x_poll=1 polls the
// rings every millisecond instead of taking the MSI.

#include <IOKit/IOBSD.h>
#include <IOKit/IOLib.h>
#include <IOKit/IOWorkLoop.h>
#include <IOKit/IOInterruptEventSource.h>
#include <IOKit/IOTimerEventSource.h>
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

#include "NeoDarwinStorageDMA.h"

// The core's accessors: the function's own BARs, mapped in start().
#define ND_TC956X_READ(sc, off)         (*(volatile uint32_t *)((volatile uint8_t *)(sc)->bar4 + (off)))
#define ND_TC956X_WRITE(sc, off, val)   (*(volatile uint32_t *)((volatile uint8_t *)(sc)->bar4 + (off)) = (val))
#define ND_TC956X_BRIDGE_WRITE(sc, off, val) \
	(*(volatile uint32_t *)((volatile uint8_t *)(sc)->bar0 + (off)) = (val))
#define ND_TC956X_DELAY(sc, us)         ndTC956xDelay(us)
#define ND_TC956X_LOG(sc, fmt, ...)     IOLog("NeoDarwinTC956x: %s: " fmt "\n", (const char *)(sc)->owner, ##__VA_ARGS__)

static void
ndTC956xDelay(uint32_t us)
{
	if (us < 1000) {
		IODelay(us);
	} else {
		IOSleep((us + 999) / 1000);
	}
}

#include "nd_tc956x.h"

class NeoDarwinTC956x : public IOEthernetController
{
	OSDeclareDefaultStructors(NeoDarwinTC956x);

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
	virtual IOReturn getChecksumSupport(UInt32 *checksumMask, UInt32 checksumFamily, bool isOutput) APPLE_KEXT_OVERRIDE;
	virtual const OSString *newVendorString() const APPLE_KEXT_OVERRIDE;
	virtual const OSString *newModelString() const APPLE_KEXT_OVERRIDE;
	virtual IOReturn selectMedium(const IONetworkMedium *medium) APPLE_KEXT_OVERRIDE;

private:
	static const uint32_t kNTxd = 256;              // descriptors, a power of 2
	static const uint32_t kNRxd = 256;
	static const uint32_t kRxBufBytes = 2048;       // an mbuf cluster; RBSZ
	static const uint32_t kTxBounceBytes = 2048;    // a whole frame (MTU 1500)
	static const uint32_t kTxMaxSegs = 8;
	static const uint32_t kTxMaxSegBytes = 4096;    // below TDES2's 14-bit length
	static const uint32_t kTxQueueCapacity = 256;
	static const uint32_t kLinkPollMs = 500;        // FreeBSD's iflib admin timer
	static const uint32_t kChipWaitMs = 10000;      // function 1 waiting for function 0
	static const uint32_t kMinFrame = 14;

	bool mapRegisters();
	bool waitForChip();
	bool setUpRings();
	bool setUpInterrupts();
	bool publishMedia();
	mbuf_t newRxPacket();
	bool postRxBuffer(mbuf_t m);
	bool startDatapath();
	void stopDatapath();
	void releaseBuffers();
	void serviceRx();
	void reclaimTx();
	void service(uint32_t status);
	void checkLink(bool log);
	void reportLink(bool log);
	const IONetworkMedium *mediumFor(uint32_t mbps) const;
	void applyFilter();
	void removeSources();
	void firstInterrupt(const char *queue);

	static void interrupt(OSObject *owner, IOInterruptEventSource *source, int count);
	static void linkTimer(OSObject *owner, IOTimerEventSource *source);
	static void pollTimer(OSObject *owner, IOTimerEventSource *source);
	static IOReturn stopAction(OSObject *owner, void *, void *, void *, void *);

	uint64_t reach() const
	{
		return dmaPolicy.addressBits >= 64 ? ~0ULL : (1ULL << dmaPolicy.addressBits) - 1;
	}
	const char *bsdName() const;

	IOPCIDevice *pci = NULL;
	char where[16] = {};                    // bus:device.function
	IOMemoryMap *bar0Map = NULL, *bar4Map = NULL;
	struct nd_tc956x sc = {};
	NDStorageDMA dmaPolicy;
	IOEthernetAddress mac = {};
	bool macFromFirmware = false;
	bool coldInit = true;
	bool csumOffload = true;
	bool pollMode = false;
	uint32_t phyID = 0;

	IOWorkLoop *workLoop = NULL;
	IOOutputQueue *transmitQueue = NULL;
	IOEthernetInterface *netif = NULL;
	IONetworkStats *netStats = NULL;
	IOMbufNaturalMemoryCursor *txCursor = NULL, *rxCursor = NULL;
	OSDictionary *media = NULL;
	IOInterruptEventSource *intrSource = NULL;
	IOTimerEventSource *linkSource = NULL, *pollSource = NULL;
	int msiIndex = -1;
	bool msiX = false;
	uint32_t lpi = 0;
	bool running = false;                   // start() finished
	bool enabled = false;                   // the interface is up
	bool datapath = false;                  // nd_tc956x_init ran, rings live

	IOBufferMemoryDescriptor *txRing = NULL, *rxRing = NULL;
	IOBufferMemoryDescriptor *txBounce = NULL, *rxBounce = NULL;
	mbuf_t txm[kNTxd] = {};                 // at a packet's last descriptor
	uint32_t txBytes[kNTxd] = {};
	mbuf_t rxm[kNRxd] = {};
	bool rxBounced[kNRxd] = {};

	bool promisc = false, allmulti = false;
	uint32_t hash[2] = {};

	// Counters, published as the "Statistics" registry property.
	uint64_t rxPackets = 0, rxBytesTotal = 0, rxDropped = 0, rxBouncedCount = 0, rxErrors = 0;
	uint64_t txPackets = 0, txBytesTotal = 0, txDropped = 0, txBouncedCount = 0, txStalls = 0;
	uint64_t linkChanges = 0;
	bool rxSeen = false, txSeen = false;
};

#define super IOEthernetController
OSDefineMetaClassAndStructors(NeoDarwinTC956x, IOEthernetController);

// MARK: - Function 0's chip set-up, as function 1 waits for it

// The chips whose function 0 has run nd_tc956x_chip_init in this boot,
// keyed by the PCI bridge both functions sit under.
static IORegistryEntry *gChipsReady[8];

static void
announceChip(IORegistryEntry *key)
{
	for (IORegistryEntry *&slot : gChipsReady) {
		IORegistryEntry *expected = NULL;
		if (__atomic_load_n(&slot, __ATOMIC_ACQUIRE) == key ||
		    __atomic_compare_exchange_n(&slot, &expected, key, false, __ATOMIC_RELEASE, __ATOMIC_RELAXED)) {
			return;
		}
	}
}

static bool
chipAnnounced(IORegistryEntry *key)
{
	for (IORegistryEntry *&slot : gChipsReady) {
		if (__atomic_load_n(&slot, __ATOMIC_ACQUIRE) == key) {
			return true;
		}
	}
	return false;
}

// Function 1: wait until function 0 has set the chip up. FreeBSD's tcx
// refuses to attach (ENXIO) when it finds the MSI generators stopped; here
// function 1 waits for function 0's driver, up to kChipWaitMs, and then
// goes on if the generators run (firmware's set-up, which FreeBSD's
// function 1 also accepts) or gives up.
bool
NeoDarwinTC956x::waitForChip()
{
	IORegistryEntry *key = pci->getParentEntry(gIOServicePlane);
	for (uint32_t ms = 0; ms < kChipWaitMs; ms += 100) {
		if (chipAnnounced(key)) {
			return true;
		}
		IOSleep(100);
	}
	if (nd_tc956x_msigen_running(&sc)) {
		IOLog("NeoDarwinTC956x: %s: function 0 has not attached in %u s; using the firmware's chip set-up\n", where,
		    kChipWaitMs / 1000);
		return true;
	}
	IOLog("NeoDarwinTC956x: %s: function 0 has not set up the chip\n", where);
	return false;
}

// MARK: - Bring-up

bool
NeoDarwinTC956x::createWorkLoop()
{
	workLoop = IOWorkLoop::workLoop();
	return workLoop != NULL;
}

IOWorkLoop *
NeoDarwinTC956x::getWorkLoop() const
{
	return workLoop;
}

IOOutputQueue *
NeoDarwinTC956x::createOutputQueue()
{
	return IOGatedOutputQueue::withTarget(this, getWorkLoop(), kTxQueueCapacity);
}

// BAR0 (the bridge configuration, TAMAP) and BAR4 (the SFR space) of this
// function, nothing else; each at least as long as the highest offset the
// core uses.
bool
NeoDarwinTC956x::mapRegisters()
{
	bar0Map = pci->mapDeviceMemoryWithRegister(kIOPCIConfigBaseAddress0);
	bar4Map = pci->mapDeviceMemoryWithRegister(kIOPCIConfigBaseAddress4);
	if (bar0Map == NULL || bar4Map == NULL) {
		IOLog("NeoDarwinTC956x: %s: cannot map registers\n", where);
		return false;
	}
	if (bar0Map->getLength() < TC956X_BAR0_MIN || bar4Map->getLength() < TC956X_SFR_MIN) {
		IOLog("NeoDarwinTC956x: %s: BAR0 is 0x%llx bytes, BAR4 0x%llx: too small for a TC956x\n", where,
		    (unsigned long long)bar0Map->getLength(), (unsigned long long)bar4Map->getLength());
		return false;
	}
	sc.bar0 = (void *)bar0Map->getVirtualAddress();
	sc.bar4 = (void *)bar4Map->getVirtualAddress();
	return true;
}

bool
NeoDarwinTC956x::setUpRings()
{
	txRing = dmaPolicy.allocate(kNTxd * sizeof(struct nd_tc956x_desc));
	rxRing = dmaPolicy.allocate(kNRxd * sizeof(struct nd_tc956x_desc));
	txBounce = dmaPolicy.allocate((size_t)kNTxd * kTxBounceBytes);
	rxBounce = dmaPolicy.allocate((size_t)kNRxd * kRxBufBytes);
	txCursor = IOMbufNaturalMemoryCursor::withSpecification(kTxMaxSegBytes, kTxMaxSegs);
	rxCursor = IOMbufNaturalMemoryCursor::withSpecification(kRxBufBytes, 1);
	if (txRing == NULL || rxRing == NULL || txBounce == NULL || rxBounce == NULL || txCursor == NULL || rxCursor == NULL) {
		IOLog("NeoDarwinTC956x: %s: no memory for the rings\n", where);
		return false;
	}
	sc.txd = (volatile struct nd_tc956x_desc *)txRing->getBytesNoCopy();
	sc.rxd = (volatile struct nd_tc956x_desc *)rxRing->getBytesNoCopy();
	sc.tx_pa = NDStorageDMA::physical(txRing);
	sc.rx_pa = NDStorageDMA::physical(rxRing);
	sc.ntxd = kNTxd;
	sc.nrxd = kNRxd;
	// The DMA keeps only the low 32 bits of its descriptor pointers: a ring
	// must not cross a 4 GiB boundary (page-aligned rings this small can't).
	uint64_t txEnd = sc.tx_pa + kNTxd * sizeof(struct nd_tc956x_desc) - 1;
	uint64_t rxEnd = sc.rx_pa + kNRxd * sizeof(struct nd_tc956x_desc) - 1;
	if ((sc.tx_pa >> 32) != (txEnd >> 32) || (sc.rx_pa >> 32) != (rxEnd >> 32)) {
		IOLog("NeoDarwinTC956x: %s: a descriptor ring crosses a 4 GiB boundary\n", where);
		return false;
	}
	return true;
}

// The function's MSI: IOPCIFamily lists it after INTx, one vector (the
// function offers 32; without SUPPORT_MULTIPLE_MSI a device gets one; an
// MSI-X vector, should IOPCIFamily choose one, is taken the same way). The
// TC956x's INTx is not used (FreeBSD's tcx doesn't either). Without an MSI
// (nd_pci_msi=0, no ITS) or with nd_tc956x_poll=1, the rings are polled.
bool
NeoDarwinTC956x::setUpInterrupts()
{
	uint32_t poll = 0;
	if (!(PE_parse_boot_argn("nd_tc956x_poll", &poll, sizeof(poll)) && poll != 0)) {
		int type = 0;
		for (int i = 0; pci->getInterruptType(i, &type) == kIOReturnSuccess; i++) {
			if ((type & (kIOInterruptTypePCIMessaged | kIOInterruptTypePCIMessagedX)) != 0) {
				msiIndex = i;
				msiX = (type & kIOInterruptTypePCIMessagedX) != 0;
				break;
			}
		}
	}
	if (msiIndex >= 0) {
		intrSource = IOInterruptEventSource::interruptEventSource(this, &NeoDarwinTC956x::interrupt, pci, msiIndex);
		if (intrSource == NULL || workLoop->addEventSource(intrSource) != kIOReturnSuccess) {
			IOLog("NeoDarwinTC956x: %s: cannot register the MSI; polling\n", where);
			OSSafeReleaseNULL(intrSource);
			msiIndex = -1;
		} else {
			OSNumber *base = OSDynamicCast(OSNumber, pci->getProperty("msi-lpi-base"));
			lpi = base != NULL ? base->unsigned32BitValue() : 0;
			intrSource->enable();
		}
	}
	pollMode = msiIndex < 0;
	if (pollMode) {
		pollSource = IOTimerEventSource::timerEventSource(this, &NeoDarwinTC956x::pollTimer);
		if (pollSource == NULL || workLoop->addEventSource(pollSource) != kIOReturnSuccess) {
			OSSafeReleaseNULL(pollSource);
			IOLog("NeoDarwinTC956x: %s: no interrupt and no poll timer\n", where);
			return false;
		}
	}
	linkSource = IOTimerEventSource::timerEventSource(this, &NeoDarwinTC956x::linkTimer);
	if (linkSource == NULL || workLoop->addEventSource(linkSource) != kIOReturnSuccess) {
		OSSafeReleaseNULL(linkSource);
		IOLog("NeoDarwinTC956x: %s: no link timer\n", where);
		return false;
	}
	return true;
}

// Autoselect and the four full-duplex speeds the MAC does (no half duplex).
bool
NeoDarwinTC956x::publishMedia()
{
	static const struct {
		IOMediumType type;
		uint32_t mbps;
	} kinds[] = {
		{ kIOMediumEthernetAuto, 0 },
		{ kIOMediumEthernet2500BaseT | kIOMediumOptionFullDuplex, 2500 },
		{ kIOMediumEthernet1000BaseT | kIOMediumOptionFullDuplex, 1000 },
		{ kIOMediumEthernet100BaseTX | kIOMediumOptionFullDuplex, 100 },
		{ kIOMediumEthernet10BaseT | kIOMediumOptionFullDuplex, 10 },
	};
	media = OSDictionary::withCapacity(5);
	if (media == NULL) {
		return false;
	}
	for (const auto &k : kinds) {
		IONetworkMedium *medium = IONetworkMedium::medium(k.type, (UInt64)k.mbps * 1000000ULL, 0, k.mbps);
		if (medium == NULL || !IONetworkMedium::addMedium(media, medium)) {
			OSSafeReleaseNULL(medium);
			return false;
		}
		medium->release();
	}
	if (!publishMediumDictionary(media)) {
		return false;
	}
	setSelectedMedium(IONetworkMedium::getMediumWithType(media, kIOMediumEthernetAuto));
	return true;
}

bool
NeoDarwinTC956x::start(IOService *provider)
{
	uint32_t on = 1;
	if (PE_parse_boot_argn("nd_tc956x", &on, sizeof(on)) && on == 0) {
		return false;
	}
	uint32_t value = 0;
	if (PE_parse_boot_argn("nd_tc956x_cold", &value, sizeof(value))) {
		coldInit = value != 0;
	}
	if (PE_parse_boot_argn("nd_tc956x_csum", &value, sizeof(value))) {
		csumOffload = value != 0;
	}
	pci = OSDynamicCast(IOPCIDevice, provider);
	if (pci == NULL) {
		return false;
	}
	snprintf(where, sizeof(where), "%02x:%02x.%x", pci->getBusNumber(), pci->getDeviceNumber(), pci->getFunctionNumber());
	int function = pci->getFunctionNumber();
	if (function > 1) {
		return false;
	}
	nd_tc956x_setup(&sc, function);
	sc.owner = where;

	// The rings share cache lines with descriptors the device writes: only
	// a coherent function is driven (the Q8B's host bridges are, _CCA 1).
	dmaPolicy = NDStorageDMA::forDevice(pci);
	if (!dmaPolicy.coherent) {
		IOLog("NeoDarwinTC956x: %s: DMA is not coherent; not supported\n", where);
		return false;
	}
	if (dmaPolicy.addressBits > TC956X_DMA_BITS) {
		dmaPolicy.addressBits = TC956X_DMA_BITS;        // the TAMAP window
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

	// FreeBSD's tcx_attach_pre, in its order.
	pci->setMemoryEnable(true);
	pci->setBusLeadEnable(true);
	if (!mapRegisters()) {
		return false;
	}
	if (function == 0) {
		nd_tc956x_chip_init(&sc);
		announceChip(pci->getParentEntry(gIOServicePlane));
	} else if (!waitForChip()) {
		return false;
	}

	// The station address, before any MAC reset clears it.
	uint8_t ea[6];
	macFromFirmware = nd_tc956x_read_mac_address(&sc, ea);
	if (macFromFirmware) {
		memcpy(mac.bytes, ea, 6);
	} else {
		// A locally administered one, from the clock and the function.
		uint32_t r = (uint32_t)mach_absolute_time();
		uint8_t a[6] = { 0x02, 0x4e, 0x44, (uint8_t)(r >> 16), (uint8_t)(r >> 8), (uint8_t)((r & 0xfe) | function) };
		memcpy(mac.bytes, a, 6);
	}
	bool cold = coldInit || !nd_tc956x_mac_running(&sc) ||
	    (ND_TC956X_READ(&sc, TC956X_NEMACCTL(function)) & TC956X_EMACCTL_INIT_DONE) == 0;
	bool started = nd_tc956x_attach_mac(&sc, coldInit);
	uint32_t version = ND_TC956X_MAC_READ(&sc, XGMAC_VERSION);
	uint32_t revision = ND_TC956X_READ(&sc, TC956X_NCID) & TC956X_NCID_REV_MASK;

	// FreeBSD's tcx_attach_post: the PHY, and its advertisement.
	phyID = nd_tc956x_phy_id(&sc);
	if (phyID == 0) {
		IOLog("NeoDarwinTC956x: %s: PHY at %d does not answer\n", where, TC956X_PHY_ADDR);
	} else {
		if (phyID != QCA8081_ID) {
			IOLog("NeoDarwinTC956x: %s: PHY at %d is 0x%08x, not a QCA8081; link state will be misread\n", where,
			    TC956X_PHY_ADDR, phyID);
		}
		(void)nd_tc956x_phy_fix_advert(&sc);
	}

	if (!setUpRings() || !setUpInterrupts() || !publishMedia()) {
		return false;
	}
	running = true;
	checkLink(false);
	linkSource->setTimeoutMS(kLinkPollMs);

	char irq[48];
	if (pollMode) {
		snprintf(irq, sizeof(irq), "polled every 1 ms");
	} else {
		snprintf(irq, sizeof(irq), "%s (LPI %u)", msiX ? "MSI-X" : "MSI", lpi);
	}
	IOLog("NeoDarwinTC956x: %s: TC956x 1179:0220 function %d: revision 0x%02x, XGMAC 0x%02x; MAC "
	    "%02x:%02x:%02x:%02x:%02x:%02x%s; %s%s, SerDes %u Mb/s; PHY 0x%08x; link %s; rx %u x %u bytes, tx %u "
	    "descriptors; %s; checksum offload %s; DMA coherent, %u address bits\n",
	    where, function, revision, version & XGMAC_VERSION_SNPS_MASK, mac.bytes[0], mac.bytes[1], mac.bytes[2],
	    mac.bytes[3], mac.bytes[4], mac.bytes[5], macFromFirmware ? " (firmware's)" : " (random)",
	    cold ? "cold init" : "firmware's MAC kept", started ? "" : " (did not start cleanly)", sc.serdes_speed, phyID,
	    sc.link.up ? "up" : "down", kNRxd - 1, kRxBufBytes, kNTxd, irq, csumOffload ? "on" : "off",
	    dmaPolicy.addressBits);

	// The interface: IONetworkStack names it and attaches it to BSD.
	if (!attachInterface((IONetworkInterface **)&netif, true)) {
		IOLog("NeoDarwinTC956x: %s: cannot attach the network interface\n", where);
		return false;
	}
	return true;
}

bool
NeoDarwinTC956x::configureInterface(IONetworkInterface *interface)
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
NeoDarwinTC956x::removeSources()
{
	IOEventSource **sources[3] = { (IOEventSource **)&intrSource, (IOEventSource **)&linkSource,
		                       (IOEventSource **)&pollSource };
	if (linkSource != NULL) {
		linkSource->cancelTimeout();
	}
	if (pollSource != NULL) {
		pollSource->cancelTimeout();
	}
	for (IOEventSource **s : sources) {
		if (*s != NULL) {
			(*s)->disable();
			if (workLoop != NULL) {
				workLoop->removeEventSource(*s);
			}
			OSSafeReleaseNULL(*s);
		}
	}
}

// On the work loop: the DMA stopped before the interface goes.
IOReturn
NeoDarwinTC956x::stopAction(OSObject *owner, void *, void *, void *, void *)
{
	NeoDarwinTC956x *self = static_cast<NeoDarwinTC956x *>(owner);
	self->stopDatapath();
	self->enabled = false;
	self->running = false;
	return kIOReturnSuccess;
}

void
NeoDarwinTC956x::stop(IOService *provider)
{
	if (workLoop != NULL) {
		workLoop->runAction(&NeoDarwinTC956x::stopAction, this);
	}
	if (netif != NULL) {
		detachInterface(netif);
		OSSafeReleaseNULL(netif);
	}
	removeSources();
	releaseBuffers();
	super::stop(provider);
}

void
NeoDarwinTC956x::free()
{
	// After a failed start, too: the DMA stopped, then everything it had.
	if (datapath) {
		stopDatapath();
	}
	running = false;
	removeSources();
	releaseBuffers();
	IOBufferMemoryDescriptor **memory[4] = { &txRing, &rxRing, &txBounce, &rxBounce };
	for (IOBufferMemoryDescriptor **m : memory) {
		if (*m != NULL) {
			(*m)->complete();
			OSSafeReleaseNULL(*m);
		}
	}
	sc.bar0 = sc.bar4 = NULL;
	OSSafeReleaseNULL(bar0Map);
	OSSafeReleaseNULL(bar4Map);
	OSSafeReleaseNULL(txCursor);
	OSSafeReleaseNULL(rxCursor);
	OSSafeReleaseNULL(media);
	OSSafeReleaseNULL(transmitQueue);
	OSSafeReleaseNULL(workLoop);
	super::free();
}

// MARK: - The datapath

mbuf_t
NeoDarwinTC956x::newRxPacket()
{
	mbuf_t m = allocatePacket(kRxBufBytes);
	if (m != NULL && mbuf_len(m) < kRxBufBytes) {
		freePacket(m);
		return NULL;
	}
	return m;
}

// m goes on the next free receive descriptor: its own cluster if the MAC
// can reach it in one piece, else the slot's bounce buffer.
bool
NeoDarwinTC956x::postRxBuffer(mbuf_t m)
{
	if (nd_tc956x_rx_posted(&sc) >= kNRxd - 1) {
		return false;
	}
	uint32_t slot = sc.rx_pidx;
	struct IOPhysicalSegment seg;
	uint64_t pa;
	if (rxCursor->getPhysicalSegments(m, &seg, 1) == 1 && seg.location != 0 && seg.length == kRxBufBytes &&
	    seg.location + kRxBufBytes - 1 <= reach()) {
		dmaPolicy.fromDevice(mbuf_data(m), kRxBufBytes);
		pa = seg.location;
		rxBounced[slot] = false;
	} else {
		pa = NDStorageDMA::physical(rxBounce, (IOByteCount)slot * kRxBufBytes);
		rxBounced[slot] = true;
	}
	rxm[slot] = m;
	nd_tc956x_rx_refill(&sc, pa);
	return true;
}

// FreeBSD's iflib_init_locked for one queue pair: tcx_init, the receive
// ring refilled (all but one descriptor), the tail moved, interrupts on.
bool
NeoDarwinTC956x::startDatapath()
{
	struct nd_tc956x_init_params p;
	p.lladdr = mac.bytes;
	p.promisc = promisc;
	p.allmulti = allmulti;
	p.hash = hash;
	p.rx_csum = csumOffload;
	p.rx_bufsz = kRxBufBytes;
	bzero((void *)sc.txd, kNTxd * sizeof(struct nd_tc956x_desc));
	bzero((void *)sc.rxd, kNRxd * sizeof(struct nd_tc956x_desc));
	dmaPolicy.toDevice(sc.txd, kNTxd * sizeof(struct nd_tc956x_desc));
	if (!nd_tc956x_init(&sc, &p)) {
		return false;
	}
	datapath = true;
	uint32_t posted = 0;
	while (posted < kNRxd - 1) {
		mbuf_t m = newRxPacket();
		if (m == NULL) {
			break;
		}
		postRxBuffer(m);
		posted++;
	}
	if (posted == 0) {
		IOLog("NeoDarwinTC956x: %s: no mbufs for the receive ring\n", where);
		stopDatapath();
		return false;
	}
	dmaPolicy.toDevice(sc.rxd, kNRxd * sizeof(struct nd_tc956x_desc));
	nd_tc956x_rx_flush(&sc);
	if (pollMode) {
		pollSource->setTimeoutMS(1);
	} else {
		nd_tc956x_intr_enable(&sc);
	}
	return true;
}

void
NeoDarwinTC956x::stopDatapath()
{
	if (!datapath) {
		return;
	}
	nd_tc956x_stop(&sc);
	if (pollSource != NULL) {
		pollSource->cancelTimeout();
	}
	datapath = false;
	releaseBuffers();
}

void
NeoDarwinTC956x::releaseBuffers()
{
	for (uint32_t i = 0; i < kNRxd; i++) {
		if (rxm[i] != NULL) {
			freePacket(rxm[i]);
			rxm[i] = NULL;
		}
	}
	for (uint32_t i = 0; i < kNTxd; i++) {
		if (txm[i] != NULL) {
			freePacket(txm[i]);
			txm[i] = NULL;
		}
		txBytes[i] = 0;
	}
}

// MARK: - The interface

const char *
NeoDarwinTC956x::bsdName() const
{
	OSString *bsd = netif != NULL ? OSDynamicCast(OSString, netif->getProperty(kIOBSDNameKey)) : NULL;
	return bsd != NULL ? bsd->getCStringNoCopy() : "";
}

IOReturn
NeoDarwinTC956x::enable(IONetworkInterface *interface)
{
	(void)interface;
	if (!running) {
		return kIOReturnNotReady;
	}
	if (!datapath && !startDatapath()) {
		return kIOReturnIOError;
	}
	enabled = true;
	transmitQueue->setCapacity(kTxQueueCapacity);
	transmitQueue->start();
	reportLink(true);
	return kIOReturnSuccess;
}

IOReturn
NeoDarwinTC956x::disable(IONetworkInterface *interface)
{
	(void)interface;
	enabled = false;
	transmitQueue->stop();
	transmitQueue->setCapacity(0);
	transmitQueue->flush();
	stopDatapath();
	setLinkStatus(kIONetworkLinkValid, NULL);
	return kIOReturnSuccess;
}

IOReturn
NeoDarwinTC956x::getHardwareAddress(IOEthernetAddress *address)
{
	*address = mac;
	return kIOReturnSuccess;
}

void
NeoDarwinTC956x::applyFilter()
{
	if (running) {
		nd_tc956x_set_filter(&sc, promisc, allmulti, hash);
	}
}

IOReturn
NeoDarwinTC956x::setPromiscuousMode(bool active)
{
	promisc = active;
	applyFilter();
	return kIOReturnSuccess;
}

IOReturn
NeoDarwinTC956x::setMulticastMode(bool active)
{
	allmulti = active;
	applyFilter();
	return kIOReturnSuccess;
}

IOReturn
NeoDarwinTC956x::setMulticastList(IOEthernetAddress *addresses, UInt32 count)
{
	hash[0] = hash[1] = 0;
	for (UInt32 i = 0; i < count; i++) {
		nd_tc956x_hash_add(hash, addresses[i].bytes);
	}
	applyFilter();
	return kIOReturnSuccess;
}

// IPv4 header, TCP and UDP over IPv4 and IPv6, both ways (TDES3.CIC 3 and
// RDES3.L34T, as FreeBSD's IFCAP_HWCSUM | IFCAP_HWCSUM_IPV6).
IOReturn
NeoDarwinTC956x::getChecksumSupport(UInt32 *checksumMask, UInt32 checksumFamily, bool isOutput)
{
	(void)isOutput;
	if (checksumFamily != kChecksumFamilyTCPIP || !csumOffload) {
		return kIOReturnUnsupported;
	}
	*checksumMask = kChecksumIP | kChecksumTCP | kChecksumUDP | kChecksumTCPIPv6 | kChecksumUDPIPv6;
	return kIOReturnSuccess;
}

const IONetworkMedium *
NeoDarwinTC956x::mediumFor(uint32_t mbps) const
{
	IOMediumType type;
	switch (mbps) {
	case 2500: type = kIOMediumEthernet2500BaseT; break;
	case 1000: type = kIOMediumEthernet1000BaseT; break;
	case 100: type = kIOMediumEthernet100BaseTX; break;
	case 10: type = kIOMediumEthernet10BaseT; break;
	default: return NULL;
	}
	return IONetworkMedium::getMediumWithType(media, type | kIOMediumOptionFullDuplex);
}

IOReturn
NeoDarwinTC956x::selectMedium(const IONetworkMedium *medium)
{
	if (medium == NULL || media == NULL) {
		return kIOReturnUnsupported;
	}
	int r = nd_tc956x_phy_set_media(&sc, medium->getType() == kIOMediumEthernetAuto ? 0 : medium->getIndex());
	if (r != 0) {
		return r == -2 ? kIOReturnUnsupported : kIOReturnIOError;
	}
	setSelectedMedium(medium);
	return kIOReturnSuccess;
}

const OSString *
NeoDarwinTC956x::newVendorString() const
{
	return OSString::withCString("Toshiba");
}

const OSString *
NeoDarwinTC956x::newModelString() const
{
	return OSString::withCString("TC956x Ethernet");
}

// The link as last applied, to IONetworkingFamily while the interface is up.
void
NeoDarwinTC956x::reportLink(bool log)
{
	if (!enabled) {
		return;
	}
	if (sc.link_known && sc.link.up) {
		setLinkStatus(kIONetworkLinkValid | kIONetworkLinkActive, mediumFor(sc.link.speed),
		    (UInt64)sc.link.speed * 1000000ULL);
	} else {
		setLinkStatus(kIONetworkLinkValid, NULL);
	}
	if (log) {
		const char *name = bsdName();
		if (sc.link_known && sc.link.up) {
			IOLog("NeoDarwinTC956x: %s: %s%slink up at %u Mb/s, %s duplex, flow control %s\n", where, name,
			    *name ? ": " : "", sc.link.speed, sc.link.fdx ? "full" : "half",
			    sc.link.txpause && sc.link.rxpause ? "rx/tx" : sc.link.rxpause ? "rx" : sc.link.txpause ? "tx" : "off");
		} else {
			IOLog("NeoDarwinTC956x: %s: %s%slink down\n", where, name, *name ? ": " : "");
		}
	}
}

// FreeBSD's tcx_update_admin_status, from the link timer.
void
NeoDarwinTC956x::checkLink(bool log)
{
	enum nd_tc956x_link_event e = nd_tc956x_link_check(&sc);
	if (e != ND_TC956X_LINK_SAME) {
		linkChanges++;
		reportLink(log);
	}
}

void
NeoDarwinTC956x::linkTimer(OSObject *owner, IOTimerEventSource *source)
{
	NeoDarwinTC956x *self = static_cast<NeoDarwinTC956x *>(owner);
	if (!self->running) {
		return;
	}
	self->checkLink(true);
	if (self->datapath) {
		self->reclaimTx();      // completions without an interrupt (coalesced)
	}
	source->setTimeoutMS(kLinkPollMs);
}

// MARK: - Receive

// On the work loop: every frame the DMA has finished, handed to the
// interface in one batch; each descriptor gets a buffer again.
void
NeoDarwinTC956x::serviceRx()
{
	struct nd_tc956x_rx_frame f;
	uint32_t count = 0;
	dmaPolicy.fromDevice(sc.rxd, kNRxd * sizeof(struct nd_tc956x_desc));
	while (nd_tc956x_rx_next(&sc, &f)) {
		mbuf_t m = rxm[f.first];
		bool bounced = rxBounced[f.first];
		rxm[f.first] = NULL;
		mbuf_t fresh = NULL;
		bool good = enabled && f.ndesc == 1 && m != NULL && f.len >= kMinFrame && f.len <= kRxBufBytes;
		if (good) {
			fresh = newRxPacket();
		}
		if (fresh == NULL) {
			// Dropped: not up, a runt, longer than a buffer (jumbo), or no
			// mbuf to replace it; its buffers go back on the ring.
			rxDropped++;
			if (enabled && netStats != NULL) {
				netStats->inputErrors++;
			}
			for (uint32_t k = 0; k < f.ndesc; k++) {
				uint32_t idx = (f.first + k) & (kNRxd - 1);
				mbuf_t b = k == 0 ? m : rxm[idx];
				rxm[idx] = NULL;
				if (b != NULL && !postRxBuffer(b)) {
					freePacket(b);
				}
			}
			count++;
			continue;
		}
		if (bounced) {
			uint8_t *bounce = (uint8_t *)rxBounce->getBytesNoCopy() + (size_t)f.first * kRxBufBytes;
			dmaPolicy.fromDevice(bounce, f.len);
			mbuf_copyback(m, 0, f.len, bounce, MBUF_DONTWAIT);
			rxBouncedCount++;
		} else {
			dmaPolicy.fromDevice(mbuf_data(m), f.len);
		}
		if ((f.des3 & RDES3_ES) != 0) {
			rxErrors++;
		}
		mbuf_setlen(m, f.len);
		mbuf_pkthdr_setlen(m, f.len);
		switch (f.csum) {
		case ND_TC956X_RX_CSUM_IP4TCP:
			setChecksumResult(m, kChecksumFamilyTCPIP, kChecksumIP | kChecksumTCP, kChecksumIP | kChecksumTCP);
			break;
		case ND_TC956X_RX_CSUM_IP4UDP:
			setChecksumResult(m, kChecksumFamilyTCPIP, kChecksumIP | kChecksumUDP, kChecksumIP | kChecksumUDP);
			break;
		case ND_TC956X_RX_CSUM_IP6TCP:
			setChecksumResult(m, kChecksumFamilyTCPIP, kChecksumTCPIPv6, kChecksumTCPIPv6);
			break;
		case ND_TC956X_RX_CSUM_IP6UDP:
			setChecksumResult(m, kChecksumFamilyTCPIP, kChecksumUDPIPv6, kChecksumUDPIPv6);
			break;
		default:
			break;
		}
		rxPackets++;
		rxBytesTotal += f.len;
		netif->inputPacket(m, f.len, IONetworkInterface::kInputOptionQueuePacket);
		if (!postRxBuffer(fresh)) {
			freePacket(fresh);
		}
		count++;
	}
	if (count != 0) {
		dmaPolicy.toDevice(sc.rxd, kNRxd * sizeof(struct nd_tc956x_desc));
		nd_tc956x_rx_flush(&sc);
		if (netif != NULL) {
			netif->flushInputQueue();
		}
	}
}

// MARK: - Transmit

// On the work loop (IOGatedOutputQueue).
UInt32
NeoDarwinTC956x::outputPacket(mbuf_t m, void *param)
{
	(void)param;
	if (!enabled || !datapath) {
		freePacket(m);
		return kIOReturnOutputDropped;
	}
	if (nd_tc956x_tx_free(&sc) < kTxMaxSegs) {
		reclaimTx();
		if (nd_tc956x_tx_free(&sc) < kTxMaxSegs) {
			txStalls++;
			return kIOReturnOutputStall;
		}
	}
	size_t length = mbuf_pkthdr_len(m);
	UInt32 demand = 0;
	enum nd_tc956x_tx_csum csum = ND_TC956X_TX_CSUM_NONE;
	if (csumOffload) {
		getChecksumDemand(m, kChecksumFamilyTCPIP, &demand);
		if ((demand & (kChecksumTCP | kChecksumUDP | kChecksumTCPIPv6 | kChecksumUDPIPv6)) != 0) {
			csum = ND_TC956X_TX_CSUM_FULL;
		} else if ((demand & kChecksumIP) != 0) {
			csum = ND_TC956X_TX_CSUM_IP;
		}
	}

	struct IOPhysicalSegment phys[kTxMaxSegs];
	struct nd_tc956x_seg segs[kTxMaxSegs];
	UInt32 n = txCursor->getPhysicalSegmentsWithCoalesce(m, phys, kTxMaxSegs);
	bool reachable = n != 0;
	for (UInt32 i = 0; i < n && reachable; i++) {
		reachable = phys[i].location != 0 && phys[i].location + phys[i].length - 1 <= reach();
		segs[i].pa = phys[i].location;
		segs[i].len = (uint32_t)phys[i].length;
	}
	uint32_t first = sc.tx_pidx;
	int last;
	if (reachable) {
		for (mbuf_t p = m; p != NULL; p = mbuf_next(p)) {
			if (mbuf_len(p) != 0) {
				dmaPolicy.toDevice(mbuf_data(p), mbuf_len(p));
			}
		}
		last = nd_tc956x_tx_encap(&sc, segs, n, (uint32_t)length, csum);
	} else {
		last = -1;
	}
	if (last < 0 && length <= kTxBounceBytes) {
		// Copied into the bounce buffer of the descriptor it will use.
		uint8_t *bounce = (uint8_t *)txBounce->getBytesNoCopy() + (size_t)first * kTxBounceBytes;
		mbuf_copydata(m, 0, length, bounce);
		dmaPolicy.toDevice(bounce, length);
		segs[0].pa = NDStorageDMA::physical(txBounce, (IOByteCount)first * kTxBounceBytes);
		segs[0].len = (uint32_t)length;
		last = nd_tc956x_tx_encap(&sc, segs, 1, (uint32_t)length, csum);
		if (last >= 0) {
			freePacket(m);
			m = NULL;
			txBouncedCount++;
		}
	}
	if (last < 0) {
		txDropped++;
		if (netStats != NULL) {
			netStats->outputErrors++;
		}
		if (m != NULL) {
			freePacket(m);
		}
		return kIOReturnOutputDropped;
	}
	txm[last] = m;
	txBytes[last] = (uint32_t)length;
	dmaPolicy.toDevice(sc.txd, kNTxd * sizeof(struct nd_tc956x_desc));
	nd_tc956x_tx_flush(&sc);
	return kIOReturnOutputSuccess;
}

// Every packet the DMA has sent: its mbuf freed, its descriptors free; a
// stalled output queue is restarted.
void
NeoDarwinTC956x::reclaimTx()
{
	uint32_t old = sc.tx_cidx;
	dmaPolicy.fromDevice(sc.txd, kNTxd * sizeof(struct nd_tc956x_desc));
	uint32_t n = nd_tc956x_tx_reclaim(&sc);
	uint32_t packets = 0;
	for (uint32_t i = 0; i < n; i++) {
		uint32_t idx = (old + i) & (kNTxd - 1);
		if (txBytes[idx] != 0) {
			packets++;
			txBytesTotal += txBytes[idx];
			txBytes[idx] = 0;
		}
		if (txm[idx] != NULL) {
			freePacket(txm[idx], kDelayFree);
			txm[idx] = NULL;
		}
	}
	if (n != 0) {
		releaseFreePackets();
		txPackets += packets;
		if (netStats != NULL) {
			netStats->outputPackets += packets;
		}
		if (enabled) {
			transmitQueue->service(IOBasicOutputQueue::kServiceAsync);
		}
	}
}

// MARK: - Interrupts

void
NeoDarwinTC956x::firstInterrupt(const char *queue)
{
	if (pollMode) {
		IOLog("NeoDarwinTC956x: %s: %s: first completion (polled)\n", where, queue);
	} else {
		IOLog("NeoDarwinTC956x: %s: %s: first completion by its interrupt (%s, LPI %u)\n", where, queue,
		    msiX ? "MSI-X" : "MSI", lpi);
	}
}

// What a channel status asks for: both rings serviced, or after a fatal
// bus error (the channel has stopped) the datapath restarted.
void
NeoDarwinTC956x::service(uint32_t status)
{
	if ((status & XGMAC_DMA_CH_FBE) != 0) {
		stopDatapath();
		if (enabled && !startDatapath()) {
			IOLog("NeoDarwinTC956x: %s: the datapath did not restart after a bus error\n", where);
		}
		return;
	}
	uint64_t rxBefore = rxPackets, txBefore = sc.tx_cidx;
	serviceRx();
	reclaimTx();
	if (!rxSeen && rxPackets != rxBefore) {
		rxSeen = true;
		firstInterrupt("rx");
	}
	if (!txSeen && sc.tx_cidx != txBefore) {
		txSeen = true;
		firstInterrupt("tx");
	}
}

// The MSI, on the work loop. The generator sends one MSI and holds off
// until MASK_CLR; nd_tc956x_intr_status turns its output off and
// acknowledges the channel, nd_tc956x_intr_enable re-arms it.
void
NeoDarwinTC956x::interrupt(OSObject *owner, IOInterruptEventSource *source, int count)
{
	(void)source; (void)count;
	NeoDarwinTC956x *self = static_cast<NeoDarwinTC956x *>(owner);
	if (!self->running || !self->datapath) {
		return;
	}
	uint32_t status = nd_tc956x_intr_status(&self->sc);
	if (status == 0) {
		return;                 // stray; the generator is re-armed
	}
	self->service(status);
	if (self->datapath) {
		nd_tc956x_intr_enable(&self->sc);
	}
}

void
NeoDarwinTC956x::pollTimer(OSObject *owner, IOTimerEventSource *source)
{
	NeoDarwinTC956x *self = static_cast<NeoDarwinTC956x *>(owner);
	if (!self->running || !self->datapath) {
		return;
	}
	uint32_t status = ND_TC956X_MAC_READ(&self->sc, XGMAC_DMA_CH_STATUS(0));
	if (status != 0) {
		ND_TC956X_MAC_WRITE(&self->sc, XGMAC_DMA_CH_STATUS(0), status);
	}
	self->service(status);
	if (self->datapath) {
		source->setTimeoutMS(1);
	}
}

// MARK: - Registry

// The driver's counters, current whenever the registry is read (ioreg).
bool
NeoDarwinTC956x::serializeProperties(OSSerialize *s) const
{
	OSDictionary *stats = OSDictionary::withCapacity(20);
	if (stats != NULL) {
		const struct {
			const char *key;
			uint64_t value;
		} counters[] = {
			{ "rx packets", rxPackets }, { "rx bytes", rxBytesTotal }, { "rx dropped", rxDropped },
			{ "rx bounced", rxBouncedCount }, { "rx error summary", sc.stat_rx_err },
			{ "rx last error status", sc.rx_err_des3 }, { "rx buffer unavailable", sc.stat_rbu },
			{ "tx packets", txPackets }, { "tx bytes", txBytesTotal }, { "tx dropped", txDropped },
			{ "tx bounced", txBouncedCount }, { "tx stalls", txStalls },
			{ "interrupts", sc.stat_intr }, { "interrupts rx", sc.stat_intr_rx }, { "interrupts tx", sc.stat_intr_tx },
			{ "dma bus errors", sc.stat_fbe }, { "link changes", linkChanges },
			{ "link speed", sc.link.up ? sc.link.speed : 0 }, { "serdes speed", sc.serdes_speed },
		};
		for (const auto &c : counters) {
			OSNumber *n = OSNumber::withNumber(c.value, 64);
			if (n != NULL) {
				stats->setObject(c.key, n);
				n->release();
			}
		}
		const_cast<NeoDarwinTC956x *>(this)->setProperty("Statistics", stats);
		stats->release();
	}
	return super::serializeProperties(s);
}
