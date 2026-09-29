// SPDX-License-Identifier: BSD-2-Clause
// NeoDarwin-Language: expressibility: IOKit drivers are C++ IOService subclasses (IOBlockStorageDevice).
//
// One NVMe namespace as an IOStorageFamily block storage device
// (docs/kernel/storage.md, "NVMe"). NeoDarwinNVMeController creates one per
// active namespace with a supported LBA format (512 bytes to 64 KiB, no
// metadata) and attaches it; IOBlockStorageDriver matches it once it
// registers. Reads, writes and flushes go to the controller, which runs
// them on its work loop.
//
// The namespace is write-protected when the controller's write policy says
// so (every controller but QEMU's, unless nd_nvme_rw=1) or when Identify
// Namespace says the controller write-protects it: IOMedia is then not
// writable, a write open of /dev/diskN fails (EACCES) and a file system
// mounts read-only, and a write that still arrives is refused here and in
// the controller.

#include <IOKit/IOKitKeys.h>
#include <IOKit/IOLib.h>
#include <IOKit/storage/IOStorage.h>
#include <IOKit/storage/IOStorageDeviceCharacteristics.h>
#include <IOKit/storage/IOStorageProtocolCharacteristics.h>

#include "NeoDarwinNVMe.h"

#define super IOBlockStorageDevice
OSDefineMetaClassAndStructors(NeoDarwinNVMeNamespace, IOBlockStorageDevice);

bool
NeoDarwinNVMeNamespace::initWith(NeoDarwinNVMeController *c, uint32_t id, uint32_t shift, uint64_t count, bool protect)
{
	if (!super::init(NULL)) {
		return false;
	}
	controller = c;
	nsid = id;
	lbaShift = shift;
	blocks = count;
	writeProtected = protect;
	snprintf(info, sizeof(info), "%s namespace %u", c->location(), id);
	return true;
}

static void
setString(OSDictionary *d, const char *key, const char *value)
{
	OSString *s = OSString::withCString(value);
	if (s != NULL) {
		d->setObject(key, s);
		s->release();
	}
}

bool
NeoDarwinNVMeNamespace::start(IOService *provider)
{
	if (!super::start(provider)) {
		return false;
	}
	// What IOBlockStorageDriver needs to split requests: at most the
	// controller's transfer size, and any number of dword-aligned segments
	// (the controller builds a PRP entry per 4 KiB page).
	uint64_t maxBytes = controller->maxTransferBytes();
	setProperty(kIOMaximumByteCountReadKey, maxBytes, 64);
	setProperty(kIOMaximumByteCountWriteKey, maxBytes, 64);
	setProperty(kIOMaximumSegmentCountReadKey, maxBytes / kNDNVMePage + 1, 64);
	setProperty(kIOMaximumSegmentCountWriteKey, maxBytes / kNDNVMePage + 1, 64);
	setProperty(kIOMinimumSegmentAlignmentByteCountKey, 4, 64);
	setProperty(kIOMaximumSegmentAddressableBitCountKey, controller->addressBits(), 64);
	setProperty("NVMe Namespace", nsid, 32);

	OSDictionary *protocol = OSDictionary::withCapacity(2);
	if (protocol != NULL) {
		setString(protocol, kIOPropertyPhysicalInterconnectTypeKey, kIOPropertyPhysicalInterconnectTypePCIExpress);
		setString(protocol, kIOPropertyPhysicalInterconnectLocationKey, kIOPropertyInternalKey);
		setProperty(kIOPropertyProtocolCharacteristicsKey, protocol);
		protocol->release();
	}
	OSDictionary *device = OSDictionary::withCapacity(6);
	if (device != NULL) {
		setString(device, kIOPropertyProductNameKey, controller->model());
		setString(device, kIOPropertyProductRevisionLevelKey, controller->firmware());
		setString(device, kIOPropertyProductSerialNumberKey, controller->serial());
		setString(device, kIOPropertyMediumTypeKey, kIOPropertyMediumTypeSolidStateKey);
		OSNumber *logical = OSNumber::withNumber(1ULL << lbaShift, 32);
		if (logical != NULL) {
			device->setObject(kIOPropertyLogicalBlockSizeKey, logical);
			logical->release();
		}
		setProperty(kIOPropertyDeviceCharacteristicsKey, device);
		device->release();
	}
	registerService();
	return true;
}

