// SPDX-License-Identifier: BSD-2-Clause
// NeoDarwin-Language: expressibility: IOKit drivers are C++ IOService subclasses.
//
// NeoDarwin's console USB keyboard (docs/kernel/usb-console.md, roadmap
// P1-18): a minimal xHCI host controller driver, on PCI (class 0C0330) and
// on ACPI (PNP0D10, PNP0D15, and the host role of a PNP0CA1 USB Role
// Switch), that drives HID boot keyboards and the USB 2 hubs in front of
// them, and feeds the console like the serial keyboard. A bring-up aid:
// P3-07's USB stack (NDUSBFamily and HID dexts) replaces it.
#ifndef ND_XHCI_DRIVER_H
#define ND_XHCI_DRIVER_H

#include <IOKit/IOBufferMemoryDescriptor.h>
#include <IOKit/IOFilterInterruptEventSource.h>
#include <IOKit/IOInterruptEventSource.h>
#include <IOKit/IOService.h>
#include <IOKit/IOTimerEventSource.h>
#include <IOKit/IOWorkLoop.h>

#include "NeoDarwinStorageDMA.h"
#include "nd_hid_kbd.h"
#include "nd_xhci.h"

class IOPCIDevice;
class IOACPIPlatformDevice;

class NeoDarwinXHCI : public IOService
{
	OSDeclareDefaultStructors(NeoDarwinXHCI);

public:
	IOService *probe(IOService *provider, SInt32 *score) APPLE_KEXT_OVERRIDE;
	bool start(IOService *provider) APPLE_KEXT_OVERRIDE;
	void stop(IOService *provider) APPLE_KEXT_OVERRIDE;
	void free(void) APPLE_KEXT_OVERRIDE;
	IOWorkLoop *getWorkLoop(void) const APPLE_KEXT_OVERRIDE;

	static constexpr uint32_t kMaxSlots = 32;
	static constexpr uint32_t kMaxPorts = 255;
	static constexpr uint32_t kMaxHubPorts = 15;

private:
	// A producer ring (command or transfer): TRBs with a Link TRB at the end
	// back to the start, toggling the cycle bit.
	struct Ring {
		volatile struct nd_xhci_trb *trb;
		uint64_t pa;
		uint32_t size;          // TRBs, the Link TRB included
		uint32_t enqueue;
		uint32_t cycle;
	};

	enum Function : uint8_t { kNone = 0, kKeyboard, kHub };

	struct Device {
		uint8_t slot;
		uint8_t speed;
		uint8_t rootPort;
		uint8_t parentSlot;     // 0: on a root port
		uint8_t parentPort;     // the port on the parent hub, or the root port
		uint8_t depth;          // hubs between the root port and the device
		uint32_t route;
		uint8_t ttSlot, ttPort;
		uint16_t mps0;
		uint16_t vendor, product;
		char name[48];
		char path[24];          // "port 3" or "port 3.1" (a hub's port 1)
		Function function;
		IOBufferMemoryDescriptor *memory;
		uint8_t *base;
		uint64_t pa;
		Ring ep0, intr;
		// the interrupt IN endpoint
		uint8_t intDci, iface, interval, maxBurst;
		uint16_t intMps;
		bool intQueued, intHalted;
		uint8_t intErrors;
		uint64_t intTrbPA;
		// the control transfer in flight
		uint64_t ctrlStatusPA;
		bool ctrlDone;
		uint8_t ctrlCode;
		uint32_t ctrlResidue;
		// a hub
		uint8_t hubPorts, hubTTT;
		uint32_t hubChanged;    // ports whose status changed (bit n: port n)
		bool hubScan;
		uint8_t children[kMaxHubPorts + 1];
		// a keyboard
		struct nd_hid_kbd kbd;
		bool reported;
	};

	// Where in a device's DMA page each piece is. Output context first (the
	// controller writes it), then the CPU-written input context and rings,
	// then the buffers the controller writes into, each on its own cache
	// lines.
	static constexpr uint32_t kDevOutputCtx = 0x0000;       // 32 contexts of up to 64 bytes
	static constexpr uint32_t kDevInputCtx = 0x0800;        // 33 contexts
	static constexpr uint32_t kDevEP0Ring = 0x1100;         // 32 TRBs
	static constexpr uint32_t kDevIntRing = 0x1400;         // 16 TRBs
	static constexpr uint32_t kDevCtrlBuffer = 0x1800;      // 1 KiB
	static constexpr uint32_t kDevIntBuffer = 0x1c00;       // 1 KiB
	static constexpr uint32_t kDevBytes = 0x2000;
	static constexpr uint32_t kEP0RingTRBs = 32;
	static constexpr uint32_t kIntRingTRBs = 16;
	static constexpr uint32_t kCtrlBufferBytes = 1024;

	// The controller's CPU-written page: DCBAA, command ring, ERST, the
	// scratchpad array. The event ring has its own.
	static constexpr uint32_t kCtlDCBAA = 0x0000;
	static constexpr uint32_t kCtlCommandRing = 0x0800;     // 64 TRBs
	static constexpr uint32_t kCtlERST = 0x0c00;
	static constexpr uint32_t kCtlScratchArray = 0x1000;    // up to 1023 pointers
	static constexpr uint32_t kCtlBytes = 0x3000;
	static constexpr uint32_t kCommandTRBs = 64;
	static constexpr uint32_t kEventTRBs = 256;

