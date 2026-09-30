// SPDX-License-Identifier: BSD-2-Clause
// NeoDarwin-Language: expressibility: IOKit drivers are C++ IOService subclasses.
//
// NeoDarwin's console USB keyboard (docs/kernel/usb-console.md, roadmap
// P1-18): a minimal xHCI driver written from the xHCI 1.2 specification,
// with the USB 2.0 hub (chapter 11) and HID 1.11 boot keyboard requests it
// needs. A bring-up aid for boards whose only input is USB (the Radxa
// Dragon Q8B at its HDMI console); P3-07's NDUSBFamily and HID dexts
// replace it.
//
// Attach: an IOPCIDevice of class 0C0330, or an IOACPIPlatformDevice named
// PNP0D10 or PNP0D15 (xHCI) or PNP0CA1 (a USB Role Switch, whose host-role
// child, _ADR 0, holds the interrupt; the Q8B's USB-C controllers). On a
// role switch that is a Synopsys DWC3 core, the core must be in host mode,
// and a receive threshold the firmware left on is turned off (nd_dwc3.h,
// from FreeBSD).
//
// Bring-up: the firmware's ownership released (USB Legacy Support), the
// controller halted and reset (HCRST, CNR), the DCBAA with the scratchpad
// buffers, a command ring, one event ring (interrupter 0), 32- or 64-byte
// contexts (HCCPARAMS1.CSZ), running, root ports powered. Interrupts: on
// PCI MSI-X, else MSI, else INTx; on ACPI the GIC interrupt of _CRS. A
// timer checks every second for an event no interrupt announced, and
// switches to polling every 10 ms if one waited a whole second; the
// boot-arg nd_xhci_poll=1 polls from the start.
//
// Devices: each root port that connects is reset (USB 2; USB 3 ports train
// by themselves) and its device addressed: Enable Slot, Address Device,
// the device and configuration descriptors, SET_CONFIGURATION. A HID boot
// keyboard (3/1/1) gets SET_PROTOCOL(boot), SET_IDLE(0) and its interrupt
// IN endpoint (Configure Endpoint); its reports become console input
// (cons_cinput, from the work loop), with typematic repeat. A USB 2 hub
// gets its descriptor, the hub fields of its slot context (ports, TT think
// time), port power and its status change endpoint, and its ports are
// reset and enumerated the same way, with the transaction translator of
// the nearest high-speed hub for low- and full-speed devices. Anything
// else is addressed, logged and left alone.
//
// Everything but the interrupt filter runs on the driver's work loop: the
// interrupt action, the poll/watchdog timer, the port scan (enumeration
// waits for its commands and control transfers by processing the event
// ring itself) and the typematic timer.
//
// DMA: the provider's dma-coherent (PCI) or _CCA (ACPI) decides cache
// maintenance: rings, contexts and the DCBAA are cleaned after the CPU
// writes them, the event ring, output contexts and buffers invalidated
// before the CPU reads them. Memory is allocated below dma-address-bits,
// and below 4 GiB when the controller can't address 64 bits (AC64 = 0).
// Boot-args: nd_xhci=0 (off), nd_xhci_poll=1, nd_xhci_coherent=0 (force
// the non-coherent path), nd_xhci_dma_bits=N (a lower address limit).

#include <IOKit/IOLib.h>
#include <IOKit/acpi/IOACPIPlatformDevice.h>
#include <IOKit/pci/IOPCIDevice.h>
#include <kern/clock.h>
#include <pexpert/pexpert.h>

#include "NeoDarwinXHCI.h"
#include "nd_usb_desc.h"

#define ND_DWC3_READ(regs, offset)              (*(volatile uint32_t *)((volatile uint8_t *)(regs) + (offset)))
#define ND_DWC3_WRITE(regs, offset, value)      (*(volatile uint32_t *)((volatile uint8_t *)(regs) + (offset)) = (value))
#include "nd_dwc3.h"

extern "C" void cons_cinput(char ch);
struct tty;
extern "C" struct tty *km_tty[1];

#define super IOService
OSDefineMetaClassAndStructors(NeoDarwinXHCI, IOService);

static constexpr uint32_t kCommandTimeoutMs = 5000;
static constexpr uint32_t kControlTimeoutMs = 5000;
static constexpr uint32_t kRepeatDelayMs = 500;
static constexpr uint32_t kRepeatRateMs = 33;
static constexpr uint32_t kPollMs = 10;
static constexpr uint32_t kWatchdogMs = 1000;
static constexpr uint32_t kMaxIntErrors = 16;

static uint64_t
deadlineAfterMs(uint32_t ms)
{
	uint64_t interval = 0;
	nanoseconds_to_absolutetime((uint64_t)ms * NSEC_PER_MSEC, &interval);
	return mach_absolute_time() + interval;
}

static const char *
completionName(uint8_t code)
{
	switch (code) {
	case ND_XHCI_CC_SUCCESS: return "success";
	case ND_XHCI_CC_DATA_BUFFER: return "data buffer error";
	case ND_XHCI_CC_BABBLE: return "babble";
	case ND_XHCI_CC_TRANSACTION: return "USB transaction error";
	case ND_XHCI_CC_TRB: return "TRB error";
	case ND_XHCI_CC_STALL: return "stall";
	case ND_XHCI_CC_RESOURCE: return "resource error";
	case ND_XHCI_CC_BANDWIDTH: return "bandwidth error";
	case ND_XHCI_CC_NO_SLOTS: return "no slots available";
	case ND_XHCI_CC_SLOT_NOT_ENABLED: return "slot not enabled";
	case ND_XHCI_CC_SHORT_PACKET: return "short packet";
	case ND_XHCI_CC_PARAMETER: return "parameter error";
	case ND_XHCI_CC_CONTEXT_STATE: return "context state error";
	case ND_XHCI_CC_EVENT_RING_FULL: return "event ring full";
	case 0: return "timeout";
	default: return "error";
	}
}

// MARK: - Registers

uint32_t
NeoDarwinXHCI::rd32(uint32_t offset) const
{
	return *(volatile uint32_t *)(regs + offset);
}

void
NeoDarwinXHCI::wr32(uint32_t offset, uint32_t value)
{
	*(volatile uint32_t *)(regs + offset) = value;
}

// 64-bit registers as two 32-bit writes, low half first (§5.1).
void
NeoDarwinXHCI::wr64(uint32_t offset, uint64_t value)
{
	wr32(offset, (uint32_t)value);
	wr32(offset + 4, (uint32_t)(value >> 32));
}

void
NeoDarwinXHCI::ringDoorbell(uint32_t slot, uint32_t target)
{
	__builtin_arm_dsb(0xf);         // DSB SY: the rings reach memory before the doorbell
	wr32(dbBase + 4 * slot, target);
}

// MARK: - Matching and start

static bool
acpiHasID(IOService *nub, const char *id)
{
	OSString *s = OSString::withCString(id);
	bool match = s != NULL && nub->compareName(s);
	OSSafeReleaseNULL(s);
	return match;
}

// A role switch's host-role child: _ADR 0 with an interrupt on the GIC. It
// is in IOACPIPlane only (an _ADR-only device); the ACPI platform
// publishes present devices alone, so it is present.
static IOACPIPlatformDevice *
roleSwitchHost(IOACPIPlatformDevice *nub)
{
	IOACPIPlatformDevice *found = NULL;
	OSIterator *it = nub->getChildIterator(gIOACPIPlane);
	if (it == NULL) {
		return NULL;
	}
	while (OSObject *o = it->getNextObject()) {
		IOACPIPlatformDevice *child = OSDynamicCast(IOACPIPlatformDevice, o);
		OSNumber *adr = child != NULL ? OSDynamicCast(OSNumber, child->getProperty(gIOACPIAddressKey)) : NULL;
		if (adr != NULL && adr->unsigned64BitValue() == 0 && child->getProperty(gIOInterruptSpecifiersKey) != NULL) {
			found = child;
			break;
		}
	}
	it->release();
	return found;
}

IOService *
NeoDarwinXHCI::probe(IOService *provider, SInt32 *score)
{
	IOACPIPlatformDevice *nub = OSDynamicCast(IOACPIPlatformDevice, provider);
	if (nub != NULL && !acpiHasID(nub, "PNP0D10") && !acpiHasID(nub, "PNP0D15")) {
		// A USB Role Switch: only with a host-role child to take the
		// interrupt from, as FreeBSD's generic_xhci_acpi.
		if (roleSwitchHost(nub) == NULL) {
			return NULL;
		}
	}
	return super::probe(provider, score);
}

bool
NeoDarwinXHCI::start(IOService *provider)
{
	uint32_t enabled = 1;
	PE_parse_boot_argn("nd_xhci", &enabled, sizeof(enabled));
	pci = OSDynamicCast(IOPCIDevice, provider);
	acpi = OSDynamicCast(IOACPIPlatformDevice, provider);
	if (pci != NULL) {
		snprintf(where, sizeof(where), "%02x:%02x.%x", pci->getBusNumber(), pci->getDeviceNumber(),
		    pci->getFunctionNumber());
	} else if (acpi != NULL) {
		OSString *path = OSDynamicCast(OSString, acpi->getProperty("acpi-path"));
		strlcpy(where, path != NULL ? path->getCStringNoCopy() : acpi->getName(), sizeof(where));
	} else {
		return false;
	}
	if (enabled == 0) {
		IOLog("NeoDarwinXHCI: %s: off (nd_xhci=0)\n", where);
		return false;
	}
	if (!super::start(provider)) {
		return false;
	}
	if (!startController(provider)) {
		stop(provider);
		return false;
	}
	registerService();
	return true;
}

