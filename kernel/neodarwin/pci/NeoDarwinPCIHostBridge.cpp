// SPDX-License-Identifier: BSD-2-Clause
// NeoDarwin-Language: expressibility: IOKit classes are C++; this implements an IOPCIHostBridge over ECAM for IOPCIFamily.
//
// The ACPI PCI host bridge (NeoDarwinPCIHostBridge.h, docs/kernel/pci.md).

#include "NeoDarwinPCIHostBridge.h"
#include "nd_pci.h"
#include <IOKit/IODeviceMemory.h>
#include <IOKit/IOLib.h>
#include <kern/thread.h>

#define super IOPCIHostBridge
OSDefineMetaClassAndStructors(NeoDarwinPCIHostBridge, IOPCIHostBridge);

// _OSC for PCI host bridges (PCI Firmware 3.3 §4.5): the UUID
// 33DB4D5B-1FF7-401C-9657-7441C03DD766 as ToUUID lays it out.
static const UInt8 kPCIOSCUUID[16] = {
	0x5b, 0x4d, 0xdb, 0x33, 0xf7, 0x1f, 0x1c, 0x40, 0x96, 0x57, 0x74, 0x41, 0xc0, 0x3d, 0xd7, 0x66,
};
// Support field: extended configuration space, ASPM, clock PM, segments.
// No MSI yet (checkpoint 3 adds it with the ITS).
enum {
	kOSCSupportExtConfig = 0x01,
	kOSCSupportASPM      = 0x02,
	kOSCSupportClockPM   = 0x04,
	kOSCSupportSegments  = 0x08,
	kOSCSupportMSI       = 0x10,
};
// Control field: what IOPCIFamily does natively.
enum {
	kOSCControlHotplug   = 0x01,
	kOSCControlPME       = 0x04,
	kOSCControlAER       = 0x08,
	kOSCControlPCIeCap   = 0x10,
	kOSCControlLTR       = 0x20,
};
// Status (first dword of the result).
enum {
	kOSCStatusQuery      = 0x01,
	kOSCStatusFailure    = 0x02,
	kOSCStatusBadUUID    = 0x04,
	kOSCStatusBadRev     = 0x08,
	kOSCStatusMasked     = 0x10,
};

// The lowest I/O port a BAR is given (Linux's PCIBIOS_MIN_IO on arm64).
static const UInt64 kMinIOPort = 0x1000;

// Registry names of IORT node types (IORT E.f, table 4).
static const char *
iort_node_name(UInt8 type)
{
	switch (type) {
	case 0: return "ITS group";
	case 1: return "named component";
	case 2: return "root complex";
	case 3: return "SMMUv1/v2";
	case 4: return "SMMUv3";
	case 5: return "PMCG";
	default: return "unknown node";
	}
}

static UInt32
rd32(const UInt8 *p)
{
	UInt32 v;
	memcpy(&v, p, sizeof(v));
	return v;
}

static UInt64
rd64(const UInt8 *p)
{
	UInt64 v;
	memcpy(&v, p, sizeof(v));
	return v;
}

// ------------------------------------------------------------------------
// The ACPI side: _SEG, _BBN, bus range, ECAM, windows, _OSC, _CCA/IORT

