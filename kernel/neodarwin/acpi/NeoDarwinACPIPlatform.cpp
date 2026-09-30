// SPDX-License-Identifier: BSD-2-Clause
// NeoDarwin-Language: expressibility: IOKit classes are C++; this subclasses XNU's IOService and calls ACPICA's C interface.
//
// ACPICA's start-up and the namespace walk that publishes IOACPIPlatformDevice
// nubs (docs/kernel/acpi.md).
//
// Each present device (_STA bit 0, or no _STA) in \_SB becomes a nub in
// IOACPIPlane, under the nub of its nearest ancestor device. A device with
// a _HID or _CID is also published in the service plane (registerService),
// where drivers match it by IONameMatch on those IDs. Registration waits
// until the whole namespace has nubs: a driver that starts on one (the PCI
// host bridge) may look up others (the _PRT's link devices, children of the
// bridge that the walk reaches after it), and matching runs on other CPUs
// while the walk goes on. A device named only
// by _ADR (a PCI slot under a host bridge) stays in IOACPIPlane, for the
// bus driver to find (P1-09 checkpoint 2). A device whose _STA says absent
// but functioning is skipped and its children walked; absent and not
// functioning skips the subtree (ACPI 6.5 §6.3.7).
//
// Nub properties:
//   name, compatible  _HID, then _CIDs (NUL-separated), as the device tree has
//   _HID, _CID, _UID, _ADR, _STA, acpi-path
//   IODeviceMemory    memory descriptors of _CRS, as CPU physical addresses
//   interrupts        u32 INTIDs of _CRS Interrupt() descriptors: on the GIC,
//                     a GSIV is the INTID. As IOInterruptSpecifiers (two
//                     cells: the INTID and the flags below, NeoDarwinGICv3's
//                     specifier format) and IOInterruptControllers, so
//                     registerInterrupt works.
//   interrupt-parent  u32: the GIC's phandle
//   acpi-interrupt-flags  u32 per interrupt: bit 0 edge, bit 1 active low,
//                     bit 2 shared, bit 3 wake-capable
//   acpi-bus-range, acpi-windows  on PCI host bridges: the bus numbers and
//                     the windows _CRS produces (checkpoint 2's input)

#include "NeoDarwinACPIPlatform.h"
#include "nd_acpica.h"
#include "nd_pci.h"
#include <IOKit/IODeviceMemory.h>
#include <IOKit/IODeviceTreeSupport.h>
#include <IOKit/IOLib.h>
#include <kern/thread.h>
#include <pexpert/pexpert.h>

#define super IOService
OSDefineMetaClassAndStructors(NeoDarwinACPIPlatform, IOService);

const IORegistryPlane *gIOACPIPlane;
const OSSymbol *gIOACPIHardwareIDKey;
const OSSymbol *gIOACPIUniqueIDKey;
const OSSymbol *gIOACPIAddressKey;
const OSSymbol *gIOACPIDeviceStatusKey;

static NeoDarwinACPIPlatform *gACPIPlatform;

// ------------------------------------------------------------------------
// Start-up

static void
acpi_platform_thread(void *param, wait_result_t)
{
	NeoDarwinACPIPlatform *platform = static_cast<NeoDarwinACPIPlatform *>(param);
	IOService *provider = platform->getProvider();
	if (platform->start(provider)) {
		platform->registerService();
	} else {
		platform->detach(provider);
	}
	platform->release();
}

void
NeoDarwinACPIPlatform::startFromPlatformExpert(IOService *platformExpert)
{
	if (nd_acpi_rsdp() == 0) {
		IOLog("NeoDarwinACPIPlatform: the loader passed no ACPI tables (/chosen acpi-rsdp)\n");
		return;
	}
	NeoDarwinACPIPlatform *platform = OSTypeAlloc(NeoDarwinACPIPlatform);
	if (platform == NULL || !platform->init() || !platform->attach(platformExpert)) {
		panic("NeoDarwinACPIPlatform: cannot create the ACPI platform");
	}
	// ACPICA's start-up evaluates AML (_INI, _REG, _STA of every device) and
	// may sleep; the platform expert's start must not wait for it.
	thread_t thread;
	if (kernel_thread_start(acpi_platform_thread, platform, &thread) != KERN_SUCCESS) {
		panic("NeoDarwinACPIPlatform: cannot start the ACPI thread");
	}
	thread_deallocate(thread);
}