bool
NeoDarwinXHCI::startController(IOService *provider)
{
	uint32_t poll = 0, coherent = 1, dmaBits = 64;
	PE_parse_boot_argn("nd_xhci_poll", &poll, sizeof(poll));
	PE_parse_boot_argn("nd_xhci_coherent", &coherent, sizeof(coherent));
	bool capBits = PE_parse_boot_argn("nd_xhci_dma_bits", &dmaBits, sizeof(dmaBits));
	polling = poll != 0;

	workLoop = IOWorkLoop::workLoop();
	pollTimer = IOTimerEventSource::timerEventSource(this, &NeoDarwinXHCI::pollFired);
	scanTimer = IOTimerEventSource::timerEventSource(this, &NeoDarwinXHCI::scanFired);
	repeatTimer = IOTimerEventSource::timerEventSource(this, &NeoDarwinXHCI::repeatFired);
	if (workLoop == NULL || pollTimer == NULL || scanTimer == NULL || repeatTimer == NULL ||
	    workLoop->addEventSource(pollTimer) != kIOReturnSuccess || workLoop->addEventSource(scanTimer) != kIOReturnSuccess ||
	    workLoop->addEventSource(repeatTimer) != kIOReturnSuccess) {
		IOLog("NeoDarwinXHCI: %s: no work loop\n", where);
		return false;
	}

	if (pci != NULL) {
		// MSI-X first, before anything resolves the device's interrupts
		// (gic-its.md, "For P1-10").
		if (!polling && pci->configureInterrupts(kIOInterruptTypePCIMessagedX, 1, 1, 0) == kIOReturnSuccess) {
			int type = 0;
			if (pci->getInterruptType(0, &type) == kIOReturnSuccess && (type & kIOInterruptTypePCIMessagedX) != 0) {
				interruptMode = kIntrMSIX;
				interruptIndex = 0;
			}
		}
		dma = NDStorageDMA::forDevice(pci);
		pci->setMemoryEnable(true);
		pci->setBusLeadEnable(true);
	} else {
		// _CCA (ACPI 6.5 §6.2.17): absent means not coherent on Arm.
		UInt32 cca = 0;
		dma.coherent = acpi->evaluateInteger("_CCA", &cca) == kIOReturnSuccess && cca != 0;
		dma.addressBits = 64;
	}
	if (coherent == 0) {
		dma.coherent = false;
	}
	if (capBits && dmaBits >= 32 && dmaBits < dma.addressBits) {
		dma.addressBits = (uint8_t)dmaBits;
	}
	if (!mapRegisters(provider)) {
		return false;
	}
	if (acpi != NULL && !setUpACPI()) {
		return false;
	}

	uint32_t caps = rd32(ND_XHCI_CAPLENGTH);
	if (caps == 0xffffffff) {
		IOLog("NeoDarwinXHCI: %s: the registers read all ones\n", where);
		return false;
	}
	opBase = caps & 0xff;
	version = (uint16_t)(caps >> 16);
	hcs1 = rd32(ND_XHCI_HCSPARAMS1);
	hcs2 = rd32(ND_XHCI_HCSPARAMS2);
	hcc1 = rd32(ND_XHCI_HCCPARAMS1);
	dbBase = rd32(ND_XHCI_DBOFF) & ~3u;
	rtBase = rd32(ND_XHCI_RTSOFF) & ~0x1fu;
	contextBytes = (hcc1 & ND_XHCI_HCC1_CSZ) ? 64 : 32;
	uint32_t maxSlots = ND_XHCI_HCS1_MAX_SLOTS(hcs1);
	slotsEnabled = (uint8_t)(maxSlots < kMaxSlots ? maxSlots : kMaxSlots);
	ports = (uint8_t)ND_XHCI_HCS1_MAX_PORTS(hcs1);
	scratchpads = ND_XHCI_HCS2_SCRATCHPADS(hcs2);
	if (opBase < 0x20 || opBase + 0x400 + 0x10 * (uint64_t)ports > regsLength || rtBase + 0x40 > regsLength ||
	    dbBase + 4 * (kMaxSlots + 1) > regsLength || slotsEnabled == 0 || ports == 0) {
		IOLog("NeoDarwinXHCI: %s: implausible registers (CAPLENGTH 0x%x, DBOFF 0x%x, RTSOFF 0x%x, %u slots, %u ports) "
		    "in a 0x%llx-byte window\n", where, opBase, dbBase, rtBase, maxSlots, ports, regsLength);
		return false;
	}
	if ((hcc1 & ND_XHCI_HCC1_AC64) == 0 && dma.addressBits > 32) {
		dma.addressBits = 32;           // 32-bit addresses only
	}

	legacyHandoff();
	readProtocols();
	if (!haltAndReset()) {
		return false;
	}
	uint32_t pagesize = op32(ND_XHCI_PAGESIZE) & 0xffff;
	pageBytes = pagesize == 0 ? 4096 : 4096u << __builtin_ctz(pagesize);
	if (!allocateController()) {
		return false;
	}
	if (!polling && !setUpInterrupts()) {
		polling = true;
	}
	if (!runController()) {
		return false;
	}
	logController();

	// Every root port is looked at once; after that, port status change
	// events say which.
	for (uint32_t p = 1; p <= ports; p++) {
		markPortChanged(p);
	}
	scheduleScan(polling ? 20 : 1);
	pollTimer->setTimeoutMS(polling ? kPollMs : kWatchdogMs);
	return true;
}

bool
NeoDarwinXHCI::mapRegisters(IOService *provider)
{
	if (pci != NULL) {
		map = pci->mapDeviceMemoryWithRegister(kIOPCIConfigBaseAddress0);
	} else {
		map = provider->mapDeviceMemoryWithIndex(0);
	}
	if (map == NULL || map->getLength() < 0x1000) {
		IOLog("NeoDarwinXHCI: %s: no register window\n", where);
		return false;
	}
	regs = (volatile uint8_t *)map->getVirtualAddress();
	regsLength = map->getLength();
	return true;
}

// ACPI: which interrupt, and a DWC3 role switch's host mode and receive
// threshold (nd_dwc3.h, from FreeBSD's generic_xhci_acpi).
bool
NeoDarwinXHCI::setUpACPI(void)
{
	roleSwitch = !acpiHasID(acpi, "PNP0D10") && !acpiHasID(acpi, "PNP0D15");
	interruptNub = acpi;
	if (roleSwitch) {
		IOACPIPlatformDevice *host = roleSwitchHost(acpi);
		if (host == NULL) {
			IOLog("NeoDarwinXHCI: %s: USB Role Switch without a host-role child\n", where);
			return false;
		}
		interruptNub = host;
		uint32_t gsnpsid = 0, grxthrcfg = 0;
		switch (nd_dwc3_urs_setup((void *)regs, regsLength, &gsnpsid, &grxthrcfg)) {
		case ND_DWC3_NOT_HOST:
			IOLog("NeoDarwinXHCI: %s: DWC3 core (GSNPSID 0x%08x) not in host mode (GCTL 0x%08x); not used\n", where,
			    gsnpsid, rd32(ND_DWC3_GCTL));
			return false;
		case ND_DWC3_READY:
			snprintf(dwc3Note, sizeof(dwc3Note), "; DWC3 core, GSNPSID 0x%08x, host mode, GRXTHRCFG 0x%08x -> 0x%08x",
			    gsnpsid, grxthrcfg, rd32(ND_DWC3_GRXTHRCFG));
			break;
		case ND_DWC3_NOT_DWC3:
			break;
		}
	}
	interruptNub->retain();
	if (!polling) {
		interruptMode = kIntrGSIV;
		interruptIndex = 0;
	}
	return true;
}

// USB Legacy Support (§4.22.1): the firmware owns the controller until the
// OS asks; UEFI's xHCI driver releases it at the OS Owned request, or at
// ExitBootServices. Then its SMIs are disabled and their events acknowledged.
void
NeoDarwinXHCI::legacyHandoff(void)
{
	uint32_t offset = ND_XHCI_HCC1_XECP(hcc1) * 4;
	for (int guard = 0; offset != 0 && offset + 8 <= regsLength && guard < 64; guard++) {
		uint32_t v = rd32(offset);
		if (ND_XHCI_XCAP_ID(v) == ND_XHCI_XCAP_LEGACY) {
			if (v & ND_XHCI_LEGSUP_BIOS_OWNED) {
				wr32(offset, v | ND_XHCI_LEGSUP_OS_OWNED);
				uint32_t ms = 0;
				while ((rd32(offset) & ND_XHCI_LEGSUP_BIOS_OWNED) && ms < 1000) {
					IOSleep(10);
					ms += 10;
				}
				if (rd32(offset) & ND_XHCI_LEGSUP_BIOS_OWNED) {
					wr32(offset, (rd32(offset) & ~ND_XHCI_LEGSUP_BIOS_OWNED) | ND_XHCI_LEGSUP_OS_OWNED);
					snprintf(legacyNote, sizeof(legacyNote), "; firmware did not release it in 1 s: taken");
				} else {
					snprintf(legacyNote, sizeof(legacyNote), "; firmware released it in %u ms", ms);
				}
			} else {
				wr32(offset, v | ND_XHCI_LEGSUP_OS_OWNED);
				snprintf(legacyNote, sizeof(legacyNote), "; legacy support: not firmware owned");
			}
			wr32(offset + 4, nd_xhci_legctl_quiet(rd32(offset + 4)));
		}
		uint32_t next = ND_XHCI_XCAP_NEXT(v);
		offset = next == 0 ? 0 : offset + next * 4;
	}
}

// Supported Protocol capabilities (§7.2): which root ports are USB 2 and
// which USB 3.
void
NeoDarwinXHCI::readProtocols(void)
{
	uint32_t offset = ND_XHCI_HCC1_XECP(hcc1) * 4;
	for (int guard = 0; offset != 0 && offset + 12 <= regsLength && guard < 64; guard++) {
		uint32_t v = rd32(offset);
		if (ND_XHCI_XCAP_ID(v) == ND_XHCI_XCAP_PROTOCOL) {
			uint32_t d2 = rd32(offset + 8);
			uint32_t first = ND_XHCI_PROTO_PORT(d2), count = ND_XHCI_PROTO_COUNT(d2);
			for (uint32_t p = first; p < first + count && p <= kMaxPorts; p++) {
				portMajor[p] = (uint8_t)ND_XHCI_PROTO_MAJOR(v);
				portMinor[p] = (uint8_t)ND_XHCI_PROTO_MINOR(v);
			}
		}
		uint32_t next = ND_XHCI_XCAP_NEXT(v);
		offset = next == 0 ? 0 : offset + next * 4;
	}
}