bool
NeoDarwinPCIHostBridge::readHostBridge(IOACPIPlatformDevice *nub)
{
	OSString *p = OSDynamicCast(OSString, nub->getProperty("acpi-path"));
	strlcpy(path, p != NULL ? p->getCStringNoCopy() : nub->getName(), sizeof(path));

	UInt64 seg = 0, bbn = 0;
	nub->evaluateInteger("_SEG", &seg);
	nub->evaluateInteger("_BBN", &bbn);
	segment = (UInt16)seg;
	busFirst = (UInt8)bbn;
	busLast = 255;
	OSData *range = OSDynamicCast(OSData, nub->getProperty("acpi-bus-range"));
	if (range != NULL && range->getLength() == 2 * sizeof(UInt32)) {
		const UInt32 *r = (const UInt32 *)range->getBytesNoCopy();
		busFirst = (UInt8)r[0];
		busLast = (UInt8)(r[1] > 255 ? 255 : r[1]);
	}

	// ECAM: _CBA (the address of bus 0 of the segment), else the MCFG
	// allocation for this segment and bus. Register every MCFG entry of
	// the segment's bus range; the ACPI platform already registered them
	// all, before any AML ran.
	ecam = 0;
	if (nub->evaluateInteger("_CBA", &ecam) == kIOReturnSuccess && ecam != 0) {
		ecamSource = "_CBA";
	} else {
		ecam = 0;
		const OSData *mcfg = nub->getACPITableData("MCFG", 0);
		if (mcfg != NULL) {
			const UInt8 *t = (const UInt8 *)mcfg->getBytesNoCopy();
			for (unsigned int off = 44; off + 16 <= mcfg->getLength(); off += 16) {
				UInt16 s;
				memcpy(&s, t + off + 8, sizeof(s));
				if (s == segment && t[off + 10] <= busFirst && busFirst <= t[off + 11]) {
					ecam = rd64(t + off);
					// MCFG's range is the authority on which buses decode.
					if (t[off + 11] < busLast) {
						busLast = t[off + 11];
					}
					break;
				}
			}
		}
		ecamSource = "MCFG";
	}
	if (ecam == 0) {
		IOLog("NeoDarwinPCIHostBridge: %s: segment %u: no ECAM (_CBA or MCFG); not enumerated\n", path, segment);
		return false;
	}
	nd_pci_ecam_add(segment, busFirst, busLast, ecam);
	return true;
}

// PCIe native control (PCI Firmware 3.3 §4.5.1): a query, then the request
// for what the query granted, as Linux and FreeBSD do. A firmware whose _OSC
// fails (the Q8B's returns AE_AML_BUFFER_LIMIT under FreeBSD) grants
// nothing, and IOPCIFamily then leaves AER alone.
static IOReturn
osc_call(IOACPIPlatformDevice *nub, bool query, UInt32 support, UInt32 *control, UInt32 *status)
{
	UInt32 caps[3] = { query ? (UInt32)kOSCStatusQuery : 0u, support, *control };
	OSData *uuid = OSData::withBytes(kPCIOSCUUID, sizeof(kPCIOSCUUID));
	OSNumber *rev = OSNumber::withNumber(1ULL, 32);
	OSNumber *count = OSNumber::withNumber(3ULL, 32);
	OSData *buf = OSData::withBytes(caps, sizeof(caps));
	IOReturn ret = kIOReturnNoMemory;
	if (uuid && rev && count && buf) {
		OSObject *params[4] = { uuid, rev, count, buf };
		OSObject *result = NULL;
		ret = nub->evaluateObject("_OSC", &result, params, 4);
		OSData *out = OSDynamicCast(OSData, result);
		if (ret == kIOReturnSuccess && (out == NULL || out->getLength() < sizeof(caps))) {
			ret = kIOReturnBadArgument;
		}
		if (ret == kIOReturnSuccess) {
			const UInt32 *o = (const UInt32 *)out->getBytesNoCopy();
			*status = o[0];
			*control = o[2];
		}
		OSSafeReleaseNULL(result);
	}
	OSSafeReleaseNULL(uuid);
	OSSafeReleaseNULL(rev);
	OSSafeReleaseNULL(count);
	OSSafeReleaseNULL(buf);
	return ret;
}

