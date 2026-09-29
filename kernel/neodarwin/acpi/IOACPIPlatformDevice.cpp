// SPDX-License-Identifier: BSD-2-Clause
// NeoDarwin-Language: expressibility: IOKit classes are C++; this implements an IOService subclass over ACPICA's C interface.
//
// IOACPIPlatformDevice: the nub for one ACPI device (docs/kernel/acpi.md).
// The interface is Apple's (IOKit/acpi/IOACPIPlatformDevice.h, a subset);
// the implementation evaluates through ACPICA.

#include <IOKit/acpi/IOACPIPlatformDevice.h>
#include "NeoDarwinACPIPlatform.h"
#include "nd_acpica.h"
#include <IOKit/IOLib.h>

#define super IOPlatformDevice
OSDefineMetaClassAndStructors(IOACPIPlatformDevice, IOPlatformDevice);

bool
IOACPIPlatformDevice::init(IOService *platform, void *handle, OSDictionary *properties)
{
	if (!super::init(properties)) {
		return false;
	}
	_platform = platform;
	_deviceHandle = handle;
	_deviceType = kTypeDevice;
	OSNumber *sta = OSDynamicCast(OSNumber, getProperty(gIOACPIDeviceStatusKey));
	_deviceStatus = sta != NULL ? sta->unsigned32BitValue() : 0x0F;
	return true;
}

// IONameMatch: the registry name (the ACPI name segment), _HID and each _CID.
bool
IOACPIPlatformDevice::compareName(OSString *name, OSString **matched) const
{
	bool found = false;
	const OSSymbol *own = copyName();
	if (own != NULL) {
		found = name->isEqualTo(own);
		own->release();
	}
	OSData *compatible = OSDynamicCast(OSData, getProperty("compatible"));
	if (!found && compatible != NULL) {
		const char *p = (const char *)compatible->getBytesNoCopy();
		const char *end = p + compatible->getLength();
		while (!found && p < end) {
			size_t n = strnlen(p, (size_t)(end - p));
			found = name->isEqualTo(p) && n == name->getLength();
			p += n + 1;
		}
	}
	if (found && matched != NULL) {
		name->retain();
		*matched = name;
	}
	return found;
}

IOReturn
IOACPIPlatformDevice::getResources(void)
{
	return kIOReturnSuccess;
}

void *
IOACPIPlatformDevice::getDeviceHandle(void) const
{
	return _deviceHandle;
}

UInt32
IOACPIPlatformDevice::getDeviceStatus(void) const
{
	return _deviceStatus;
}

UInt32
IOACPIPlatformDevice::getDeviceType(void) const
{
	return _deviceType;
}

void
IOACPIPlatformDevice::setDeviceType(UInt32 deviceType)
{
	_deviceType = deviceType;
}

// ------------------------------------------------------------------------
// Evaluation

static IOReturn
acpi_ioreturn(ACPI_STATUS status)
{
	switch (status) {
	case AE_OK:            return kIOReturnSuccess;
	case AE_NOT_FOUND:     return kIOReturnNotFound;
	case AE_NO_MEMORY:     return kIOReturnNoMemory;
	case AE_TYPE:
	case AE_BAD_PARAMETER:
	case AE_AML_UNINITIALIZED_ARG:
	case AE_AML_OPERAND_TYPE: return kIOReturnBadArgument;
	case AE_TIME:          return kIOReturnTimeout;
	case AE_SUPPORT:
	case AE_NOT_CONFIGURED: return kIOReturnUnsupported;
	default:               return kIOReturnError;
	}
}