static const char *
acpi_error(ACPI_STATUS status)
{
	return AcpiFormatException(status);
}

bool
NeoDarwinACPIPlatform::initACPICA(void)
{
	ACPI_STATUS status = AcpiInitializeSubsystem();
	if (ACPI_FAILURE(status)) {
		IOLog("NeoDarwinACPIPlatform: AcpiInitializeSubsystem: %s\n", acpi_error(status));
		return false;
	}
	status = AcpiInitializeTables(NULL, 16, FALSE);
	if (ACPI_FAILURE(status)) {
		IOLog("NeoDarwinACPIPlatform: AcpiInitializeTables: %s\n", acpi_error(status));
		return false;
	}
	status = AcpiLoadTables();
	if (ACPI_FAILURE(status)) {
		IOLog("NeoDarwinACPIPlatform: AcpiLoadTables: %s\n", acpi_error(status));
		return false;
	}
	// Before any AML runs (_INI, _REG): PCI_Config operation regions reach
	// configuration space through ECAM.
	registerECAM();
	// Hardware-reduced: no ACPI mode switch, SCI, fixed events or GPEs.
	status = AcpiEnableSubsystem(ACPI_FULL_INITIALIZATION);
	if (ACPI_FAILURE(status)) {
		IOLog("NeoDarwinACPIPlatform: AcpiEnableSubsystem: %s\n", acpi_error(status));
		return false;
	}
	status = AcpiInitializeObjects(ACPI_FULL_INITIALIZATION);
	if (ACPI_FAILURE(status)) {
		IOLog("NeoDarwinACPIPlatform: AcpiInitializeObjects: %s\n", acpi_error(status));
		return false;
	}
	return true;
}

bool
NeoDarwinACPIPlatform::start(IOService *provider)
{
	if (!super::start(provider)) {
		return false;
	}
	uint64_t started = mach_absolute_time();
	gACPIPlatform = this;
	tables = OSDictionary::withCapacity(16);
	tablesLock = IOLockAlloc();
	if (tables == NULL || tablesLock == NULL) {
		return false;
	}
	gIOACPIPlane = IORegistryEntry::makePlane(kIOACPIPlane);
	gIOACPIHardwareIDKey = OSSymbol::withCStringNoCopy("_HID");
	gIOACPIUniqueIDKey = OSSymbol::withCStringNoCopy("_UID");
	gIOACPIAddressKey = OSSymbol::withCStringNoCopy("_ADR");
	gIOACPIDeviceStatusKey = OSSymbol::withCStringNoCopy("_STA");

	// Interrupt specifiers name the GIC as the device tree's do.
	IORegistryEntry *gic = IORegistryEntry::fromPath("/arm-io/gic", gIODTPlane);
	if (gic != NULL) {
		gicName = IODTInterruptControllerName(gic);
		OSData *ph = OSDynamicCast(OSData, gic->getProperty("AAPL,phandle"));
		if (ph != NULL && ph->getLength() == sizeof(UInt32)) {
			gicPHandle = *(const UInt32 *)ph->getBytesNoCopy();
		}
		gic->release();
	}
	if (gicName == NULL) {
		IOLog("NeoDarwinACPIPlatform: no /arm-io/gic: devices get no interrupt specifiers\n");
	}

	UInt32 v = 1;
	if (PE_parse_boot_argn("ndacpi_verbose", &v, sizeof(v))) {
		verbose = v != 0;
	} else {
		verbose = true;
	}

	if (!initACPICA()) {
		return false;
	}

	char signatures[128] = "";
	ACPI_TABLE_HEADER *header;
	for (UInt32 i = 0; AcpiGetTableByIndex(i, &header) == AE_OK; i++) {
		char sig[6] = { ' ', header->Signature[0], header->Signature[1], header->Signature[2], header->Signature[3], 0 };
		strlcat(signatures, i == 0 ? sig + 1 : sig, sizeof(signatures));
		AcpiPutTable(header);
		tableCount++;
	}
	setProperty("ACPICA version", ACPI_CA_VERSION, 32);
	setProperty("acpi-tables", signatures);

	attachToParent(getRegistryRoot(), gIOACPIPlane);
	hostBridges = OSArray::withCapacity(1);
	toRegister = OSArray::withCapacity(32);
	publishDevices();
	if (hostBridges != NULL) {
		for (unsigned int i = 0; i < hostBridges->getCount(); i++) {
			describeHostBridge(OSDynamicCast(IOACPIPlatformDevice, hostBridges->getObject(i)));
		}
		OSSafeReleaseNULL(hostBridges);
	}
	if (toRegister != NULL) {
		for (unsigned int i = 0; i < toRegister->getCount(); i++) {
			OSDynamicCast(IOService, toRegister->getObject(i))->registerService();
		}
		OSSafeReleaseNULL(toRegister);
	}

	uint64_t ns;
	absolutetime_to_nanoseconds(mach_absolute_time() - started, &ns);
	IOLog("NeoDarwinACPIPlatform: ACPICA %08x; %u tables (%s); %u devices published, %u more in IOACPIPlane; %llu ms\n",
	    (unsigned)ACPI_CA_VERSION, tableCount, signatures, published, addressOnly, ns / 1000000);
	return true;
}