void
NeoDarwinPCIHostBridge::negotiateOSC(IOACPIPlatformDevice *nub)
{
	const UInt32 support = kOSCSupportExtConfig | kOSCSupportASPM | kOSCSupportClockPM | kOSCSupportSegments;
	oscRequested = kOSCControlHotplug | kOSCControlPME | kOSCControlAER | kOSCControlPCIeCap | kOSCControlLTR;
	oscGranted = 0;
	if (nub->validateObject("_OSC") != kIOReturnSuccess) {
		oscResult = "no _OSC";
	} else {
		UInt32 control = oscRequested, status = 0;
		IOReturn ret = osc_call(nub, true, support, &control, &status);
		if (ret != kIOReturnSuccess) {
			oscResult = "_OSC query failed";
		} else if (status & (kOSCStatusFailure | kOSCStatusBadUUID | kOSCStatusBadRev)) {
			oscResult = "_OSC query refused";
		} else {
			UInt32 granted = control & oscRequested;
			ret = osc_call(nub, false, support, &granted, &status);
			if (ret != kIOReturnSuccess || (status & (kOSCStatusFailure | kOSCStatusBadUUID | kOSCStatusBadRev))) {
				oscResult = "_OSC request failed";
			} else {
				oscGranted = granted;
				oscResult = "_OSC";
			}
		}
	}
	// IOPCIFamily reads its flags when the host bridge's configurator is
	// created (IOPCIHostBridge::probe). The flags are global: with several
	// host bridges, AER stays on only if every one of them granted it.
	if ((oscGranted & kOSCControlAER) == 0) {
		gIOPCIFlags &= ~kIOPCIConfiguratorAER;
	}
	nub->setProperty("acpi-osc-control", oscGranted, 32);
}

// DMA coherence (docs/kernel/pci.md): _CCA on the bridge, else the IORT
// root complex node of the segment; on Arm, a bus master with neither is
// taken as not coherent (ACPI 6.5 §6.2.17; Linux does the same). Also notes
// what the root complex's ID mappings lead to: the ITS for MSIs, or an SMMU.
void
NeoDarwinPCIHostBridge::findCoherence(IOACPIPlatformDevice *nub)
{
	UInt64 cca = 0;
	bool haveCCA = nub->evaluateInteger("_CCA", &cca) == kIOReturnSuccess;
	dmaBits = 0;
	bool haveIORT = false;
	UInt32 iortCCA = 0;
	iortOutput = "nowhere (no IORT root complex node)";
	const OSData *iort = nub->getACPITableData("IORT", 0);
	if (iort != NULL && iort->getLength() >= 48) {
		const UInt8 *t = (const UInt8 *)iort->getBytesNoCopy();
		UInt32 len = iort->getLength();
		UInt32 count = rd32(t + 36), off = rd32(t + 40);
		for (UInt32 i = 0; i < count && off + 36 <= len; i++) {
			const UInt8 *node = t + off;
			UInt16 nodeLen;
			memcpy(&nodeLen, node + 1, sizeof(nodeLen));
			if (nodeLen < 16 || off + nodeLen > len) {
				break;
			}
			if (node[0] == 2 && nodeLen >= 33 && rd32(node + 28) == segment) {
				haveIORT = true;
				iortCCA = rd32(node + 16);
				dmaBits = node[32];     // Memory Size Limit: address bits the root complex can reach
				UInt32 maps = rd32(node + 8), ref = rd32(node + 12);
				iortOutput = "nowhere (no IORT ID mappings)";
				if (maps != 0 && ref + 20 <= nodeLen) {
					UInt32 out = rd32(node + ref + 12);
					iortOutput = out < len ? iort_node_name(t[out]) : "bad IORT reference";
				}
				break;
			}
			off += nodeLen;
		}
	}
	if (haveCCA) {
		coherent = cca == 1;
		coherenceSource = "_CCA";
	} else if (haveIORT) {
		coherent = (iortCCA & 1) != 0;
		coherenceSource = "IORT";
	} else {
		coherent = false;
		coherenceSource = "no _CCA or IORT";
	}
}

// ------------------------------------------------------------------------
// IOService

IOService *
NeoDarwinPCIHostBridge::probe(IOService *provider, SInt32 *score)
{
	acpi = OSDynamicCast(IOACPIPlatformDevice, provider);
	if (acpi == NULL || !readHostBridge(acpi)) {
		return NULL;
	}
	// Before IOPCIHostBridge::probe creates the configurator with the flags.
	negotiateOSC(acpi);
	// IOPCIFamily's log domain.
	UInt32 domain = segment;
	provider->setProperty("domain-id", &domain, sizeof(domain));
	return super::probe(provider, score);
}