IOReturn
NeoDarwinNVMeNamespace::doAsyncReadWrite(IOMemoryDescriptor *buffer, UInt64 block, UInt64 nblks,
    IOStorageAttributes *attributes, IOStorageCompletion *completion)
{
	if (!controller->running()) {
		return kIOReturnNotReady;
	}
	if (nblks == 0 || block + nblks < block || block + nblks > blocks ||
	    (nblks << lbaShift) > controller->maxTransferBytes()) {
		return kIOReturnBadArgument;
	}
	if (buffer->getDirection() != kIODirectionIn && writeProtected) {
		return kIOReturnNotWritable;
	}
	bool fua = attributes != NULL && (attributes->options & kIOStorageOptionForceUnitAccess) != 0;
	return controller->readWrite(this, buffer, block, nblks, fua, completion);
}

// NVMe Flush writes the volatile write cache back; without one, or on a
// namespace the driver never writes, there is nothing to flush.
IOReturn
NeoDarwinNVMeNamespace::doSynchronize(UInt64 block, UInt64 nblks, IOStorageSynchronizeOptions options)
{
	(void)block; (void)nblks; (void)options;
	if (!controller->running()) {
		return kIOReturnNotReady;
	}
	if (writeProtected || !controller->volatileWriteCache()) {
		return kIOReturnSuccess;
	}
	return controller->flush(this);
}

IOReturn
NeoDarwinNVMeNamespace::doEjectMedia(void)
{
	return kIOReturnUnsupported;
}

IOReturn
NeoDarwinNVMeNamespace::doFormatMedia(UInt64 byteCapacity)
{
	(void)byteCapacity;
	return kIOReturnUnsupported;
}

UInt32
NeoDarwinNVMeNamespace::doGetFormatCapacities(UInt64 *capacities, UInt32 capacitiesMaxCount) const
{
	if (capacities != NULL && capacitiesMaxCount > 0) {
		capacities[0] = blocks << lbaShift;
	}
	return 1;
}

char *
NeoDarwinNVMeNamespace::getVendorString(void)
{
	return (char *)"NVMe";
}

char *
NeoDarwinNVMeNamespace::getProductString(void)
{
	return (char *)controller->model();
}

char *
NeoDarwinNVMeNamespace::getRevisionString(void)
{
	return (char *)controller->firmware();
}

char *
NeoDarwinNVMeNamespace::getAdditionalDeviceInfoString(void)
{
	return info;
}

IOReturn
NeoDarwinNVMeNamespace::reportBlockSize(UInt64 *size)
{
	*size = 1ULL << lbaShift;
	return kIOReturnSuccess;
}

IOReturn
NeoDarwinNVMeNamespace::reportEjectability(bool *isEjectable)
{
	*isEjectable = false;
	return kIOReturnSuccess;
}

IOReturn
NeoDarwinNVMeNamespace::reportMaxValidBlock(UInt64 *maxBlock)
{
	*maxBlock = blocks != 0 ? blocks - 1 : 0;
	return kIOReturnSuccess;
}

IOReturn
NeoDarwinNVMeNamespace::reportMediaState(bool *mediaPresent, bool *changedState)
{
	*mediaPresent = blocks != 0;
	if (changedState != NULL) {
		*changedState = false;
	}
	return kIOReturnSuccess;
}

IOReturn
NeoDarwinNVMeNamespace::reportRemovability(bool *isRemovable)
{
	*isRemovable = false;
	return kIOReturnSuccess;
}

IOReturn
NeoDarwinNVMeNamespace::reportWriteProtection(bool *isWriteProtected)
{
	*isWriteProtected = writeProtected;
	return kIOReturnSuccess;
}

// Read with Get Features at start; changing it would be a Set Features
// that changes the drive's state, which the driver doesn't send.
IOReturn
NeoDarwinNVMeNamespace::getWriteCacheState(bool *enabled)
{
	*enabled = controller->volatileWriteCache() && controller->writeCacheEnabled();
	return kIOReturnSuccess;
}

IOReturn
NeoDarwinNVMeNamespace::setWriteCacheState(bool enabled)
{
	(void)enabled;
	return kIOReturnUnsupported;
}