// Halt (R/S = 0, HCH within 16 ms), then HCRST; the controller is ready
// again when HCRST and CNR are clear (§4.2).
bool
NeoDarwinXHCI::haltAndReset(void)
{
	uint32_t ms;
	for (ms = 0; (op32(ND_XHCI_USBSTS) & ND_XHCI_STS_CNR) && ms < 1000; ms++) {
		IOSleep(1);
	}
	uint32_t cmd = op32(ND_XHCI_USBCMD);
	if (cmd & ND_XHCI_CMD_RS) {
		opw32(ND_XHCI_USBCMD, cmd & ~(ND_XHCI_CMD_RS | ND_XHCI_CMD_INTE | ND_XHCI_CMD_HSEE));
	}
	for (ms = 0; !(op32(ND_XHCI_USBSTS) & ND_XHCI_STS_HCH) && ms < 100; ms++) {
		IOSleep(1);
	}
	if (!(op32(ND_XHCI_USBSTS) & ND_XHCI_STS_HCH)) {
		IOLog("NeoDarwinXHCI: %s: the controller does not halt (USBSTS 0x%x)\n", where, op32(ND_XHCI_USBSTS));
		return false;
	}
	opw32(ND_XHCI_USBCMD, ND_XHCI_CMD_HCRST);
	IODelay(1000);                  // some controllers can't be read right after HCRST
	for (ms = 0; ((op32(ND_XHCI_USBCMD) & ND_XHCI_CMD_HCRST) || (op32(ND_XHCI_USBSTS) & ND_XHCI_STS_CNR)) && ms < 1000; ms++) {
		IOSleep(1);
	}
	if ((op32(ND_XHCI_USBCMD) & ND_XHCI_CMD_HCRST) || (op32(ND_XHCI_USBSTS) & ND_XHCI_STS_CNR)) {
		IOLog("NeoDarwinXHCI: %s: the controller does not come out of reset (USBCMD 0x%x, USBSTS 0x%x)\n", where,
		    op32(ND_XHCI_USBCMD), op32(ND_XHCI_USBSTS));
		return false;
	}
	return true;
}

void
NeoDarwinXHCI::initRing(Ring *ring, volatile void *memory, uint64_t pa, uint32_t size)
{
	ring->trb = (volatile struct nd_xhci_trb *)memory;
	ring->pa = pa;
	ring->size = size;
	ring->enqueue = 0;
	ring->cycle = 1;
	volatile struct nd_xhci_trb *link = &ring->trb[size - 1];
	link->param = pa;
	link->status = 0;
	link->control = ND_XHCI_TRB_TYPE(ND_XHCI_TRB_LINK) | ND_XHCI_TRB_TC;   // not yet the controller's
}

bool
NeoDarwinXHCI::allocateController(void)
{
	if (scratchpads * 8 > kCtlBytes - kCtlScratchArray || pageBytes > PAGE_SIZE) {
		IOLog("NeoDarwinXHCI: %s: %u scratchpad buffers of %u bytes: not supported\n", where, scratchpads, pageBytes);
		return false;
	}
	ctlMemory = dma.allocate(kCtlBytes);
	eventMemory = dma.allocate(kEventTRBs * sizeof(struct nd_xhci_trb));
	if (scratchpads != 0) {
		scratchMemory = dma.allocate((size_t)scratchpads * pageBytes);
	}
	if (ctlMemory == NULL || eventMemory == NULL || (scratchpads != 0 && scratchMemory == NULL)) {
		IOLog("NeoDarwinXHCI: %s: no memory below %u address bits\n", where, dma.addressBits);
		return false;
	}
	ctl = (uint8_t *)ctlMemory->getBytesNoCopy();
	ctlPA = NDStorageDMA::physical(ctlMemory);
	dcbaa = (volatile uint64_t *)(ctl + kCtlDCBAA);
	events = (volatile struct nd_xhci_trb *)eventMemory->getBytesNoCopy();
	eventsPA = NDStorageDMA::physical(eventMemory);
	eventDequeue = 0;
	eventCycle = 1;
	initRing(&command, ctl + kCtlCommandRing, ctlPA + kCtlCommandRing, kCommandTRBs);
	if (scratchpads != 0) {
		volatile uint64_t *array = (volatile uint64_t *)(ctl + kCtlScratchArray);
		uint64_t base = NDStorageDMA::physical(scratchMemory);
		for (uint32_t i = 0; i < scratchpads; i++) {
			array[i] = base + (uint64_t)i * pageBytes;
		}
		dcbaa[0] = ctlPA + kCtlScratchArray;
	}
	volatile uint32_t *erst = (volatile uint32_t *)(ctl + kCtlERST);
	erst[0] = (uint32_t)eventsPA;
	erst[1] = (uint32_t)(eventsPA >> 32);
	erst[2] = kEventTRBs;
	erst[3] = 0;
	dma.toDevice(ctl, kCtlBytes);
	return true;
}

// MARK: - Interrupts

bool
NeoDarwinXHCI::setUpInterrupts(void)
{
	if (pci != NULL && interruptMode == kIntrNone) {
		// No MSI-X: MSI if IOPCIFamily lists it (after INTx), else INTx.
		int type = 0;
		for (int i = 0; pci->getInterruptType(i, &type) == kIOReturnSuccess; i++) {
			if ((type & kIOInterruptTypePCIMessaged) != 0) {
				interruptMode = kIntrMSI;
				interruptIndex = i;
				break;
			}
		}
		if (interruptMode == kIntrNone && pci->getInterruptType(0, &type) == kIOReturnSuccess) {
			interruptMode = kIntrINTx;
			interruptIndex = 0;
		}
	}
	IOService *nub = pci != NULL ? (IOService *)pci : interruptNub;
	if (interruptMode == kIntrMSIX || interruptMode == kIntrMSI) {
		// Edge-triggered and not shared; the controller clears IP itself
		// once the message is written (§5.5.2.1).
		interruptSource = IOInterruptEventSource::interruptEventSource(this, &NeoDarwinXHCI::interrupt, nub,
		    interruptIndex);
		OSNumber *base = OSDynamicCast(OSNumber, pci->getProperty("msi-lpi-base"));
		lpi = base != NULL ? base->unsigned32BitValue() : 0;
	} else if (interruptMode == kIntrINTx || interruptMode == kIntrGSIV) {
		// Level and possibly shared: the filter claims it only when
		// interrupter 0 has an interrupt pending, and clears it.
		interruptSource = IOFilterInterruptEventSource::filterInterruptEventSource(this, &NeoDarwinXHCI::interrupt,
		    &NeoDarwinXHCI::filter, nub, interruptIndex);
	}
	if (interruptSource == NULL || workLoop->addEventSource(interruptSource) != kIOReturnSuccess) {
		IOLog("NeoDarwinXHCI: %s: no interrupt; polling every %u ms\n", where, kPollMs);
		OSSafeReleaseNULL(interruptSource);
		interruptMode = kIntrNone;
		return false;
	}
	return true;
}

static uint32_t
firstSpecifier(IOService *nub)
{
	OSArray *specs = OSDynamicCast(OSArray, nub->getProperty(gIOInterruptSpecifiersKey));
	OSData *spec = specs != NULL ? OSDynamicCast(OSData, specs->getObject(0)) : NULL;
	if (spec == NULL || spec->getLength() < 4) {
		return 0;
	}
	return *(const uint32_t *)spec->getBytesNoCopy();
}

bool
NeoDarwinXHCI::filter(OSObject *owner, IOFilterInterruptEventSource *source)
{
	(void)source;
	NeoDarwinXHCI *self = static_cast<NeoDarwinXHCI *>(owner);
	if (!self->running) {
		return false;
	}
	uint32_t ir = self->rtBase + ND_XHCI_IR(0);
	uint32_t iman = self->rd32(ir + ND_XHCI_IR_IMAN);
	if (iman == 0xffffffff || (iman & ND_XHCI_IMAN_IP) == 0) {
		return false;
	}
	self->wr32(ir + ND_XHCI_IR_IMAN, iman | ND_XHCI_IMAN_IP);       // RW1C: deasserts the line
	self->wr32(self->opBase + ND_XHCI_USBSTS, ND_XHCI_STS_EINT);
	return true;
}

void
NeoDarwinXHCI::interrupt(OSObject *owner, IOInterruptEventSource *source, int count)
{
	(void)source;
	(void)count;
	NeoDarwinXHCI *self = static_cast<NeoDarwinXHCI *>(owner);
	if (!self->running) {
		return;
	}
	self->interruptCount++;
	if (self->interruptMode == kIntrMSIX || self->interruptMode == kIntrMSI) {
		uint32_t ir = self->rtBase + ND_XHCI_IR(0);
		self->wr32(ir + ND_XHCI_IR_IMAN, ND_XHCI_IMAN_IP | ND_XHCI_IMAN_IE);
		self->wr32(self->opBase + ND_XHCI_USBSTS, ND_XHCI_STS_EINT);
	}
	uint32_t n = self->processEvents();
	if (n != 0 && !self->announcedInterrupt) {
		self->announcedInterrupt = true;
		switch (self->interruptMode) {
		case kIntrMSIX:
			IOLog("NeoDarwinXHCI: %s: first event by interrupt (MSI-X vector 0, LPI %u)\n", self->where, self->lpi);
			break;
		case kIntrMSI:
			IOLog("NeoDarwinXHCI: %s: first event by interrupt (MSI, LPI %u)\n", self->where, self->lpi);
			break;
		case kIntrINTx:
			IOLog("NeoDarwinXHCI: %s: first event by interrupt (INTx, GSIV %u)\n", self->where, firstSpecifier(self->pci));
			break;
		default:
			IOLog("NeoDarwinXHCI: %s: first event by interrupt (GSIV %u)\n", self->where,
			    firstSpecifier(self->interruptNub));
			break;
		}
	}
}