// The windows the configurator allocates from, before IOPCIBridge::start
// hands the bridge to it (kConfigOpAddHostBridge reads them).
void
NeoDarwinPCIHostBridge::addWindows(IOService *provider)
{
	windows[0] = 0;
	OSArray *list = OSDynamicCast(OSArray, provider->getProperty("acpi-windows"));
	for (unsigned int i = 0; list != NULL && i < list->getCount(); i++) {
		OSDictionary *w = OSDynamicCast(OSDictionary, list->getObject(i));
		OSString *type = w ? OSDynamicCast(OSString, w->getObject("type")) : NULL;
		OSNumber *base = w ? OSDynamicCast(OSNumber, w->getObject("base")) : NULL;
		OSNumber *length = w ? OSDynamicCast(OSNumber, w->getObject("length")) : NULL;
		OSNumber *translation = w ? OSDynamicCast(OSNumber, w->getObject("translation")) : NULL;
		if (type == NULL || base == NULL || length == NULL || translation == NULL) {
			continue;
		}
		UInt64 b = base->unsigned64BitValue(), l = length->unsigned64BitValue(), x = translation->unsigned64BitValue();
		size_t used = strnlen(windows, sizeof(windows));
		if (type->isEqualTo("io")) {
			// Arm has no port instructions: the host bridge decodes this
			// bus I/O range as memory at CPU address base + translation.
			// IOPCIFamily maps an I/O BAR as a sub-range of ioDeviceMemory,
			// at the port number's offset, so it starts at port 0.
			// Ports below 0x1000 are left out, as Linux does on arm64
			// (PCIBIOS_MIN_IO): IOPCIFamily takes a BAR at port 0 for an
			// unassigned one, and EDK2 on QEMU hands out port 0.
			if (hasIO || b + l <= kMinIOPort) {
				continue;       // IOPCIFamily has one I/O space per bridge
			}
			hasIO = true;
			ioTranslation = x;
			ioMemory = IODeviceMemory::withRange((IOPhysicalAddress)x, (IOPhysicalLength)(b + l));
			UInt64 first = b < kMinIOPort ? kMinIOPort : b;
			addBridgeIORange((IOByteCount)first, (IOByteCount)(b + l - first));
			snprintf(windows + used, sizeof(windows) - used, "%sio 0x%llx+0x%llx at 0x%llx", used ? ", " : "", first, b + l - first, first + x);
		} else if (x != 0) {
			// IOPCIFamily assumes memory BARs are CPU addresses.
			snprintf(windows + used, sizeof(windows) - used, "%s%s 0x%llx+0x%llx (translated by 0x%llx: unused)",
			    used ? ", " : "", type->getCStringNoCopy(), b, l, x);
		} else if (type->isEqualTo("prefetchable")) {
			addBridgePrefetchableMemoryRange((addr64_t)b, (addr64_t)l);
			snprintf(windows + used, sizeof(windows) - used, "%sprefetchable 0x%llx+0x%llx", used ? ", " : "", b, l);
		} else {
			addBridgeMemoryRange((IOPhysicalAddress)b, (IOPhysicalLength)l, true);
			snprintf(windows + used, sizeof(windows) - used, "%smem 0x%llx+0x%llx", used ? ", " : "", b, l);
		}
	}
}

static void
summary_thread(void *param, wait_result_t)
{
	NeoDarwinPCIHostBridge *bridge = static_cast<NeoDarwinPCIHostBridge *>(param);
	bridge->logSummary();
	bridge->release();
}