// OSObject arguments as ACPI objects. A package's elements go in `pool`,
// which the caller frees; strings and buffers point into the OSObjects.
static bool
to_acpi(OSObject *o, ACPI_OBJECT *out, ACPI_OBJECT **pool, UInt32 *poolUsed, UInt32 poolSize)
{
	if (OSNumber *n = OSDynamicCast(OSNumber, o)) {
		out->Type = ACPI_TYPE_INTEGER;
		out->Integer.Value = n->unsigned64BitValue();
	} else if (OSBoolean *b = OSDynamicCast(OSBoolean, o)) {
		out->Type = ACPI_TYPE_INTEGER;
		out->Integer.Value = b->isTrue() ? 1 : 0;
	} else if (OSString *s = OSDynamicCast(OSString, o)) {
		out->Type = ACPI_TYPE_STRING;
		out->String.Length = s->getLength();
		out->String.Pointer = (char *)s->getCStringNoCopy();
	} else if (OSData *d = OSDynamicCast(OSData, o)) {
		out->Type = ACPI_TYPE_BUFFER;
		out->Buffer.Length = d->getLength();
		out->Buffer.Pointer = (UINT8 *)d->getBytesNoCopy();
	} else if (OSArray *a = OSDynamicCast(OSArray, o)) {
		UInt32 count = a->getCount();
		if (count > poolSize - *poolUsed) {
			return false;
		}
		ACPI_OBJECT *elements = *pool + *poolUsed;
		*poolUsed += count;
		out->Type = ACPI_TYPE_PACKAGE;
		out->Package.Count = count;
		out->Package.Elements = elements;
		for (UInt32 i = 0; i < count; i++) {
			if (!to_acpi(a->getObject(i), &elements[i], pool, poolUsed, poolSize)) {
				return false;
			}
		}
	} else {
		return false;
	}
	return true;
}

// Counts the ACPI objects a package argument needs.
static UInt32
package_elements(OSObject *o)
{
	OSArray *a = OSDynamicCast(OSArray, o);
	if (a == NULL) {
		return 0;
	}
	UInt32 n = a->getCount();
	for (UInt32 i = 0; i < a->getCount(); i++) {
		n += package_elements(a->getObject(i));
	}
	return n;
}

// An ACPI result as an OSObject (retained), or NULL for a type with no
// counterpart. A reference (a package naming a device, as _PRT does)
// becomes the object's path.
static OSObject *
from_acpi(const ACPI_OBJECT *obj)
{
	switch (obj->Type) {
	case ACPI_TYPE_INTEGER:
		return OSNumber::withNumber(obj->Integer.Value, 64);
	case ACPI_TYPE_STRING:
		return OSString::withCString(obj->String.Pointer);
	case ACPI_TYPE_BUFFER:
		return OSData::withBytes(obj->Buffer.Pointer, obj->Buffer.Length);
	case ACPI_TYPE_PACKAGE: {
		OSArray *a = OSArray::withCapacity(obj->Package.Count ? obj->Package.Count : 1);
		if (a == NULL) {
			return NULL;
		}
		for (UInt32 i = 0; i < obj->Package.Count; i++) {
			OSObject *e = from_acpi(&obj->Package.Elements[i]);
			if (e == NULL) {
				e = OSNumber::withNumber((unsigned long long)0, 64);
			}
			if (e != NULL) {
				a->setObject(e);
				e->release();
			}
		}
		return a;
	}
	case ACPI_TYPE_LOCAL_REFERENCE:
	case ACPI_TYPE_ANY: {
		if (obj->Reference.Handle == NULL) {
			return NULL;
		}
		char path[128];
		ACPI_BUFFER buf = { sizeof(path), path };
		if (AcpiGetName(obj->Reference.Handle, ACPI_FULL_PATHNAME_NO_TRAILING, &buf) != AE_OK) {
			return NULL;
		}
		return OSString::withCString(path);
	}
	default:
		return NULL;
	}
}

IOReturn
IOACPIPlatformDevice::validateObject(const char *objectName)
{
	ACPI_HANDLE h;
	if (objectName == NULL) {
		return kIOReturnBadArgument;
	}
	return acpi_ioreturn(AcpiGetHandle(_deviceHandle, (char *)objectName, &h));
}

IOReturn
IOACPIPlatformDevice::validateObject(const OSSymbol *objectName)
{
	return objectName != NULL ? validateObject(objectName->getCStringNoCopy()) : kIOReturnBadArgument;
}

#define ND_ACPI_MAX_ARGS 7     // ACPI_METHOD_NUM_ARGS