// ------------------------------------------------------------------------
// Tables

const OSData *
NeoDarwinACPIPlatform::getTableData(const char *signature, UInt32 instance)
{
	if (signature == NULL || strnlen(signature, ACPI_NAMESEG_SIZE + 1) != ACPI_NAMESEG_SIZE) {
		return NULL;
	}
	char key[24];
	snprintf(key, sizeof(key), "%s%u", signature, instance);
	IOLockLock(tablesLock);
	OSData *data = OSDynamicCast(OSData, tables->getObject(key));
	if (data == NULL) {
		ACPI_TABLE_HEADER *header;
		char sig[ACPI_NAMESEG_SIZE + 1];
		strlcpy(sig, signature, sizeof(sig));
		if (AcpiGetTable(sig, instance + 1, &header) == AE_OK) {
			data = OSData::withBytes(header, header->Length);
			AcpiPutTable(header);
			if (data != NULL) {
				tables->setObject(key, data);
				data->release();    // the dictionary keeps it
			}
		}
	}
	IOLockUnlock(tablesLock);
	return data;
}

// ------------------------------------------------------------------------
// The namespace walk

static void
acpi_nub_data_handler(ACPI_HANDLE, void *)
{
	// The registry owns the nub; the namespace only points at it.
}

// _STA, or all of present, enabled, shown and functioning without one
// (ACPI 6.5 §6.3.7). When _STA fails (AML touching an address space with no
// handler, a dependency that isn't there yet), the device is left out but
// its children are still walked: one bad device must not hide a subtree.
static UInt32
acpi_sta(ACPI_HANDLE handle, ACPI_STATUS *error)
{
	ACPI_OBJECT obj;
	ACPI_BUFFER buf = { sizeof(obj), &obj };
	ACPI_STATUS status = AcpiEvaluateObjectTyped(handle, (char *)METHOD_NAME__STA, NULL, &buf, ACPI_TYPE_INTEGER);
	*error = AE_OK;
	if (status == AE_NOT_FOUND) {
		return ACPI_STA_DEVICE_PRESENT | ACPI_STA_DEVICE_ENABLED | ACPI_STA_DEVICE_UI | ACPI_STA_DEVICE_FUNCTIONING;
	}
	if (ACPI_FAILURE(status)) {
		*error = status;
		return ACPI_STA_DEVICE_FUNCTIONING;
	}
	return (UInt32)obj.Integer.Value;
}