// Polling (every 10 ms), or with interrupts the watchdog (every second): an
// event that was already pending at the last look was not announced by an
// interrupt within a second, so interrupts don't work and polling takes
// over. The Q8B's path is the one to watch: GIC SPIs from ACPI with
// non-coherent DMA.
void
NeoDarwinXHCI::pollFired(OSObject *owner, IOTimerEventSource *sender)
{
	(void)sender;
	NeoDarwinXHCI *self = static_cast<NeoDarwinXHCI *>(owner);
	if (!self->running) {
		return;
	}
	if (self->polling) {
		uint32_t n = self->processEvents();
		if (n != 0 && !self->announcedPoll) {
			self->announcedPoll = true;
			IOLog("NeoDarwinXHCI: %s: first event by polling\n", self->where);
		}
		self->pollTimer->setTimeoutMS(kPollMs);
		return;
	}
	volatile struct nd_xhci_trb *e = &self->events[self->eventDequeue];
	self->dma.fromDevice(e, sizeof(*e));
	bool pending = (e->control & ND_XHCI_TRB_CYCLE) == self->eventCycle;
	if (pending && self->watchPending && self->watchDequeue == self->eventDequeue) {
		IOLog("NeoDarwinXHCI: %s: an event waited a second without an interrupt (%u interrupts so far); "
		    "polling every %u ms\n", self->where, self->interruptCount, kPollMs);
		self->polling = true;
		self->processEvents();
		self->watchPending = false;
		self->pollTimer->setTimeoutMS(kPollMs);
		return;
	}
	self->watchPending = pending;
	self->watchDequeue = self->eventDequeue;
	self->pollTimer->setTimeoutMS(kWatchdogMs);
}

// MARK: - Running

bool
NeoDarwinXHCI::runController(void)
{
	uint32_t config = op32(ND_XHCI_CONFIG);
	opw32(ND_XHCI_CONFIG, (config & ~0xffu) | slotsEnabled);
	wr64(opBase + ND_XHCI_DCBAAP, ctlPA + kCtlDCBAA);
	wr64(opBase + ND_XHCI_CRCR, command.pa | ND_XHCI_CRCR_RCS);
	uint32_t ir = rtBase + ND_XHCI_IR(0);
	wr32(ir + ND_XHCI_IR_ERSTSZ, (rd32(ir + ND_XHCI_IR_ERSTSZ) & ~0xffffu) | 1);
	wr64(ir + ND_XHCI_IR_ERDP, eventsPA);
	wr64(ir + ND_XHCI_IR_ERSTBA, ctlPA + kCtlERST);
	wr32(ir + ND_XHCI_IR_IMOD, 160);        // 40 µs
	bool interrupts = interruptSource != NULL;
	wr32(ir + ND_XHCI_IR_IMAN, ND_XHCI_IMAN_IP | (interrupts ? ND_XHCI_IMAN_IE : 0));
	running = true;
	if (interrupts) {
		interruptSource->enable();
	}
	opw32(ND_XHCI_USBCMD, (interrupts ? ND_XHCI_CMD_INTE : 0) | ND_XHCI_CMD_RS);
	uint32_t ms;
	for (ms = 0; (op32(ND_XHCI_USBSTS) & ND_XHCI_STS_HCH) && ms < 100; ms++) {
		IOSleep(1);
	}
	if (op32(ND_XHCI_USBSTS) & ND_XHCI_STS_HCH) {
		IOLog("NeoDarwinXHCI: %s: the controller does not run (USBSTS 0x%x)\n", where, op32(ND_XHCI_USBSTS));
		running = false;
		return false;
	}
	// Port power, where the controller switches it (§4.19.4).
	if (hcc1 & ND_XHCI_HCC1_PPC) {
		bool powered = false;
		for (uint32_t p = 1; p <= ports; p++) {
			uint32_t sc = portsc(p);
			if ((sc & ND_XHCI_PORTSC_PP) == 0) {
				setPortsc(p, nd_xhci_portsc_neutral(sc) | ND_XHCI_PORTSC_PP);
				powered = true;
			}
		}
		if (powered) {
			IOSleep(20);
		}
	}
	return true;
}

void
NeoDarwinXHCI::logController(void)
{
	char protocols[96] = "";
	size_t used = 0;
	for (uint32_t p = 1; p <= ports;) {
		uint32_t q = p;
		while (q + 1 <= ports && portMajor[q + 1] == portMajor[p] && portMinor[q + 1] == portMinor[p]) {
			q++;
		}
		int n = portMajor[p] != 0 ?
		    snprintf(protocols + used, sizeof(protocols) - used, "%sUSB %x.%x: %u-%u", used ? ", " : "", portMajor[p],
		    portMinor[p] >> 4, p, q) :
		    snprintf(protocols + used, sizeof(protocols) - used, "%sno protocol: %u-%u", used ? ", " : "", p, q);
		if (n > 0 && (size_t)n < sizeof(protocols) - used) {
			used += (size_t)n;
		}
		p = q + 1;
	}
	char ident[96];
	if (pci != NULL) {
		snprintf(ident, sizeof(ident), "%04x:%04x", pci->configRead16(kIOPCIConfigVendorID),
		    pci->configRead16(kIOPCIConfigDeviceID));
	} else {
		OSString *hid = OSDynamicCast(OSString, acpi->getProperty(gIOACPIHardwareIDKey));
		IODeviceMemory *m = acpi->getDeviceMemoryWithIndex(0);
		snprintf(ident, sizeof(ident), "%s at 0x%llx", hid != NULL ? hid->getCStringNoCopy() : "ACPI",
		    m != NULL ? (unsigned long long)m->getPhysicalAddress() : 0ULL);
	}
	char intr[64];
	switch (interruptMode) {
	case kIntrMSIX:
		snprintf(intr, sizeof(intr), "MSI-X vector 0 (LPI %u)", lpi);
		break;
	case kIntrMSI:
		snprintf(intr, sizeof(intr), "MSI (LPI %u)", lpi);
		break;
	case kIntrINTx:
		snprintf(intr, sizeof(intr), "INTx (GSIV %u)", firstSpecifier(pci));
		break;
	case kIntrGSIV: {
		OSData *flags = OSDynamicCast(OSData, interruptNub->getProperty("acpi-interrupt-flags"));
		bool edge = flags != NULL && flags->getLength() >= 4 && (*(const uint32_t *)flags->getBytesNoCopy() & 1);
		snprintf(intr, sizeof(intr), "GSIV %u (%s%s)", firstSpecifier(interruptNub), edge ? "edge" : "level",
		    interruptNub != acpi ? ", from the host-role child" : "");
		break;
	}
	default:
		snprintf(intr, sizeof(intr), "polling every %u ms", kPollMs);
		break;
	}
	IOLog("NeoDarwinXHCI: %s: xHCI %x.%x, %s; %u slots (%u enabled), %u ports (%s), %u-byte contexts, "
	    "%u scratchpad buffers%s%s\n", where, version >> 8, (version >> 4) & 0xf, ident, ND_XHCI_HCS1_MAX_SLOTS(hcs1),
	    slotsEnabled, ports, protocols, contextBytes, scratchpads, legacyNote, dwc3Note);
	IOLog("NeoDarwinXHCI: %s: %s; DMA %s, %u address bits%s\n", where, intr,
	    dma.coherent ? "coherent" : "not coherent", dma.addressBits,
	    (hcc1 & ND_XHCI_HCC1_AC64) ? "" : " (32-bit controller)");
}

// MARK: - Rings, commands, events

// Room for `trbs` TRBs before the Link TRB, so that no TD straddles it:
// the rest of the segment becomes No-op TRBs.
void
NeoDarwinXHCI::reserve(Ring *ring, uint32_t trbs)
{
	if (ring->enqueue + trbs <= ring->size - 1) {
		return;
	}
	while (ring->enqueue != 0) {
		push(ring, 0, 0, ND_XHCI_TRB_TYPE(ND_XHCI_TRB_NOOP));
	}
}

uint64_t
NeoDarwinXHCI::push(Ring *ring, uint64_t param, uint32_t status, uint32_t control)
{
	volatile struct nd_xhci_trb *t = &ring->trb[ring->enqueue];
	uint64_t pa = ring->pa + (uint64_t)ring->enqueue * sizeof(*t);
	t->param = param;
	t->status = status;
	t->control = (control & ~ND_XHCI_TRB_CYCLE) | ring->cycle;
	ring->enqueue++;
	if (ring->enqueue == ring->size - 1) {
		volatile struct nd_xhci_trb *link = &ring->trb[ring->size - 1];
		link->param = ring->pa;
		link->status = 0;
		link->control = ND_XHCI_TRB_TYPE(ND_XHCI_TRB_LINK) | ND_XHCI_TRB_TC | ring->cycle;
		ring->enqueue = 0;
		ring->cycle ^= 1;
	}
	return pa;
}

// Processes events until *done, or the time is up. Everything else that
// arrives meanwhile (keyboard reports, port changes) is handled too.
bool
NeoDarwinXHCI::waitFor(const bool *done, uint32_t timeoutMs)
{
	uint64_t deadline = deadlineAfterMs(timeoutMs);
	for (;;) {
		processEvents();
		if (*done) {
			return true;
		}
		if (mach_absolute_time() > deadline) {
			return false;
		}
		IOSleep(1);
	}
}

IOReturn
NeoDarwinXHCI::runCommand(uint32_t control, uint64_t param, uint8_t *slotOut)
{
	commandDone = false;
	commandCode = 0;
	commandSlot = 0;
	commandPA = push(&command, param, 0, control);
	dma.toDevice(command.trb, command.size * sizeof(struct nd_xhci_trb));
	ringDoorbell(0, 0);
	if (!waitFor(&commandDone, kCommandTimeoutMs)) {
		IOLog("NeoDarwinXHCI: %s: command %u timed out (USBSTS 0x%x, CRCR 0x%x)\n", where,
		    ND_XHCI_TRB_GET_TYPE(control), op32(ND_XHCI_USBSTS), op32(ND_XHCI_CRCR));
		return kIOReturnTimeout;
	}
	if (slotOut != NULL) {
		*slotOut = commandSlot;
	}
	return commandCode == ND_XHCI_CC_SUCCESS ? kIOReturnSuccess : kIOReturnIOError;
}

void
NeoDarwinXHCI::scheduleScan(uint32_t ms)
{
	scanTimer->setTimeoutMS(ms);
}

void
NeoDarwinXHCI::markPortChanged(uint32_t port)
{
	if (port >= 1 && port <= kMaxPorts) {
		portChanged[port / 32] |= 1u << (port % 32);
	}
}