bool
NeoDarwinPCIHostBridge::start(IOService *provider)
{
	logLock = IOLockAlloc();
	if (logLock == NULL) {
		return false;
	}
	findCoherence(acpi);
	addWindows(provider);
	setProperty("dma-coherent", coherent);
	if (dmaBits != 0) {
		setProperty("dma-address-bits", dmaBits, 32);
	}
	setProperty("pci-segment", segment, 16);
	setProperty("ecam-base", ecam, 64);
	busMaxSeen = busFirst;

	char dma[24] = "";
	if (dmaBits != 0) {
		snprintf(dma, sizeof(dma), ", %u address bits", dmaBits);
	}
	// Two lines: the kernel's log truncates long ones.
	IOLog("NeoDarwinPCIHostBridge: %s: segment %u, buses %u-%u, ECAM 0x%llx (%s); %s\n",
	    path, segment, busFirst, busLast, ecam, ecamSource, windows[0] ? windows : "no windows");
	IOLog("NeoDarwinPCIHostBridge: %s: %s control 0x%x of 0x%x; DMA %scoherent (%s%s); requester IDs to %s\n",
	    path, oscResult, oscGranted, oscRequested, coherent ? "" : "not ", coherenceSource, dma, iortOutput);

	// Every IOPCIDevice, as IOPCIFamily publishes it; only this bridge's
	// are logged. Installed first, so that bus 0's are seen too.
	publishNotifier = addMatchingNotification(gIOPublishNotification, serviceMatching("IOPCIDevice"),
	    OSMemberFunctionCast(IOServiceMatchingNotificationHandler, this, &NeoDarwinPCIHostBridge::devicePublished),
	    this, NULL, 0);

	if (!super::start(provider)) {
		return false;
	}

	// The summary once the bus and the bridges below it have been
	// enumerated and their drivers matched.
	retain();
	thread_t thread;
	if (kernel_thread_start(summary_thread, this, &thread) != KERN_SUCCESS) {
		release();
	} else {
		thread_deallocate(thread);
	}
	return true;
}

void
NeoDarwinPCIHostBridge::free(void)
{
	if (publishNotifier != NULL) {
		publishNotifier->remove();
		publishNotifier = NULL;
	}
	OSSafeReleaseNULL(ioMemory);
	if (logLock != NULL) {
		IOLockFree(logLock);
		logLock = NULL;
	}
	super::free();
}

void
NeoDarwinPCIHostBridge::logSummary(void)
{
	waitQuiet(10ULL * 1000 * 1000 * 1000);
	IOLockLock(logLock);
	IOLog("NeoDarwinPCIHostBridge: %s: segment %u: %u devices (%u PCI-to-PCI bridges) on buses %u-%u, %u with INTx\n",
	    path, segment, devices, bridges, busFirst, busMaxSeen, intx);
	IOLockUnlock(logLock);
}

// ------------------------------------------------------------------------
// Configuration space

UInt8
NeoDarwinPCIHostBridge::firstBusNum(void)
{
	return busFirst;
}

UInt8
NeoDarwinPCIHostBridge::lastBusNum(void)
{
	return busLast;
}

IODeviceMemory *
NeoDarwinPCIHostBridge::ioDeviceMemory(void)
{
	return ioMemory;
}

IOPCIAddressSpace
NeoDarwinPCIHostBridge::getBridgeSpace(void)
{
	IOPCIAddressSpace space;
	space.bits = 0;
	space.s.busNum = busFirst;
	return space;
}

// IOPCIFamily passes the low byte of the register in `offset` and bits 8-11
// in the address space (IOPCIConfigurator::configRead32).
UInt32
NeoDarwinPCIHostBridge::registerOf(IOPCIAddressSpace space, UInt8 offset)
{
	return (UInt32)space.es.registerNumExtended << 8 | offset;
}

UInt32
NeoDarwinPCIHostBridge::configRead32(IOPCIAddressSpace space, UInt8 offset)
{
	UInt32 v;
	nd_pci_config_read(segment, space.s.busNum, space.s.deviceNum, space.s.functionNum, registerOf(space, offset), 4, &v);
	return v;
}

void
NeoDarwinPCIHostBridge::configWrite32(IOPCIAddressSpace space, UInt8 offset, UInt32 data)
{
	nd_pci_config_write(segment, space.s.busNum, space.s.deviceNum, space.s.functionNum, registerOf(space, offset), 4, data);
}

