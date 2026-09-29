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