uint32_t
NeoDarwinXHCI::processEvents(void)
{
	uint32_t n = 0;
	if (events == NULL) {
		return 0;
	}
	for (;;) {
		volatile struct nd_xhci_trb *e = &events[eventDequeue];
		dma.fromDevice(e, sizeof(*e));
		uint32_t control = e->control;
		if ((control & ND_XHCI_TRB_CYCLE) != eventCycle) {
			break;
		}
		__builtin_arm_dmb(0x9);         // DMB ISHLD: the rest of the TRB after its cycle bit
		uint64_t param = e->param;
		uint32_t status = e->status;
		if (++eventDequeue == kEventTRBs) {
			eventDequeue = 0;
			eventCycle ^= 1;
		}
		n++;
		eventCount++;
		handleEvent(param, status, control);
	}
	if (n != 0) {
		wr64(rtBase + ND_XHCI_IR(0) + ND_XHCI_IR_ERDP,
		    (eventsPA + (uint64_t)eventDequeue * sizeof(struct nd_xhci_trb)) | ND_XHCI_ERDP_EHB);
	}
	return n;
}

void
NeoDarwinXHCI::handleEvent(uint64_t param, uint32_t status, uint32_t control)
{
	uint8_t code = (uint8_t)ND_XHCI_TRB_CODE(status);
	switch (ND_XHCI_TRB_GET_TYPE(control)) {
	case ND_XHCI_TRB_COMMAND_COMPLETION:
		if (param == commandPA) {
			commandCode = code;
			commandSlot = (uint8_t)ND_XHCI_TRB_GET_SLOT(control);
			commandDone = true;
		}
		break;
	case ND_XHCI_TRB_PORT_STATUS_CHANGE:
		markPortChanged((uint32_t)(param >> 24) & 0xff);
		scheduleScan(10);
		break;
	case ND_XHCI_TRB_TRANSFER_EVENT: {
		uint32_t slot = ND_XHCI_TRB_GET_SLOT(control);
		uint32_t dci = ND_XHCI_TRB_GET_EP(control);
		Device *d = slot <= kMaxSlots ? devices[slot] : NULL;
		if (d == NULL) {
			break;
		}
		if (dci == 1) {
			if (param == d->ctrlStatusPA || (code != ND_XHCI_CC_SUCCESS && code != ND_XHCI_CC_SHORT_PACKET)) {
				d->ctrlCode = code;
				d->ctrlDone = true;
			} else if (code == ND_XHCI_CC_SHORT_PACKET) {
				d->ctrlResidue = ND_XHCI_TRB_RESIDUE(status);
			}
			break;
		}
		if (dci != d->intDci || param != d->intTrbPA) {
			break;
		}
		d->intQueued = false;
		if (code == ND_XHCI_CC_SUCCESS || code == ND_XHCI_CC_SHORT_PACKET) {
			uint32_t requested = d->intMps < kCtrlBufferBytes ? d->intMps : kCtrlBufferBytes;
			uint32_t residue = ND_XHCI_TRB_RESIDUE(status);
			uint32_t length = residue <= requested ? requested - residue : 0;
			const uint8_t *buffer = d->base + kDevIntBuffer;
			dma.fromDevice(buffer, requested);
			d->intErrors = 0;
			if (d->function == kKeyboard) {
				keyboardReport(d, buffer, length);
				queueInterrupt(d);
			} else if (d->function == kHub) {
				uint32_t bits = 0;
				for (uint32_t i = 0; i < length && i < 4; i++) {
					bits |= (uint32_t)buffer[i] << (8 * i);
				}
				d->hubChanged |= bits;
				d->hubScan = true;
				scheduleScan(1);        // the scan requeues the status transfer
			}
		} else if (code >= ND_XHCI_CC_STOPPED && code <= ND_XHCI_CC_STOPPED_SHORT_PACKET) {
			// stopped by a command: nothing to do
		} else {
			d->intErrors++;
			d->intHalted = true;
			scheduleScan(10);
		}
		break;
	}
	case ND_XHCI_TRB_HOST_CONTROLLER:
		IOLog("NeoDarwinXHCI: %s: host controller event: %s (code %u)\n", where, completionName(code), code);
		break;
	default:
		break;
	}
}

// MARK: - Contexts and control transfers

volatile uint32_t *
NeoDarwinXHCI::inputContext(Device *d, uint32_t index) const
{
	return (volatile uint32_t *)(d->base + kDevInputCtx + index * contextBytes);
}

volatile uint32_t *
NeoDarwinXHCI::outputContext(Device *d, uint32_t index) const
{
	return (volatile uint32_t *)(d->base + kDevOutputCtx + index * contextBytes);
}

// The input context's slot context (index 1) from what the driver knows of
// the device: route, speed, root port, the transaction translator, and for
// a hub its ports and TT think time.
void
NeoDarwinXHCI::fillSlotContext(Device *d, uint32_t entries)
{
	volatile uint32_t *s = inputContext(d, 1);
	bool hub = d->function == kHub && d->hubPorts != 0;
	s[0] = ND_XHCI_SLOT_ROUTE(d->route) | ND_XHCI_SLOT_SPEED(d->speed) | ND_XHCI_SLOT_ENTRIES(entries) |
	    (hub ? ND_XHCI_SLOT_HUB : 0);
	s[1] = ND_XHCI_SLOT_ROOT_PORT(d->rootPort) | (hub ? ND_XHCI_SLOT_NUM_PORTS(d->hubPorts) : 0);
	s[2] = ND_XHCI_SLOT_TT_SLOT(d->ttSlot) | ND_XHCI_SLOT_TT_PORT(d->ttPort) |
	    (hub && d->speed == ND_XHCI_SPEED_HIGH ? ND_XHCI_SLOT_TTT(d->hubTTT) : 0);
	s[3] = 0;
}

IOReturn
NeoDarwinXHCI::control(Device *d, uint8_t requestType, uint8_t request, uint16_t value, uint16_t index,
    uint16_t length, uint32_t *actual)
{
	bool in = (requestType & 0x80) != 0;
	uint8_t *buffer = d->base + kDevCtrlBuffer;
	if (length > kCtrlBufferBytes) {
		return kIOReturnBadArgument;
	}
	if (length != 0) {
		// Clean (OUT data) or clean and invalidate (IN: no dirty line may
		// later overwrite what the device writes).
		dma.toDevice(buffer, length);
	}
	reserve(&d->ep0, 3);
	uint32_t first = d->ep0.enqueue;
	uint64_t setup = (uint64_t)requestType | ((uint64_t)request << 8) | ((uint64_t)value << 16) |
	    ((uint64_t)index << 32) | ((uint64_t)length << 48);
	uint32_t trt = length == 0 ? 0 : (in ? 3 : 2);
	push(&d->ep0, setup, 8, ND_XHCI_TRB_TYPE(ND_XHCI_TRB_SETUP) | ND_XHCI_TRB_IDT | ND_XHCI_TRB_TRT(trt));
	// The Setup TRB stays the software's until the TD is complete.
	d->ep0.trb[first].control ^= ND_XHCI_TRB_CYCLE;
	if (length != 0) {
		push(&d->ep0, d->pa + kDevCtrlBuffer, length,
		    ND_XHCI_TRB_TYPE(ND_XHCI_TRB_DATA) | ND_XHCI_TRB_ISP | (in ? ND_XHCI_TRB_DIR_IN : 0));
	}
	bool statusIn = length == 0 || !in;
	d->ctrlDone = false;
	d->ctrlCode = 0;
	d->ctrlResidue = 0;
	d->ctrlStatusPA = push(&d->ep0, 0, 0,
	    ND_XHCI_TRB_TYPE(ND_XHCI_TRB_STATUS) | ND_XHCI_TRB_IOC | (statusIn ? ND_XHCI_TRB_DIR_IN : 0));
	dma.toDevice(d->ep0.trb, d->ep0.size * sizeof(struct nd_xhci_trb));
	d->ep0.trb[first].control ^= ND_XHCI_TRB_CYCLE;
	dma.toDevice(&d->ep0.trb[first], sizeof(struct nd_xhci_trb));
	ringDoorbell(d->slot, 1);
	if (!waitFor(&d->ctrlDone, kControlTimeoutMs)) {
		IOLog("NeoDarwinXHCI: %s: %s: request 0x%02x/0x%02x timed out\n", where, d->path, requestType, request);
		recoverEndpoint(d, 1, &d->ep0);
		return kIOReturnTimeout;
	}
	if (in && length != 0) {
		dma.fromDevice(buffer, length);
	}
	if (d->ctrlCode != ND_XHCI_CC_SUCCESS && d->ctrlCode != ND_XHCI_CC_SHORT_PACKET) {
		recoverEndpoint(d, 1, &d->ep0);
		return d->ctrlCode == ND_XHCI_CC_STALL ? kIOReturnUnsupported : kIOReturnIOError;
	}
	if (actual != NULL) {
		*actual = d->ctrlResidue <= length ? length - d->ctrlResidue : 0;
	}
	return kIOReturnSuccess;
}

// A halted endpoint (a stall, or errors) is reset; a running one (a
// timeout) stopped; then its dequeue pointer is moved to the ring's
// enqueue pointer, past the failed TD (§4.6.8, §4.6.9, §4.6.10).
void
NeoDarwinXHCI::recoverEndpoint(Device *d, uint32_t dci, Ring *ring)
{
	volatile uint32_t *ep = outputContext(d, dci);
	dma.fromDevice(ep, contextBytes);
	uint32_t state = ND_XHCI_EP_STATE(ep[0]);
	if (state == 2) {               // Halted
		runCommand(ND_XHCI_TRB_TYPE(ND_XHCI_TRB_RESET_ENDPOINT) | ND_XHCI_TRB_SLOT(d->slot) | ND_XHCI_TRB_EP(dci), 0, NULL);
	} else if (state == 1) {        // Running
		runCommand(ND_XHCI_TRB_TYPE(ND_XHCI_TRB_STOP_ENDPOINT) | ND_XHCI_TRB_SLOT(d->slot) | ND_XHCI_TRB_EP(dci), 0, NULL);
	}
	runCommand(ND_XHCI_TRB_TYPE(ND_XHCI_TRB_SET_TR_DEQUEUE) | ND_XHCI_TRB_SLOT(d->slot) | ND_XHCI_TRB_EP(dci),
	    (ring->pa + (uint64_t)ring->enqueue * sizeof(struct nd_xhci_trb)) | ring->cycle, NULL);
}

// MARK: - Ports and enumeration