UInt16
NeoDarwinPCIHostBridge::configRead16(IOPCIAddressSpace space, UInt8 offset)
{
	UInt32 v;
	nd_pci_config_read(segment, space.s.busNum, space.s.deviceNum, space.s.functionNum, registerOf(space, offset), 2, &v);
	return (UInt16)v;
}

void
NeoDarwinPCIHostBridge::configWrite16(IOPCIAddressSpace space, UInt8 offset, UInt16 data)
{
	nd_pci_config_write(segment, space.s.busNum, space.s.deviceNum, space.s.functionNum, registerOf(space, offset), 2, data);
}

UInt8
NeoDarwinPCIHostBridge::configRead8(IOPCIAddressSpace space, UInt8 offset)
{
	UInt32 v;
	nd_pci_config_read(segment, space.s.busNum, space.s.deviceNum, space.s.functionNum, registerOf(space, offset), 1, &v);
	return (UInt8)v;
}

void
NeoDarwinPCIHostBridge::configWrite8(IOPCIAddressSpace space, UInt8 offset, UInt8 data)
{
	nd_pci_config_write(segment, space.s.busNum, space.s.deviceNum, space.s.functionNum, registerOf(space, offset), 1, data);
}

// ------------------------------------------------------------------------
// The log

// Whether `service` is an IOPCIDevice of this bridge's hierarchy: its
// providers alternate IOPCIBridge and IOPCIDevice up to this bridge.
bool
NeoDarwinPCIHostBridge::below(IOService *service)
{
	for (IOService *p = service->getProvider(); p != NULL; p = p->getProvider()) {
		if (p == this) {
			return true;
		}
	}
	return false;
}

// An IOPCIDevice on bus 0 whose device and function match an _ADR-only
// child of the host bridge in IOACPIPlane (QEMU's S00, S08, ...) gets that
// node's path, for the _DSM, _PRW and _SUN a driver may want.
void
NeoDarwinPCIHostBridge::pairWithACPI(IOPCIDevice *device)
{
	if (gIOACPIPlane == NULL || device->getBusNumber() != busFirst) {
		return;
	}
	UInt64 adr = (UInt64)device->getDeviceNumber() << 16 | device->getFunctionNumber();
	OSIterator *children = acpi->getChildIterator(gIOACPIPlane);
	if (children == NULL) {
		return;
	}
	while (IORegistryEntry *child = OSDynamicCast(IORegistryEntry, children->getNextObject())) {
		OSNumber *a = OSDynamicCast(OSNumber, child->getProperty(gIOACPIAddressKey));
		if (a != NULL && a->unsigned64BitValue() == adr) {
			OSObject *p = child->getProperty("acpi-path");
			if (p != NULL) {
				device->setProperty("acpi-path", p);
			}
			break;
		}
	}
	children->release();
}

static void
append(char *buf, size_t size, const char *fmt, ...) __printflike(3, 4);

static void
append(char *buf, size_t size, const char *fmt, ...)
{
	size_t used = strnlen(buf, size);
	if (used + 1 >= size) {
		return;
	}
	va_list ap;
	va_start(ap, fmt);
	vsnprintf(buf + used, size - used, fmt, ap);
	va_end(ap);
}