IOReturn
IOACPIPlatformDevice::evaluateObject(const char *objectName, OSObject **result, OSObject *params[],
    IOItemCount paramCount, IOOptionBits options)
{
	(void)options;
	if (objectName == NULL || paramCount > ND_ACPI_MAX_ARGS || (paramCount != 0 && params == NULL)) {
		return kIOReturnBadArgument;
	}
	if (result != NULL) {
		*result = NULL;
	}
	ACPI_OBJECT args[ND_ACPI_MAX_ARGS];
	UInt32 poolSize = 0, poolUsed = 0;
	for (IOItemCount i = 0; i < paramCount; i++) {
		poolSize += package_elements(params[i]);
	}
	ACPI_OBJECT *pool = poolSize != 0 ? IONew(ACPI_OBJECT, poolSize) : NULL;
	if (poolSize != 0 && pool == NULL) {
		return kIOReturnNoMemory;
	}
	IOReturn ret = kIOReturnSuccess;
	for (IOItemCount i = 0; i < paramCount && ret == kIOReturnSuccess; i++) {
		if (!to_acpi(params[i], &args[i], &pool, &poolUsed, poolSize)) {
			ret = kIOReturnBadArgument;
		}
	}
	if (ret == kIOReturnSuccess) {
		ACPI_OBJECT_LIST list = { (UINT32)paramCount, paramCount != 0 ? args : NULL };
		ACPI_BUFFER out = { ACPI_ALLOCATE_BUFFER, NULL };
		ret = acpi_ioreturn(AcpiEvaluateObject(_deviceHandle, (char *)objectName, &list,
		    result != NULL ? &out : NULL));
		if (ret == kIOReturnSuccess && result != NULL && out.Pointer != NULL) {
			*result = from_acpi((ACPI_OBJECT *)out.Pointer);
			if (*result == NULL) {
				ret = kIOReturnUnsupported;
			}
		}
		if (out.Pointer != NULL) {
			AcpiOsFree(out.Pointer);
		}
	}
	if (pool != NULL) {
		IODelete(pool, ACPI_OBJECT, poolSize);
	}
	return ret;
}

IOReturn
IOACPIPlatformDevice::evaluateObject(const OSSymbol *objectName, OSObject **result, OSObject *params[],
    IOItemCount paramCount, IOOptionBits options)
{
	return objectName != NULL ? evaluateObject(objectName->getCStringNoCopy(), result, params, paramCount, options)
	       : kIOReturnBadArgument;
}

IOReturn
IOACPIPlatformDevice::evaluateInteger(const char *objectName, UInt64 *resultInt64, OSObject *params[],
    IOItemCount paramCount, IOOptionBits options)
{
	if (resultInt64 == NULL) {
		return kIOReturnBadArgument;
	}
	OSObject *result;
	IOReturn ret = evaluateObject(objectName, &result, params, paramCount, options);
	if (ret != kIOReturnSuccess) {
		return ret;
	}
	OSNumber *n = OSDynamicCast(OSNumber, result);
	if (n != NULL) {
		*resultInt64 = n->unsigned64BitValue();
	} else {
		ret = kIOReturnBadArgument;
	}
	result->release();
	return ret;
}

IOReturn
IOACPIPlatformDevice::evaluateInteger(const OSSymbol *objectName, UInt64 *resultInt64, OSObject *params[],
    IOItemCount paramCount, IOOptionBits options)
{
	return objectName != NULL ? evaluateInteger(objectName->getCStringNoCopy(), resultInt64, params, paramCount, options)
	       : kIOReturnBadArgument;
}

IOReturn
IOACPIPlatformDevice::evaluateInteger(const char *objectName, UInt32 *resultInt32, OSObject *params[],
    IOItemCount paramCount, IOOptionBits options)
{
	if (resultInt32 == NULL) {
		return kIOReturnBadArgument;
	}
	UInt64 v;
	IOReturn ret = evaluateInteger(objectName, &v, params, paramCount, options);
	if (ret == kIOReturnSuccess) {
		*resultInt32 = (UInt32)v;
	}
	return ret;
}

IOReturn
IOACPIPlatformDevice::evaluateInteger(const OSSymbol *objectName, UInt32 *resultInt32, OSObject *params[],
    IOItemCount paramCount, IOOptionBits options)
{
	return objectName != NULL ? evaluateInteger(objectName->getCStringNoCopy(), resultInt32, params, paramCount, options)
	       : kIOReturnBadArgument;
}