void
NeoDarwinXHCI::scanFired(OSObject *owner, IOTimerEventSource *sender)
{
	(void)sender;
	NeoDarwinXHCI *self = static_cast<NeoDarwinXHCI *>(owner);
	if (!self->running) {
		return;
	}
	self->processEvents();
	for (uint32_t w = 0; w < sizeof(self->portChanged) / sizeof(self->portChanged[0]); w++) {
		while (self->portChanged[w] != 0) {
			uint32_t bit = (uint32_t)__builtin_ctz(self->portChanged[w]);
			self->portChanged[w] &= ~(1u << bit);
			uint32_t port = w * 32 + bit;
			if (port >= 1 && port <= self->ports) {
				self->handleRootPort(port);
			}
		}
	}
	for (uint32_t s = 1; s <= kMaxSlots; s++) {
		Device *d = self->devices[s];
		if (d == NULL) {
			continue;
		}
		if (d->intHalted) {
			d->intHalted = false;
			self->recoverEndpoint(d, d->intDci, &d->intr);
			if (d->intErrors < kMaxIntErrors) {
				if (d->function == kHub) {
					d->hubScan = true;      // a status change may have been lost
					d->hubChanged = 0xfffffffe;
				} else {
					self->queueInterrupt(d);
				}
			} else {
				IOLog("NeoDarwinXHCI: %s: %s: slot %u: interrupt endpoint keeps failing; given up\n", self->where,
				    d->path, d->slot);
			}
		}
		if (d->function == kHub && d->hubScan && self->devices[s] == d) {
			self->scanHub(d);
		}
	}
}

// USB 2 port reset (§4.3.1): PR, then PRC with PED set.
bool
NeoDarwinXHCI::resetRootPort(uint32_t port, uint32_t *status)
{
	uint32_t sc = portsc(port);
	setPortsc(port, nd_xhci_portsc_neutral(sc) | ND_XHCI_PORTSC_PR);
	uint32_t ms;
	for (ms = 0; ms < 500; ms += 5) {
		IOSleep(5);
		sc = portsc(port);
		if ((sc & ND_XHCI_PORTSC_PRC) && !(sc & ND_XHCI_PORTSC_PR)) {
			break;
		}
	}
	setPortsc(port, nd_xhci_portsc_neutral(sc) | (sc & ND_XHCI_PORTSC_CHANGES));
	*status = sc;
	if (!(sc & ND_XHCI_PORTSC_PED) || !(sc & ND_XHCI_PORTSC_CCS)) {
		IOLog("NeoDarwinXHCI: %s: port %u: reset did not enable it (PORTSC 0x%08x)\n", where, port, sc);
		return false;
	}
	IOSleep(10);                    // TRSTRCY
	return true;
}

void
NeoDarwinXHCI::handleRootPort(uint32_t port)
{
	uint32_t sc = portsc(port);
	if (sc == 0xffffffff) {
		return;
	}
	uint32_t changes = sc & ND_XHCI_PORTSC_CHANGES;
	if (changes != 0) {
		setPortsc(port, nd_xhci_portsc_neutral(sc) | changes);
	}
	bool connected = (sc & ND_XHCI_PORTSC_CCS) != 0;
	uint8_t slot = rootDevice[port];
	if (slot != 0 && (!connected || (changes & ND_XHCI_PORTSC_CSC))) {
		detach(slot, connected ? "reconnected" : "disconnected");
		slot = 0;
	}
	if (!connected || slot != 0) {
		return;
	}
	if (portMajor[port] >= 3) {
		// USB 3 ports train and enable by themselves.
		if (!(sc & ND_XHCI_PORTSC_PED)) {
			IOLog("NeoDarwinXHCI: %s: port %u: USB 3 device not enabled (link state %u)\n", where, port,
			    ND_XHCI_PORTSC_PLS(sc));
			return;
		}
	} else if (!resetRootPort(port, &sc)) {
		return;
	}
	enumerate(NULL, (uint8_t)port, (uint8_t)ND_XHCI_PORTSC_SPEED(sc), (uint8_t)port);
}

// Address a device, read its descriptors, configure it and start the
// function the driver drives. Returns the device, or NULL (the slot freed).
NeoDarwinXHCI::Device *
NeoDarwinXHCI::enumerate(Device *parent, uint8_t port, uint8_t speed, uint8_t rootPort)
{
	char path[24];
	if (parent == NULL) {
		snprintf(path, sizeof(path), "port %u", port);
	} else {
		snprintf(path, sizeof(path), "%s.%u", parent->path, port);
	}
	uint8_t slot = 0;
	IOReturn ret = runCommand(ND_XHCI_TRB_TYPE(ND_XHCI_TRB_ENABLE_SLOT), 0, &slot);
	if (ret != kIOReturnSuccess || slot == 0 || slot > slotsEnabled || devices[slot] != NULL) {
		IOLog("NeoDarwinXHCI: %s: %s: Enable Slot failed: %s\n", where, path, completionName(commandCode));
		return NULL;
	}
	Device *d = (Device *)IOMallocZero(sizeof(Device));
	IOBufferMemoryDescriptor *memory = d != NULL ? dma.allocate(kDevBytes) : NULL;
	if (memory == NULL) {
		IOLog("NeoDarwinXHCI: %s: %s: no memory for slot %u\n", where, path, slot);
		if (d != NULL) {
			IOFree(d, sizeof(Device));
		}
		runCommand(ND_XHCI_TRB_TYPE(ND_XHCI_TRB_DISABLE_SLOT) | ND_XHCI_TRB_SLOT(slot), 0, NULL);
		return NULL;
	}
	d->memory = memory;
	d->base = (uint8_t *)memory->getBytesNoCopy();
	d->pa = NDStorageDMA::physical(memory);
	d->slot = slot;
	d->speed = speed;
	d->rootPort = rootPort;
	d->parentSlot = parent != NULL ? parent->slot : 0;
	d->parentPort = port;
	d->depth = parent != NULL ? (uint8_t)(parent->depth + 1) : 0;
	d->route = parent != NULL ? nd_xhci_route(parent->route, parent->depth, port) : 0;
	if (parent != NULL && (speed == ND_XHCI_SPEED_LOW || speed == ND_XHCI_SPEED_FULL)) {
		// The nearest high-speed hub's transaction translator.
		if (parent->speed == ND_XHCI_SPEED_HIGH) {
			d->ttSlot = parent->slot;
			d->ttPort = port;
		} else {
			d->ttSlot = parent->ttSlot;
			d->ttPort = parent->ttPort;
		}
	}
	d->mps0 = nd_xhci_default_mps0(speed);
	strlcpy(d->path, path, sizeof(d->path));
	initRing(&d->ep0, d->base + kDevEP0Ring, d->pa + kDevEP0Ring, kEP0RingTRBs);
	initRing(&d->intr, d->base + kDevIntRing, d->pa + kDevIntRing, kIntRingTRBs);
	devices[slot] = d;
	if (parent != NULL) {
		parent->children[port] = slot;
	} else {
		rootDevice[port] = slot;
	}
	dcbaa[slot] = d->pa + kDevOutputCtx;
	dma.toDevice(&dcbaa[slot], sizeof(uint64_t));

	// Address Device (§4.3.3): slot and EP0 contexts; the controller sends
	// SET_ADDRESS.
	bzero(d->base + kDevInputCtx, 33 * contextBytes);
	inputContext(d, 0)[ND_XHCI_ICC_ADD] = 0x3;
	fillSlotContext(d, 1);
	volatile uint32_t *ep0 = inputContext(d, 2);
	ep0[1] = ND_XHCI_EP_CERR(3) | ND_XHCI_EP_TYPE(ND_XHCI_EP_CONTROL) | ND_XHCI_EP_MPS(d->mps0);
	ep0[2] = (uint32_t)d->ep0.pa | (uint32_t)ND_XHCI_EP_DCS;
	ep0[3] = (uint32_t)(d->ep0.pa >> 32);
	ep0[4] = ND_XHCI_EP_AVG_TRB(8);
	dma.toDevice(d->base + kDevInputCtx, 33 * contextBytes);
	ret = runCommand(ND_XHCI_TRB_TYPE(ND_XHCI_TRB_ADDRESS_DEVICE) | ND_XHCI_TRB_SLOT(slot), d->pa + kDevInputCtx, NULL);
	if (ret != kIOReturnSuccess) {
		IOLog("NeoDarwinXHCI: %s: %s: %s device: Address Device failed: %s\n", where, path, nd_xhci_speed_name(speed),
		    completionName(commandCode));
		detach(slot, NULL);
		return NULL;
	}
	IOSleep(2);                     // SET_ADDRESS recovery

	uint8_t *buffer = d->base + kDevCtrlBuffer;
	uint32_t actual = 0;
	ret = control(d, ND_USB_RT_DEVICE_IN, ND_USB_REQ_GET_DESCRIPTOR, ND_USB_DT_DEVICE << 8, 0, 8, &actual);
	if (ret != kIOReturnSuccess || actual < 8) {
		IOLog("NeoDarwinXHCI: %s: %s: %s device on slot %u: no device descriptor (0x%x)\n", where, path,
		    nd_xhci_speed_name(speed), slot, ret);
		detach(slot, NULL);
		return NULL;
	}
	uint16_t mps0 = nd_xhci_mps0_from_descriptor(speed, buffer[7]);
	if (mps0 != d->mps0) {
		// Evaluate Context: EP0's packet size (§4.6.7).
		d->mps0 = mps0;
		bzero(d->base + kDevInputCtx, 33 * contextBytes);
		inputContext(d, 0)[ND_XHCI_ICC_ADD] = 0x2;
		fillSlotContext(d, 1);
		ep0[1] = ND_XHCI_EP_CERR(3) | ND_XHCI_EP_TYPE(ND_XHCI_EP_CONTROL) | ND_XHCI_EP_MPS(d->mps0);
		ep0[2] = (uint32_t)d->ep0.pa | (uint32_t)ND_XHCI_EP_DCS;
		ep0[3] = (uint32_t)(d->ep0.pa >> 32);
		ep0[4] = ND_XHCI_EP_AVG_TRB(8);
		dma.toDevice(d->base + kDevInputCtx, 33 * contextBytes);
		if (runCommand(ND_XHCI_TRB_TYPE(ND_XHCI_TRB_EVALUATE_CONTEXT) | ND_XHCI_TRB_SLOT(slot), d->pa + kDevInputCtx,
		    NULL) != kIOReturnSuccess) {
			IOLog("NeoDarwinXHCI: %s: %s: Evaluate Context failed: %s\n", where, path, completionName(commandCode));
			detach(slot, NULL);
			return NULL;
		}
	}
	struct nd_usb_device_desc dev;
	ret = control(d, ND_USB_RT_DEVICE_IN, ND_USB_REQ_GET_DESCRIPTOR, ND_USB_DT_DEVICE << 8, 0, 18, &actual);
	if (ret != kIOReturnSuccess || !nd_usb_parse_device(buffer, actual, &dev)) {
		IOLog("NeoDarwinXHCI: %s: %s: slot %u: bad device descriptor\n", where, path, slot);
		detach(slot, NULL);
		return NULL;
	}
	d->vendor = dev.idVendor;
	d->product = dev.idProduct;
	if (dev.iProduct != 0 &&
	    control(d, ND_USB_RT_DEVICE_IN, ND_USB_REQ_GET_DESCRIPTOR, (uint16_t)(ND_USB_DT_STRING << 8 | dev.iProduct),
	    0x0409, 64, &actual) == kIOReturnSuccess) {
		nd_usb_string_ascii(buffer, actual, d->name, sizeof(d->name));
	}
	struct nd_usb_choice choice = {};
	ret = control(d, ND_USB_RT_DEVICE_IN, ND_USB_REQ_GET_DESCRIPTOR, ND_USB_DT_CONFIG << 8, 0, 9, &actual);
	uint16_t total = ret == kIOReturnSuccess && actual >= 4 ? nd_usb_le16(buffer + 2) : 0;
	if (total >= 9) {
		ret = control(d, ND_USB_RT_DEVICE_IN, ND_USB_REQ_GET_DESCRIPTOR, ND_USB_DT_CONFIG << 8, 0,
		    total < kCtrlBufferBytes ? total : kCtrlBufferBytes, &actual);
	}
	if (total < 9 || ret != kIOReturnSuccess || !nd_usb_parse_config(buffer, actual, dev.bDeviceClass, &choice)) {
		IOLog("NeoDarwinXHCI: %s: %s: slot %u: %04x:%04x: bad configuration descriptor\n", where, path, slot,
		    dev.idVendor, dev.idProduct);
		return d;                       // addressed, not driven
	}
	IOLog("NeoDarwinXHCI: %s: %s: %s device %04x:%04x \"%s\", USB %x.%02x, on slot %u%s\n", where, path,
	    nd_xhci_speed_name(speed), dev.idVendor, dev.idProduct, d->name, dev.bcdUSB_hi, dev.bcdUSB_lo, slot,
	    d->ttSlot != 0 ? " (through the TT of a high-speed hub)" : "");
	if (choice.function == ND_USB_FUNCTION_NONE) {
		if (choice.other) {
			IOLog("NeoDarwinXHCI: %s: %s: slot %u: no driver (interface class %u/%u/%u)\n", where, path, slot,
			    choice.otherClass, choice.otherSubClass, choice.otherProtocol);
		} else {
			IOLog("NeoDarwinXHCI: %s: %s: slot %u: no driver (device class %u)\n", where, path, slot, dev.bDeviceClass);
		}
		return d;
	}
	if (choice.function == ND_USB_FUNCTION_HUB && speed >= ND_XHCI_SPEED_SUPER) {
		IOLog("NeoDarwinXHCI: %s: %s: slot %u: SuperSpeed hub: not driven (its USB 2 hub, on the companion port, "
		    "serves keyboards)\n", where, path, slot);
		return d;
	}
	if (choice.function == ND_USB_FUNCTION_HUB && d->depth >= 4) {
		IOLog("NeoDarwinXHCI: %s: %s: slot %u: hub too deep in the tree; not driven\n", where, path, slot);
		return d;
	}
	ret = control(d, ND_USB_RT_DEVICE_OUT, ND_USB_REQ_SET_CONFIGURATION, choice.configurationValue, 0, 0, NULL);
	if (ret != kIOReturnSuccess) {
		IOLog("NeoDarwinXHCI: %s: %s: slot %u: SET_CONFIGURATION(%u) failed (0x%x)\n", where, path, slot,
		    choice.configurationValue, ret);
		return d;
	}
	d->iface = choice.interfaceNumber;
	d->intDci = (uint8_t)nd_xhci_dci(choice.endpointAddress);
	d->intMps = choice.maxPacketSize;
	d->maxBurst = choice.maxBurst;
	d->interval = nd_xhci_interrupt_interval(speed, choice.bInterval);
	if (choice.function == ND_USB_FUNCTION_KEYBOARD) {
		d->function = kKeyboard;
		startKeyboard(d);
	} else {
		d->function = kHub;
		startHub(d);
	}
	return d;
}