	enum InterruptMode : uint8_t { kIntrNone = 0, kIntrMSIX, kIntrMSI, kIntrINTx, kIntrGSIV };

	// Provider and registers.
	IOPCIDevice *pci;
	IOACPIPlatformDevice *acpi;
	IOService *interruptNub;
	int interruptIndex;
	IOMemoryMap *map;
	volatile uint8_t *regs;
	uint64_t regsLength;
	uint32_t opBase, rtBase, dbBase;
	uint32_t hcs1, hcs2, hcc1;
	uint16_t version;
	uint8_t slotsEnabled, ports;
	uint32_t contextBytes;
	uint32_t scratchpads;
	uint32_t pageBytes;
	uint8_t portMajor[kMaxPorts + 1];
	uint8_t portMinor[kMaxPorts + 1];
	bool roleSwitch;
	char where[64];
	char legacyNote[96];
	char dwc3Note[96];

	// Memory.
	NDStorageDMA dma;
	IOBufferMemoryDescriptor *ctlMemory, *eventMemory, *scratchMemory;
	uint8_t *ctl;
	uint64_t ctlPA;
	volatile uint64_t *dcbaa;
	Ring command;
	volatile struct nd_xhci_trb *events;
	uint64_t eventsPA;
	uint32_t eventDequeue, eventCycle;

	// The command in flight (one at a time).
	uint64_t commandPA;
	bool commandDone;
	uint8_t commandCode, commandSlot;

	// Work loop, interrupts, timers.
	IOWorkLoop *workLoop;
	IOInterruptEventSource *interruptSource;
	IOTimerEventSource *pollTimer, *scanTimer, *repeatTimer;
	InterruptMode interruptMode;
	uint32_t lpi;
	bool polling, running, announcedInterrupt, announcedPoll;
	uint32_t interruptCount, eventCount;
	bool watchPending;
	uint32_t watchDequeue;

	// Devices and ports.
	Device *devices[kMaxSlots + 1];         // by slot ID
	uint8_t rootDevice[kMaxPorts + 1];      // the slot on each root port
	uint32_t portChanged[(kMaxPorts + 32) / 32];
	uint8_t repeatSlot;
	uint32_t keyboards;

	// Registers.
	uint32_t rd32(uint32_t offset) const;
	void wr32(uint32_t offset, uint32_t value);
	void wr64(uint32_t offset, uint64_t value);
	uint32_t op32(uint32_t offset) const { return rd32(opBase + offset); }
	void opw32(uint32_t offset, uint32_t value) { wr32(opBase + offset, value); }
	uint32_t portsc(uint32_t port) const { return op32(ND_XHCI_PORTSC(port)); }
	void setPortsc(uint32_t port, uint32_t value) { opw32(ND_XHCI_PORTSC(port), value); }
	void ringDoorbell(uint32_t slot, uint32_t target);

	// Bring-up.
	bool startController(IOService *provider);
	bool mapRegisters(IOService *provider);
	bool setUpACPI(void);
	void legacyHandoff(void);
	void readProtocols(void);
	bool haltAndReset(void);
	bool allocateController(void);
	bool setUpInterrupts(void);
	bool runController(void);
	void logController(void);

	// Rings and commands.
	void initRing(Ring *ring, volatile void *memory, uint64_t pa, uint32_t size);
	uint64_t push(Ring *ring, uint64_t param, uint32_t status, uint32_t control);
	void reserve(Ring *ring, uint32_t trbs);
	IOReturn runCommand(uint32_t control, uint64_t param, uint8_t *slotOut);
	bool waitFor(const bool *done, uint32_t timeoutMs);
	uint32_t processEvents(void);
	void handleEvent(uint64_t param, uint32_t status, uint32_t control);
	void markPortChanged(uint32_t port);
	void scheduleScan(uint32_t ms);

	// Devices.
	volatile uint32_t *inputContext(Device *d, uint32_t index) const;
	volatile uint32_t *outputContext(Device *d, uint32_t index) const;
	void fillSlotContext(Device *d, uint32_t entries);
	IOReturn control(Device *d, uint8_t requestType, uint8_t request, uint16_t value, uint16_t index,
	    uint16_t length, uint32_t *actual);
	void recoverEndpoint(Device *d, uint32_t dci, Ring *ring);
	void handleRootPort(uint32_t port);
	bool resetRootPort(uint32_t port, uint32_t *status);
	Device *enumerate(Device *parent, uint8_t port, uint8_t speed, uint8_t rootPort);
	bool configureInterruptEndpoint(Device *d);
	void startKeyboard(Device *d);
	void startHub(Device *d);
	void scanHub(Device *h);
	void queueInterrupt(Device *d);
	void detach(uint8_t slot, const char *why);
	void freeDevice(Device *d);

	// Keyboard.
	void keyboardReport(Device *d, const uint8_t *report, uint32_t length);
	static void consoleInput(const uint8_t *bytes, size_t count);

	// Event sources.
	static bool filter(OSObject *owner, IOFilterInterruptEventSource *source);
	static void interrupt(OSObject *owner, IOInterruptEventSource *source, int count);
	static void pollFired(OSObject *owner, IOTimerEventSource *sender);
	static void scanFired(OSObject *owner, IOTimerEventSource *sender);
	static void repeatFired(OSObject *owner, IOTimerEventSource *sender);
};

#endif