const OSData *
IOACPIPlatformDevice::getACPITableData(const char *tableName, UInt32 tableInstance) const
{
	NeoDarwinACPIPlatform *platform = OSDynamicCast(NeoDarwinACPIPlatform, _platform);
	return platform != NULL ? platform->getTableData(tableName, tableInstance) : NULL;
}

IOReturn
IOACPIPlatformDevice::acquireGlobalLock(UInt32 *lockToken, const mach_timespec_t *timeout)
{
	(void)timeout;
	if (lockToken == NULL) {
		return kIOReturnBadArgument;
	}
	*lockToken = 0;
	return kIOReturnSuccess;
}

void
IOACPIPlatformDevice::releaseGlobalLock(UInt32 lockToken)
{
	(void)lockToken;
}

// ------------------------------------------------------------------------
// PCI legacy interrupts (docs/kernel/pci.md)
//
// IOPCIFamily resolves a device's INTx by sending "ResolvePCIInterrupt" to
// its bridge's provider, which passes it up the provider chain to the host
// bridge's ACPI nub. The first parameter is the provider the request
// started at: this nub for a device on the root bus, else the IOPCIDevice
// of the PCI-to-PCI bridge (a root port) the device is behind. Across each
// such bridge the pin is swizzled (PCI-to-PCI Bridge 1.2 §9.1: the
// bridge's pin for its child's is (pin + device) mod 4) and the bridge's
// own device number taken, up to the root bus, where _PRT maps (device,
// pin) to either a GSIV (source 0) or a link device whose _CRS holds it.
// _PRT on the bridges themselves (ACPI 6.5 §6.2.13) is not consulted:
// neither QEMU nor the Q8B has one.

#define ND_PCI_RESOLVE_INTERRUPT "ResolvePCIInterrupt"
#define ND_PCI_SET_INTERRUPTS    "SetDeviceInterrupts"

IOReturn
IOACPIPlatformDevice::resolvePCIInterrupt(IOService *requester, UInt32 device, UInt32 pin, UInt32 *gsiv)
{
	IOService *p = requester;
	for (int depth = 0; p != NULL && p != this && depth < 32; depth++) {
		// A bridge's IOPCIDevice: "reg" starts with its address space
		// (IOPCIAddressSpace: device number in bits 11-15).
		OSData *reg = OSDynamicCast(OSData, p->getProperty("reg"));
		if (reg == NULL || reg->getLength() < sizeof(UInt32)) {
			return kIOReturnNotFound;
		}
		UInt32 space = *(const UInt32 *)reg->getBytesNoCopy();
		pin = (pin + device) % 4;
		device = (space >> 11) & 0x1f;
		IOService *bridge = p->getProvider();     // the IOPCIBridge that published it
		p = bridge != NULL ? bridge->getProvider() : NULL;
	}
	if (p != this) {
		return kIOReturnNotFound;
	}
	OSObject *result = NULL;
	IOReturn ret = evaluateObject("_PRT", &result);
	OSArray *prt = OSDynamicCast(OSArray, result);
	if (ret != kIOReturnSuccess || prt == NULL) {
		OSSafeReleaseNULL(result);
		return kIOReturnNotFound;
	}
	ret = kIOReturnNotFound;
	for (unsigned int i = 0; i < prt->getCount(); i++) {
		OSArray *e = OSDynamicCast(OSArray, prt->getObject(i));
		OSNumber *address = e != NULL && e->getCount() == 4 ? OSDynamicCast(OSNumber, e->getObject(0)) : NULL;
		OSNumber *entryPin = address != NULL ? OSDynamicCast(OSNumber, e->getObject(1)) : NULL;
		OSNumber *index = address != NULL ? OSDynamicCast(OSNumber, e->getObject(3)) : NULL;
		if (entryPin == NULL || index == NULL) {
			continue;
		}
		// _ADR form: device in the high word; function 0xFFFF (any).
		if ((address->unsigned64BitValue() >> 16) != device || entryPin->unsigned32BitValue() != pin) {
			continue;
		}
		OSString *link = OSDynamicCast(OSString, e->getObject(2));
		if (link == NULL) {
			*gsiv = index->unsigned32BitValue();
			ret = kIOReturnSuccess;
		} else {
			NeoDarwinACPIPlatform *platform = OSDynamicCast(NeoDarwinACPIPlatform, _platform);
			IOACPIPlatformDevice *linkNub = platform != NULL ? platform->nubForPath(link->getCStringNoCopy()) : NULL;
			OSData *ints = linkNub != NULL ? OSDynamicCast(OSData, linkNub->getProperty("interrupts")) : NULL;
			UInt32 n = index->unsigned32BitValue();
			if (ints != NULL && (n + 1) * sizeof(UInt32) <= ints->getLength()) {
				*gsiv = ((const UInt32 *)ints->getBytesNoCopy())[n];
				ret = kIOReturnSuccess;
			}
		}
		break;
	}
	result->release();
	return ret;
}