// Configure Endpoint (§4.6.6): the interrupt IN endpoint, and the slot
// context again (with a hub's fields).
bool
NeoDarwinXHCI::configureInterruptEndpoint(Device *d)
{
	bzero(d->base + kDevInputCtx, 33 * contextBytes);
	inputContext(d, 0)[ND_XHCI_ICC_ADD] = 1u | (1u << d->intDci);
	fillSlotContext(d, d->intDci);
	volatile uint32_t *ep = inputContext(d, d->intDci + 1);
	uint32_t esit = (uint32_t)d->intMps * (d->maxBurst + 1u);
	ep[0] = ND_XHCI_EP_INTERVAL(d->interval) | ND_XHCI_EP_ESIT_HI(esit);
	ep[1] = ND_XHCI_EP_CERR(3) | ND_XHCI_EP_TYPE(ND_XHCI_EP_INTERRUPT_IN) | ND_XHCI_EP_MAX_BURST(d->maxBurst) |
	    ND_XHCI_EP_MPS(d->intMps);
	ep[2] = (uint32_t)d->intr.pa | (uint32_t)ND_XHCI_EP_DCS;
	ep[3] = (uint32_t)(d->intr.pa >> 32);
	ep[4] = ND_XHCI_EP_AVG_TRB(d->intMps) | ND_XHCI_EP_ESIT_LO(esit);
	dma.toDevice(d->base + kDevInputCtx, 33 * contextBytes);
	IOReturn ret = runCommand(ND_XHCI_TRB_TYPE(ND_XHCI_TRB_CONFIGURE_ENDPOINT) | ND_XHCI_TRB_SLOT(d->slot),
	    d->pa + kDevInputCtx, NULL);
	if (ret != kIOReturnSuccess) {
		IOLog("NeoDarwinXHCI: %s: %s: slot %u: Configure Endpoint failed: %s\n", where, d->path, d->slot,
		    completionName(commandCode));
		return false;
	}
	return true;
}

void
NeoDarwinXHCI::queueInterrupt(Device *d)
{
	if (d->intQueued || !running) {
		return;
	}
	uint32_t length = d->intMps < kCtrlBufferBytes ? d->intMps : kCtrlBufferBytes;
	dma.toDevice(d->base + kDevIntBuffer, length);  // no dirty line over what the device writes
	reserve(&d->intr, 1);
	d->intTrbPA = push(&d->intr, d->pa + kDevIntBuffer, length,
	    ND_XHCI_TRB_TYPE(ND_XHCI_TRB_NORMAL) | ND_XHCI_TRB_IOC | ND_XHCI_TRB_ISP);
	dma.toDevice(d->intr.trb, d->intr.size * sizeof(struct nd_xhci_trb));
	d->intQueued = true;
	ringDoorbell(d->slot, d->intDci);
}

// A HID boot keyboard (HID 1.11 §7.2.5, §7.2.4, appendix B): the boot
// protocol, reports only on change, and its interrupt endpoint.
void
NeoDarwinXHCI::startKeyboard(Device *d)
{
	if (!configureInterruptEndpoint(d)) {
		d->function = kNone;
		return;
	}
	IOReturn proto = control(d, ND_USB_RT_CLASS_INTERFACE_OUT, ND_HID_REQ_SET_PROTOCOL, 0, d->iface, 0, NULL);
	IOReturn idle = control(d, ND_USB_RT_CLASS_INTERFACE_OUT, ND_HID_REQ_SET_IDLE, 0, d->iface, 0, NULL);
	nd_hid_kbd_init(&d->kbd);
	keyboards++;
	setProperty("keyboards", keyboards, 32);
	IOLog("NeoDarwinXHCI: %s: %s: slot %u: HID boot keyboard (interface %u, endpoint %u IN, %u bytes every %u us); "
	    "SET_PROTOCOL %s, SET_IDLE %s: console input\n", where, d->path, d->slot, d->iface, d->intDci / 2, d->intMps,
	    nd_xhci_interval_us(d->interval), proto == kIOReturnSuccess ? "boot" : "failed",
	    idle == kIOReturnSuccess ? "0" : "not supported");
	queueInterrupt(d);
}

// A USB 2 hub (USB 2.0 §11.23, §11.24): its descriptor, the hub fields of
// its slot context, power on every port, its status change endpoint, then
// every port looked at.
void
NeoDarwinXHCI::startHub(Device *d)
{
	uint32_t actual = 0;
	struct nd_usb_hub_desc hub;
	IOReturn ret = control(d, ND_USB_RT_CLASS_DEVICE_IN, ND_USB_REQ_GET_DESCRIPTOR, ND_USB_DT_HUB << 8, 0, 16, &actual);
	if (ret != kIOReturnSuccess || !nd_usb_parse_hub(d->base + kDevCtrlBuffer, actual, &hub) || hub.ports == 0) {
		IOLog("NeoDarwinXHCI: %s: %s: slot %u: no hub descriptor (0x%x)\n", where, d->path, d->slot, ret);
		d->function = kNone;
		return;
	}
	d->hubPorts = hub.ports > kMaxHubPorts ? (uint8_t)kMaxHubPorts : hub.ports;
	d->hubTTT = (uint8_t)((hub.characteristics >> 5) & 3);
	if (!configureInterruptEndpoint(d)) {
		d->function = kNone;
		return;
	}
	for (uint32_t p = 1; p <= d->hubPorts; p++) {
		control(d, ND_USB_RT_CLASS_OTHER_OUT, ND_USB_REQ_SET_FEATURE, ND_HUB_PORT_POWER, (uint16_t)p, 0, NULL);
	}
	uint32_t wait = hub.powerOnToGood2ms * 2u;
	IOSleep(wait < 100 ? 100 : wait);
	IOLog("NeoDarwinXHCI: %s: %s: slot %u: hub, %u ports%s%s; ports powered\n", where, d->path, d->slot, hub.ports,
	    hub.ports > kMaxHubPorts ? " (the first 15 used)" : "",
	    d->speed == ND_XHCI_SPEED_HIGH ? ", high-speed, single TT" : "");
	d->hubChanged = 0xfffffffe;
	d->hubScan = true;
	scanHub(d);
}