bool
NeoDarwinPCIHostBridge::devicePublished(void *refCon, IOService *service, IONotifier *notifier)
{
	(void)refCon; (void)notifier;
	IOPCIDevice *device = OSDynamicCast(IOPCIDevice, service);
	if (device == NULL || !below(device)) {
		return true;
	}
	UInt32 id = device->configRead32(kIOPCIConfigVendorID);
	UInt32 classRev = device->configRead32(kIOPCIConfigRevisionID);
	UInt8 header = device->configRead8(kIOPCIConfigHeaderType) & 0x7f;
	UInt8 pin = device->configRead8(kIOPCIConfigInterruptPin);

	// What drivers (P1-10) need to know about DMA: whether it is coherent,
	// and how many address bits the root complex passes (IORT).
	device->setProperty("dma-coherent", coherent);
	if (dmaBits != 0) {
		device->setProperty("dma-address-bits", dmaBits, 32);
	}
	pairWithACPI(device);

	char where[16], line[256], bars[256] = "", irq[32] = "";
	snprintf(where, sizeof(where), "%04x:%02x:%02x.%x", segment, device->getBusNumber(), device->getDeviceNumber(),
	    device->getFunctionNumber());
	snprintf(line, sizeof(line), "NeoDarwinPCIHostBridge: %s %04x:%04x class %06x", where, id & 0xffff, id >> 16, classRev >> 8);
	bool bridge = header == 1;
	if (bridge) {
		UInt32 buses = device->configRead32(kPCI2PCIPrimaryBus);
		append(line, sizeof(line), " bridge to buses %u-%u", (buses >> 8) & 0xff, (buses >> 16) & 0xff);
	}

	// The BARs as the configurator left them: assigned-addresses holds
	// five cells per BAR (IOPCIPhysicalAddress: space, low, high address,
	// low, high length).
	OSData *assigned = OSDynamicCast(OSData, device->getProperty("assigned-addresses"));
	if (assigned != NULL) {
		const UInt32 *c = (const UInt32 *)assigned->getBytesNoCopy();
		for (unsigned int n = assigned->getLength() / (5 * sizeof(UInt32)); n-- != 0; c += 5) {
			IOPCIAddressSpace s;
			s.bits = c[0];
			UInt64 addr = (UInt64)c[2] << 32 | c[1];
			UInt64 len = (UInt64)c[4] << 32 | c[3];
			const char *bar = s.s.registerNum == kIOPCIConfigExpansionROMBase ? "rom" : NULL;
			char name[8];
			if (bar == NULL) {
				snprintf(name, sizeof(name), "bar%u", (s.s.registerNum - kIOPCIConfigBaseAddress0) / 4);
				bar = name;
			}
			if (s.s.space == kIOPCIIOSpace) {
				append(bars, sizeof(bars), " %s io 0x%llx+0x%llx", bar, addr + ioTranslation, len);
			} else {
				append(bars, sizeof(bars), " %s mem%s%s 0x%llx+0x%llx", bar, s.s.space == kIOPCI64BitMemorySpace ? "64" : "",
				    s.s.prefetch ? " pf" : "", addr, len);
			}
		}
	}

	// INTx: reading the specifiers makes IOPCIFamily resolve them, through
	// the ACPI nub's _PRT (IOACPIPlatformDevice::callPlatformFunction).
	bool routed = false;
	if (pin >= 1 && pin <= 4) {
		OSArray *specs = OSDynamicCast(OSArray, device->getProperty(gIOInterruptSpecifiersKey));
		OSData *spec = specs != NULL && specs->getCount() != 0 ? OSDynamicCast(OSData, specs->getObject(0)) : NULL;
		if (spec != NULL && spec->getLength() >= sizeof(UInt32)) {
			snprintf(irq, sizeof(irq), " INT%c gsiv %u", 'A' + pin - 1, *(const UInt32 *)spec->getBytesNoCopy());
			routed = true;
		} else {
			snprintf(irq, sizeof(irq), " INT%c unrouted", 'A' + pin - 1);
		}
	}

	IOLockLock(logLock);
	devices++;
	if (bridge) {
		bridges++;
		UInt8 sub = (UInt8)(device->configRead32(kPCI2PCIPrimaryBus) >> 16);
		if (sub > busMaxSeen) {
			busMaxSeen = sub;
		}
	}
	if (routed) {
		intx++;
	}
	// The kernel's log truncates lines near 256 bytes: a device with many
	// BARs gets them on a line of their own.
	if (strlen(line) + strlen(bars) + strlen(irq) < 200) {
		IOLog("%s%s%s\n", line, bars, irq);
	} else {
		IOLog("%s%s\n", line, irq);
		IOLog("NeoDarwinPCIHostBridge: %s:%s\n", where, bars);
	}
	IOLockUnlock(logLock);
	return true;
}