// The GSIVs as the device's first interrupt specifiers, on the GIC. PCI INTx
// is level-sensitive and may be shared by several devices. IOPCIFamily has
// already put empty specifier arrays on the device (IOPCIDevice::getProperty),
// to which checkpoint 3's MSIs will be appended; INTx is source 0.
IOReturn
IOACPIPlatformDevice::setPCIDeviceInterrupts(IOService *device, const UInt32 *gsivs, UInt32 count)
{
	NeoDarwinACPIPlatform *platform = OSDynamicCast(NeoDarwinACPIPlatform, _platform);
	const OSSymbol *gic = platform != NULL ? platform->getGICName() : NULL;
	if (device == NULL || gsivs == NULL || gic == NULL) {
		return kIOReturnBadArgument;
	}
	OSObject *oldSpecs = device->copyProperty(gIOInterruptSpecifiersKey);
	OSObject *oldControllers = device->copyProperty(gIOInterruptControllersKey);
	OSArray *specs = OSArray::withCapacity(count + 1);
	OSArray *controllers = OSArray::withCapacity(count + 1);
	IOReturn ret = kIOReturnNoMemory;
	if (specs != NULL && controllers != NULL) {
		ret = kIOReturnSuccess;
		for (UInt32 i = 0; i < count && ret == kIOReturnSuccess; i++) {
			UInt32 cells[2] = { gsivs[i], ND_ACPI_IRQ_SHARED };
			OSData *spec = OSData::withBytes(cells, sizeof(cells));
			if (spec == NULL || !specs->setObject(spec) || !controllers->setObject(gic)) {
				ret = kIOReturnNoMemory;
			}
			OSSafeReleaseNULL(spec);
		}
		// Anything already there (none today) follows.
		if (OSArray *a = OSDynamicCast(OSArray, oldSpecs)) {
			specs->merge(a);
		}
		if (OSArray *a = OSDynamicCast(OSArray, oldControllers)) {
			controllers->merge(a);
		}
		if (ret == kIOReturnSuccess) {
			device->setProperty(gIOInterruptSpecifiersKey, specs);
			device->setProperty(gIOInterruptControllersKey, controllers);
		}
	}
	OSSafeReleaseNULL(specs);
	OSSafeReleaseNULL(controllers);
	OSSafeReleaseNULL(oldSpecs);
	OSSafeReleaseNULL(oldControllers);
	return ret;
}

IOReturn
IOACPIPlatformDevice::callPlatformFunction(const OSSymbol *functionName, bool waitForFunction,
    void *param1, void *param2, void *param3, void *param4)
{
	if (functionName != NULL && validateObject("_PRT") == kIOReturnSuccess) {
		if (functionName->isEqualTo(ND_PCI_RESOLVE_INTERRUPT)) {
			return param4 != NULL ? resolvePCIInterrupt((IOService *)param1, (UInt32)(uintptr_t)param2,
			           (UInt32)(uintptr_t)param3, (UInt32 *)param4) : kIOReturnBadArgument;
		}
		if (functionName->isEqualTo(ND_PCI_SET_INTERRUPTS)) {
			return setPCIDeviceInterrupts((IOService *)param1, (const UInt32 *)param2, (UInt32)(uintptr_t)param3);
		}
	}
	return super::callPlatformFunction(functionName, waitForFunction, param1, param2, param3, param4);
}