// The ports of a hub whose status changed: connection changes acknowledged,
// devices gone detached, new ones reset and enumerated.
void
NeoDarwinXHCI::scanHub(Device *h)
{
	uint8_t hubSlot = h->slot;
	uint32_t changed = h->hubChanged;
	h->hubChanged = 0;
	h->hubScan = false;
	for (uint32_t p = 1; p <= h->hubPorts && devices[hubSlot] == h; p++) {
		if ((changed & (1u << p)) == 0) {
			continue;
		}
		uint8_t *buffer = h->base + kDevCtrlBuffer;
		uint32_t actual = 0;
		if (control(h, ND_USB_RT_CLASS_OTHER_IN, ND_USB_REQ_GET_STATUS, 0, (uint16_t)p, 4, &actual) != kIOReturnSuccess ||
		    actual < 4) {
			continue;
		}
		uint16_t status = nd_usb_le16(buffer), change = nd_usb_le16(buffer + 2);
		static const struct { uint16_t bit; uint16_t feature; } clears[] = {
			{ ND_HUB_PC_CONNECTION, ND_HUB_C_PORT_CONNECTION }, { ND_HUB_PC_ENABLE, ND_HUB_C_PORT_ENABLE },
			{ ND_HUB_PC_SUSPEND, ND_HUB_C_PORT_SUSPEND }, { ND_HUB_PC_OVER_CURRENT, ND_HUB_C_PORT_OVER_CURRENT },
			{ ND_HUB_PC_RESET, ND_HUB_C_PORT_RESET },
		};
		for (const auto &c : clears) {
			if (change & c.bit) {
				control(h, ND_USB_RT_CLASS_OTHER_OUT, ND_USB_REQ_CLEAR_FEATURE, c.feature, (uint16_t)p, 0, NULL);
			}
		}
		bool connected = (status & ND_HUB_PS_CONNECTION) != 0;
		uint8_t child = h->children[p];
		if (child != 0 && (!connected || (change & ND_HUB_PC_CONNECTION))) {
			detach(child, connected ? "reconnected" : "disconnected");
			child = 0;
		}
		if (!connected || child != 0) {
			continue;
		}
		// Port reset (§11.24.2.13): C_PORT_RESET within 500 ms, enabled.
		control(h, ND_USB_RT_CLASS_OTHER_OUT, ND_USB_REQ_SET_FEATURE, ND_HUB_PORT_RESET, (uint16_t)p, 0, NULL);
		bool reset = false;
		for (uint32_t ms = 0; ms < 500 && !reset; ms += 10) {
			IOSleep(10);
			if (control(h, ND_USB_RT_CLASS_OTHER_IN, ND_USB_REQ_GET_STATUS, 0, (uint16_t)p, 4, &actual) == kIOReturnSuccess &&
			    actual >= 4) {
				status = nd_usb_le16(buffer);
				reset = (nd_usb_le16(buffer + 2) & ND_HUB_PC_RESET) != 0 && (status & ND_HUB_PS_RESET) == 0;
			}
		}
		control(h, ND_USB_RT_CLASS_OTHER_OUT, ND_USB_REQ_CLEAR_FEATURE, ND_HUB_C_PORT_RESET, (uint16_t)p, 0, NULL);
		if (!reset || (status & ND_HUB_PS_ENABLE) == 0) {
			IOLog("NeoDarwinXHCI: %s: %s.%u: reset did not enable it (status 0x%04x)\n", where, h->path, p, status);
			continue;
		}
		IOSleep(10);                    // TRSTRCY
		uint8_t speed = (status & ND_HUB_PS_LOW_SPEED) ? ND_XHCI_SPEED_LOW :
		    ((status & ND_HUB_PS_HIGH_SPEED) ? ND_XHCI_SPEED_HIGH : ND_XHCI_SPEED_FULL);
		enumerate(h, (uint8_t)p, speed, h->rootPort);
	}
	if (devices[hubSlot] == h && h->function == kHub) {
		queueInterrupt(h);
	}
}

void
NeoDarwinXHCI::detach(uint8_t slot, const char *why)
{
	Device *d = slot <= kMaxSlots ? devices[slot] : NULL;
	if (d == NULL) {
		return;
	}
	for (uint32_t p = 1; p <= kMaxHubPorts; p++) {
		if (d->children[p] != 0) {
			detach(d->children[p], "its hub is gone");
		}
	}
	if (repeatSlot == slot) {
		repeatTimer->cancelTimeout();
		repeatSlot = 0;
	}
	runCommand(ND_XHCI_TRB_TYPE(ND_XHCI_TRB_DISABLE_SLOT) | ND_XHCI_TRB_SLOT(slot), 0, NULL);
	dcbaa[slot] = 0;
	dma.toDevice(&dcbaa[slot], sizeof(uint64_t));
	Device *parent = d->parentSlot != 0 ? devices[d->parentSlot] : NULL;
	if (parent != NULL) {
		parent->children[d->parentPort] = 0;
	} else if (d->parentSlot == 0 && rootDevice[d->parentPort] == slot) {
		rootDevice[d->parentPort] = 0;
	}
	if (d->function == kKeyboard && keyboards != 0) {
		keyboards--;
		setProperty("keyboards", keyboards, 32);
	}
	if (why != NULL) {
		IOLog("NeoDarwinXHCI: %s: %s: slot %u %s\n", where, d->path, slot, why);
	}
	devices[slot] = NULL;
	freeDevice(d);
}

void
NeoDarwinXHCI::freeDevice(Device *d)
{
	if (d->memory != NULL) {
		d->memory->complete();
		d->memory->release();
	}
	IOFree(d, sizeof(Device));
}

// MARK: - The keyboard

void
NeoDarwinXHCI::consoleInput(const uint8_t *bytes, size_t count)
{
	// The console tty exists once BSD has initialised (kminit); input
	// before that has nowhere to go.
	if (km_tty[0] == NULL) {
		return;
	}
	for (size_t i = 0; i < count; i++) {
		cons_cinput((char)bytes[i]);
	}
}

void
NeoDarwinXHCI::keyboardReport(Device *d, const uint8_t *report, uint32_t length)
{
	if (!d->reported) {
		d->reported = true;
		IOLog("NeoDarwinXHCI: %s: %s: slot %u: first keyboard report (%u bytes, %s)\n", where, d->path, d->slot, length,
		    polling ? "polled" : "by interrupt");
	}
	uint8_t before = d->kbd.repeatKey;
	uint8_t out[64];
	size_t n = nd_hid_kbd_report(&d->kbd, report, length, out, sizeof(out));
	consoleInput(out, n);
	uint8_t after = d->kbd.repeatKey;
	if (after != 0 && after != before) {
		repeatSlot = d->slot;
		repeatTimer->setTimeoutMS(kRepeatDelayMs);
	} else if (after == 0 && repeatSlot == d->slot) {
		repeatTimer->cancelTimeout();
		repeatSlot = 0;
	}
}

void
NeoDarwinXHCI::repeatFired(OSObject *owner, IOTimerEventSource *sender)
{
	(void)sender;
	NeoDarwinXHCI *self = static_cast<NeoDarwinXHCI *>(owner);
	Device *d = self->repeatSlot != 0 ? self->devices[self->repeatSlot] : NULL;
	if (!self->running || d == NULL || d->function != kKeyboard || d->kbd.repeatKey == 0) {
		self->repeatSlot = 0;
		return;
	}
	uint8_t out[ND_HID_KBD_MAX_BYTES];
	size_t n = nd_hid_kbd_bytes(&d->kbd, d->kbd.repeatKey, out);
	consoleInput(out, n);
	self->repeatTimer->setTimeoutMS(kRepeatRateMs);
}

// MARK: - Stop

void
NeoDarwinXHCI::stop(IOService *provider)
{
	if (running) {
		running = false;
		opw32(ND_XHCI_USBCMD, op32(ND_XHCI_USBCMD) & ~(ND_XHCI_CMD_RS | ND_XHCI_CMD_INTE));
		for (uint32_t ms = 0; !(op32(ND_XHCI_USBSTS) & ND_XHCI_STS_HCH) && ms < 100; ms++) {
			IOSleep(1);
		}
	}
	if (interruptSource != NULL) {
		interruptSource->disable();
		workLoop->removeEventSource(interruptSource);
		OSSafeReleaseNULL(interruptSource);
	}
	IOTimerEventSource *timers[] = { pollTimer, scanTimer, repeatTimer };
	for (IOTimerEventSource *t : timers) {
		if (t != NULL) {
			t->cancelTimeout();
		}
	}
	super::stop(provider);
}

void
NeoDarwinXHCI::free(void)
{
	for (uint32_t s = 0; s <= kMaxSlots; s++) {
		if (devices[s] != NULL) {
			freeDevice(devices[s]);
			devices[s] = NULL;
		}
	}
	IOBufferMemoryDescriptor *memories[] = { ctlMemory, eventMemory, scratchMemory };
	for (IOBufferMemoryDescriptor *m : memories) {
		if (m != NULL) {
			m->complete();
			m->release();
		}
	}
	ctlMemory = eventMemory = scratchMemory = NULL;
	IOTimerEventSource *timers[] = { pollTimer, scanTimer, repeatTimer };
	for (IOTimerEventSource *t : timers) {
		if (t != NULL) {
			if (workLoop != NULL) {
				workLoop->removeEventSource(t);
			}
			t->release();
		}
	}
	pollTimer = scanTimer = repeatTimer = NULL;
	OSSafeReleaseNULL(interruptNub);
	OSSafeReleaseNULL(map);
	OSSafeReleaseNULL(workLoop);
	super::free();
}

IOWorkLoop *
NeoDarwinXHCI::getWorkLoop(void) const
{
	return workLoop;
}