static ACPI_STATUS
acpi_device_walk(ACPI_HANDLE handle, UINT32, void *context, void **)
{
	ACPI_STATUS error;
	UInt32 sta = acpi_sta(handle, &error);
	if (error != AE_OK) {
		char path[128] = "?";
		ACPI_BUFFER buf = { sizeof(path), path };
		AcpiGetName(handle, ACPI_FULL_PATHNAME_NO_TRAILING, &buf);
		IOLog("NeoDarwinACPIPlatform: %s: _STA failed (%s); left out, its children walked\n", path, acpi_error(error));
	}
	if ((sta & ACPI_STA_DEVICE_PRESENT) == 0) {
		return (sta & ACPI_STA_DEVICE_FUNCTIONING) ? AE_OK : AE_CTRL_DEPTH;
	}
	static_cast<NeoDarwinACPIPlatform *>(context)->publish(handle, sta);
	return AE_OK;
}

void
NeoDarwinACPIPlatform::publishDevices(void)
{
	ACPI_HANDLE sb;
	if (AcpiGetHandle(NULL, (char *)"\\_SB", &sb) != AE_OK) {
		IOLog("NeoDarwinACPIPlatform: the namespace has no \\_SB\n");
		return;
	}
	AcpiWalkNamespace(ACPI_TYPE_DEVICE, sb, ACPI_UINT32_MAX, acpi_device_walk, NULL, this, NULL);
}

// The nub of the nearest ancestor device, in the service plane or any.
static IOACPIPlatformDevice *
acpi_ancestor_nub(ACPI_HANDLE handle, bool servicePlane)
{
	ACPI_HANDLE parent;
	while (AcpiGetParent(handle, &parent) == AE_OK) {
		void *data;
		if (AcpiGetData(parent, acpi_nub_data_handler, &data) == AE_OK) {
			IOACPIPlatformDevice *nub = static_cast<IOACPIPlatformDevice *>(data);
			if (!servicePlane || nub->inPlane(gIOServicePlane)) {
				return nub;
			}
		}
		handle = parent;
	}
	return NULL;
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

void
NeoDarwinACPIPlatform::publish(void *handle, UInt32 status)
{
	ACPI_DEVICE_INFO *info;
	if (AcpiGetObjectInfo(handle, &info) != AE_OK) {
		return;
	}
	char path[128] = "?";
	ACPI_BUFFER pathBuf = { sizeof(path), path };
	AcpiGetName(handle, ACPI_FULL_PATHNAME_NO_TRAILING, &pathBuf);

	// The four-character name, trailing padding removed: the registry name.
	char seg[ACPI_NAMESEG_SIZE + 1];
	memcpy(seg, &info->Name, ACPI_NAMESEG_SIZE);
	seg[ACPI_NAMESEG_SIZE] = 0;
	for (int i = ACPI_NAMESEG_SIZE - 1; i > 0 && seg[i] == '_'; i--) {
		seg[i] = 0;
	}

	bool hasHID = (info->Valid & ACPI_VALID_HID) != 0;
	UInt32 cidCount = (info->Valid & ACPI_VALID_CID) ? info->CompatibleIdList.Count : 0;
	bool bridge = (info->Flags & ACPI_PCI_ROOT_BRIDGE) != 0;

	OSDictionary *props = OSDictionary::withCapacity(16);
	OSData *compatible = OSData::withCapacity(32);
	if (props == NULL || compatible == NULL) {
		OSSafeReleaseNULL(props);
		OSSafeReleaseNULL(compatible);
		AcpiOsFree(info);
		return;
	}
	char summary[256];
	snprintf(summary, sizeof(summary), "%s", path);
	const char *nameString = hasHID ? info->HardwareId.String : seg;
	OSData *nameData = OSData::withBytes(nameString, (unsigned)strlen(nameString) + 1);
	if (nameData != NULL) {
		props->setObject("name", nameData);
		nameData->release();
	}
	if (hasHID) {
		const char *hid = info->HardwareId.String;
		compatible->appendBytes(hid, (unsigned)strlen(hid) + 1);
		OSString *s = OSString::withCString(hid);
		props->setObject(gIOACPIHardwareIDKey, s);
		OSSafeReleaseNULL(s);
		append(summary, sizeof(summary), " %s", hid);
	}
	if (cidCount != 0) {
		OSArray *cids = OSArray::withCapacity(cidCount);
		append(summary, sizeof(summary), " (");
		for (UInt32 i = 0; i < cidCount; i++) {
			const char *cid = info->CompatibleIdList.Ids[i].String;
			compatible->appendBytes(cid, (unsigned)strlen(cid) + 1);
			OSString *s = OSString::withCString(cid);
			if (cids != NULL && s != NULL) {
				cids->setObject(s);
			}
			OSSafeReleaseNULL(s);
			append(summary, sizeof(summary), "%s%s", i ? " " : "", cid);
		}
		append(summary, sizeof(summary), ")");
		if (cids != NULL) {
			props->setObject("_CID", cids);
			cids->release();
		}
	}
	if (compatible->getLength() != 0) {
		props->setObject("compatible", compatible);
	}
	compatible->release();
	char location[32] = "";
	if (info->Valid & ACPI_VALID_UID) {
		OSString *s = OSString::withCString(info->UniqueId.String);
		props->setObject(gIOACPIUniqueIDKey, s);
		OSSafeReleaseNULL(s);
		strlcpy(location, info->UniqueId.String, sizeof(location));
		append(summary, sizeof(summary), " uid %s", info->UniqueId.String);
	}
	if (info->Valid & ACPI_VALID_ADR) {
		OSNumber *n = OSNumber::withNumber(info->Address, 64);
		props->setObject(gIOACPIAddressKey, n);
		OSSafeReleaseNULL(n);
		if (location[0] == 0) {
			snprintf(location, sizeof(location), "%llx", info->Address);
		}
		append(summary, sizeof(summary), " adr 0x%llx", info->Address);
	}
	OSNumber *sta = OSNumber::withNumber(status, 32);
	props->setObject(gIOACPIDeviceStatusKey, sta);
	OSSafeReleaseNULL(sta);
	OSString *pathString = OSString::withCString(path);
	props->setObject("acpi-path", pathString);
	OSSafeReleaseNULL(pathString);
	AcpiOsFree(info);

	IOACPIPlatformDevice *nub = OSTypeAlloc(IOACPIPlatformDevice);
	if (nub == NULL || !nub->init(this, handle, props)) {
		OSSafeReleaseNULL(nub);
		props->release();
		return;
	}
	props->release();
	nub->setName(seg);
	if (location[0] != 0) {
		nub->setLocation(location);
	}
	addResources(nub, handle, bridge, summary, sizeof(summary));

	IOACPIPlatformDevice *acpiParent = acpi_ancestor_nub(handle, false);
	nub->attachToParent(acpiParent != NULL ? (IORegistryEntry *)acpiParent : (IORegistryEntry *)this, gIOACPIPlane);
	AcpiAttachData(handle, acpi_nub_data_handler, nub);

	if (hasHID || cidCount != 0) {
		IOACPIPlatformDevice *serviceParent = acpi_ancestor_nub(handle, true);
		nub->attach(serviceParent != NULL ? (IOService *)serviceParent : (IOService *)this);
		if (toRegister == NULL || !toRegister->setObject(nub)) {
			nub->registerService();
		}
		published++;
		if (verbose) {
			IOLog("NeoDarwinACPIPlatform: %s\n", summary);
		}
		if (bridge && hostBridges != NULL) {
			hostBridges->setObject(nub);
		}
	} else {
		addressOnly++;
	}
	nub->release();     // the registry holds it
}

// Every MCFG allocation (PCI Firmware 3.3 §4.1.2), into the ECAM registry
// (nd_pci.h): a 44-byte header, then 16-byte entries {u64 base (of bus 0),
// u16 segment, u8 first bus, u8 last bus, u32 reserved}.
void
NeoDarwinACPIPlatform::registerECAM(void)
{
	ACPI_TABLE_HEADER *header;
	if (AcpiGetTable((char *)ACPI_SIG_MCFG, 1, &header) != AE_OK) {
		return;
	}
	const UInt8 *p = (const UInt8 *)header;
	for (UInt32 off = 44; off + 16 <= header->Length; off += 16) {
		UInt64 base;
		UInt16 segment;
		memcpy(&base, p + off, sizeof(base));
		memcpy(&segment, p + off + 8, sizeof(segment));
		nd_pci_ecam_add(segment, p[off + 10], p[off + 11], base);
	}
	AcpiPutTable(header);
}

IOACPIPlatformDevice *
NeoDarwinACPIPlatform::nubForPath(const char *path)
{
	ACPI_HANDLE handle;
	void *data;
	if (path == NULL || AcpiGetHandle(NULL, (char *)path, &handle) != AE_OK ||
	    AcpiGetData(handle, acpi_nub_data_handler, &data) != AE_OK) {
		return NULL;
	}
	return static_cast<IOACPIPlatformDevice *>(data);
}

// What a PCI host bridge's driver will need (P1-09 checkpoint 2), read
// through the nub's own interface: the segment and bus from _SEG and _BBN,
// the ECAM base from _CBA (or MCFG), the interrupt routing from _PRT.
void
NeoDarwinACPIPlatform::describeHostBridge(IOACPIPlatformDevice *nub)
{
	if (nub == NULL) {
		return;
	}
	UInt64 segment = 0, bus = 0, ecam = 0;
	nub->evaluateInteger("_SEG", &segment);
	nub->evaluateInteger("_BBN", &bus);
	bool cba = nub->evaluateInteger("_CBA", &ecam) == kIOReturnSuccess;
	const OSData *mcfg = nub->getACPITableData("MCFG", 0);
	if (!cba && mcfg != NULL) {
		// MCFG: a 44-byte header, then 16-byte allocations
		// {u64 base, u16 segment, u8 start bus, u8 end bus, u32 reserved}.
		const UInt8 *p = (const UInt8 *)mcfg->getBytesNoCopy();
		for (unsigned int off = 44; off + 16 <= mcfg->getLength(); off += 16) {
			UInt16 s;
			memcpy(&s, p + off + 8, sizeof(s));
			if (s == segment && p[off + 10] <= bus && bus <= p[off + 11]) {
				memcpy(&ecam, p + off, sizeof(ecam));
				break;
			}
		}
	}
	OSObject *prt = NULL;
	unsigned int routes = 0;
	if (nub->evaluateObject("_PRT", &prt) == kIOReturnSuccess) {
		OSArray *a = OSDynamicCast(OSArray, prt);
		routes = a != NULL ? a->getCount() : 0;
	}
	OSSafeReleaseNULL(prt);
	OSString *path = OSDynamicCast(OSString, nub->getProperty("acpi-path"));
	IOLog("NeoDarwinACPIPlatform: %s: PCI segment %llu, bus %llu, ECAM 0x%llx (%s); _PRT %u routes; MCFG %u bytes\n",
	    path != NULL ? path->getCStringNoCopy() : "?", segment, bus, ecam, cba ? "_CBA" : "MCFG", routes,
	    mcfg != NULL ? mcfg->getLength() : 0);
}

// ------------------------------------------------------------------------
// _CRS

namespace {
struct ResourceContext {
	OSArray *memory;
	OSArray *specifiers;
	OSArray *controllers;
	OSData *interrupts;
	OSData *flags;
	OSArray *windows;
	const OSSymbol *gic;
	bool bridge;
	bool hasBus;
	UInt32 busMin, busMax;
	char *summary;
	size_t summarySize;
};
}

static void
add_memory(ResourceContext *c, UInt64 base, UInt64 length)
{
	if (length == 0) {
		return;
	}
	IODeviceMemory *m = IODeviceMemory::withRange((IOPhysicalAddress)base, (IOPhysicalLength)length);
	if (m != NULL) {
		c->memory->setObject(m);
		m->release();
	}
	append(c->summary, c->summarySize, " mem 0x%llx+0x%llx", base, length);
}

static void
add_interrupt(ResourceContext *c, UInt32 gsiv, UInt8 triggering, UInt8 polarity, UInt8 shareable, UInt8 wake)
{
	UInt32 f = 0;
	if (triggering == ACPI_EDGE_SENSITIVE) {
		f |= ND_ACPI_IRQ_EDGE;
	}
	if (polarity == ACPI_ACTIVE_LOW) {
		f |= ND_ACPI_IRQ_ACTIVE_LOW;
	}
	if (shareable == ACPI_SHARED) {
		f |= ND_ACPI_IRQ_SHARED;
	}
	if (wake == ACPI_WAKE_CAPABLE) {
		f |= ND_ACPI_IRQ_WAKE;
	}
	c->interrupts->appendBytes(&gsiv, sizeof(gsiv));
	c->flags->appendBytes(&f, sizeof(f));
	if (c->gic != NULL) {
		UInt32 cells[2] = { gsiv, f };
		OSData *spec = OSData::withBytes(cells, sizeof(cells));
		if (spec != NULL) {
			c->specifiers->setObject(spec);
			c->controllers->setObject(c->gic);
			spec->release();
		}
	}
	append(c->summary, c->summarySize, " irq %u%s", gsiv, (f & ND_ACPI_IRQ_EDGE) ? " edge" : "");
}

static void
add_window(ResourceContext *c, const char *type, UInt64 min, UInt64 length, UInt64 translation)
{
	OSDictionary *w = OSDictionary::withCapacity(4);
	if (w == NULL) {
		return;
	}
	OSString *t = OSString::withCString(type);
	OSNumber *b = OSNumber::withNumber(min, 64);
	OSNumber *l = OSNumber::withNumber(length, 64);
	OSNumber *x = OSNumber::withNumber(translation, 64);
	if (t && b && l && x) {
		w->setObject("type", t);
		w->setObject("base", b);
		w->setObject("length", l);
		w->setObject("translation", x);
		c->windows->setObject(w);
	}
	OSSafeReleaseNULL(t);
	OSSafeReleaseNULL(b);
	OSSafeReleaseNULL(l);
	OSSafeReleaseNULL(x);
	w->release();
	append(c->summary, c->summarySize, " %s 0x%llx+0x%llx", type, min + translation, length);
}

static ACPI_STATUS
acpi_resource_walk(ACPI_RESOURCE *res, void *context)
{
	ResourceContext *c = static_cast<ResourceContext *>(context);
	switch (res->Type) {
	case ACPI_RESOURCE_TYPE_FIXED_MEMORY32:
		add_memory(c, res->Data.FixedMemory32.Address, res->Data.FixedMemory32.AddressLength);
		break;
	case ACPI_RESOURCE_TYPE_MEMORY32:
		add_memory(c, res->Data.Memory32.Minimum, res->Data.Memory32.AddressLength);
		break;
	case ACPI_RESOURCE_TYPE_ADDRESS16:
	case ACPI_RESOURCE_TYPE_ADDRESS32:
	case ACPI_RESOURCE_TYPE_ADDRESS64:
	case ACPI_RESOURCE_TYPE_EXTENDED_ADDRESS64: {
		ACPI_RESOURCE_ADDRESS64 a;
		if (ACPI_FAILURE(AcpiResourceToAddress64(res, &a)) || a.Address.AddressLength == 0) {
			break;
		}
		// A host bridge's producer ranges are the windows it decodes for
		// its bus, not registers of its own. Firmware sets the
		// producer/consumer bit loosely elsewhere (QEMU marks a
		// motherboard resource's ECAM as a producer), so only bridges
		// read it.
		if (c->bridge && a.ProducerConsumer == ACPI_PRODUCER) {
			if (a.ResourceType == ACPI_BUS_NUMBER_RANGE) {
				c->hasBus = true;
				c->busMin = (UInt32)a.Address.Minimum;
				c->busMax = (UInt32)(a.Address.Minimum + a.Address.AddressLength - 1);
				append(c->summary, c->summarySize, " bus %u-%u", c->busMin, c->busMax);
			} else if (a.ResourceType == ACPI_MEMORY_RANGE) {
				add_window(c, a.Info.Mem.Caching == ACPI_PREFETCHABLE_MEMORY ? "prefetchable" : "memory",
				    a.Address.Minimum, a.Address.AddressLength, a.Address.TranslationOffset);
			} else if (a.ResourceType == ACPI_IO_RANGE) {
				add_window(c, "io", a.Address.Minimum, a.Address.AddressLength, a.Address.TranslationOffset);
			}
		} else if (a.ResourceType == ACPI_MEMORY_RANGE) {
			add_memory(c, a.Address.Minimum + a.Address.TranslationOffset, a.Address.AddressLength);
		}
		break;
	}
	case ACPI_RESOURCE_TYPE_EXTENDED_IRQ: {
		const ACPI_RESOURCE_EXTENDED_IRQ *irq = &res->Data.ExtendedIrq;
		if (irq->ResourceSource.StringLength != 0) {
			// On another interrupt controller (a GPIO block, say): its
			// driver resolves it.
			append(c->summary, c->summarySize, " irq@%s", irq->ResourceSource.StringPtr);
			break;
		}
		for (UInt8 i = 0; i < irq->InterruptCount; i++) {
			add_interrupt(c, irq->Interrupts[i], irq->Triggering, irq->Polarity, irq->Shareable, irq->WakeCapable);
		}
		break;
	}
	case ACPI_RESOURCE_TYPE_IRQ: {
		const ACPI_RESOURCE_IRQ *irq = &res->Data.Irq;
		for (UInt8 i = 0; i < irq->InterruptCount; i++) {
			add_interrupt(c, irq->Interrupts[i], irq->Triggering, irq->Polarity, irq->Shareable, irq->WakeCapable);
		}
		break;
	}
	case ACPI_RESOURCE_TYPE_GPIO:
		append(c->summary, c->summarySize, " gpio");
		break;
	case ACPI_RESOURCE_TYPE_SERIAL_BUS:
		append(c->summary, c->summarySize, " serial-bus");
		break;
	default:
		break;
	}
	return AE_OK;
}

void
NeoDarwinACPIPlatform::addResources(IOACPIPlatformDevice *nub, void *handle, bool bridge, char *summary, size_t summarySize)
{
	ResourceContext c = {};
	c.memory = OSArray::withCapacity(2);
	c.specifiers = OSArray::withCapacity(2);
	c.controllers = OSArray::withCapacity(2);
	c.interrupts = OSData::withCapacity(8);
	c.flags = OSData::withCapacity(8);
	c.windows = OSArray::withCapacity(4);
	c.gic = gicName;
	c.bridge = bridge;
	c.summary = summary;
	c.summarySize = summarySize;
	if (c.memory && c.specifiers && c.controllers && c.interrupts && c.flags && c.windows) {
		ACPI_STATUS status = AcpiWalkResources(handle, (char *)METHOD_NAME__CRS, acpi_resource_walk, &c);
		if (ACPI_FAILURE(status) && status != AE_NOT_FOUND) {
			append(summary, summarySize, " (_CRS: %s)", acpi_error(status));
		}
		if (c.memory->getCount() != 0) {
			nub->setDeviceMemory(c.memory);
		}
		if (c.interrupts->getLength() != 0) {
			nub->setProperty("interrupts", c.interrupts);
			nub->setProperty("acpi-interrupt-flags", c.flags);
			if (c.gic != NULL) {
				nub->setProperty(gIOInterruptSpecifiersKey, c.specifiers);
				nub->setProperty(gIOInterruptControllersKey, c.controllers);
				nub->setProperty("interrupt-parent", &gicPHandle, sizeof(gicPHandle));
			}
		}
		if (c.hasBus) {
			UInt32 range[2] = { c.busMin, c.busMax };
			nub->setProperty("acpi-bus-range", range, sizeof(range));
		}
		if (c.windows->getCount() != 0) {
			nub->setProperty("acpi-windows", c.windows);
		}
	}
	OSSafeReleaseNULL(c.memory);
	OSSafeReleaseNULL(c.specifiers);
	OSSafeReleaseNULL(c.controllers);
	OSSafeReleaseNULL(c.interrupts);
	OSSafeReleaseNULL(c.flags);
	OSSafeReleaseNULL(c.windows);
}

// NeoDarwinPlatformExpert's entry point (kernel/neodarwin/platform), which
// does not see this directory's headers.
void
nd_acpi_platform_start(IOService *platformExpert)
{
	NeoDarwinACPIPlatform::startFromPlatformExpert(platformExpert);
}
