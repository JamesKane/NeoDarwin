// SPDX-License-Identifier: BSD-2-Clause
// NeoDarwin-Language: expressibility: IOKit C++ helpers for the kernel's storage drivers (IODMACommand, IOBufferMemoryDescriptor).
//
// How NeoDarwin's PCI storage drivers (virtio-blk now, NVMe next) reach
// memory, from what the host bridge put on their IOPCIDevice
// (docs/kernel/pci.md, "DMA coherence and the IORT"):
//
//   dma-coherent       true when the root complex snoops the CPU's caches
//                      (_CCA or the IORT); otherwise every buffer the
//                      device reads is cleaned to the point of coherency
//                      first, and every buffer it writes is invalidated
//                      before the CPU reads it
//   dma-address-bits   the root complex's reach (the IORT memory size
//                      limit; 36 on the Radxa Dragon Q8B): queues are
//                      allocated below it, and IODMACommand bounces data
//                      buffers above it
//
// There is no IOMMU driver: device addresses are physical addresses.
#ifndef ND_STORAGE_DMA_H
#define ND_STORAGE_DMA_H

#include <IOKit/IOBufferMemoryDescriptor.h>
#include <IOKit/IODMACommand.h>
#include <IOKit/IOService.h>

extern "C" void FlushPoC_DcacheRegion(vm_offset_t va, size_t length);

struct NDStorageDMA {
	uint8_t addressBits = 48;
	bool coherent = false;

	static NDStorageDMA
	forDevice(IOService *device)
	{
		NDStorageDMA d;
		OSNumber *bits = OSDynamicCast(OSNumber, device->getProperty("dma-address-bits"));
		if (bits != NULL && bits->unsigned32BitValue() >= 32 && bits->unsigned32BitValue() <= 64) {
			d.addressBits = (uint8_t)bits->unsigned32BitValue();
		}
		d.coherent = device->getProperty("dma-coherent") == kOSBooleanTrue;
		return d;
	}

	// Physically contiguous, zeroed, below the address limit, page aligned:
	// rings, queues and request headers. Cleaned when the device isn't
	// coherent, so no dirty line can later overwrite what the device writes.
	IOBufferMemoryDescriptor *
	allocate(size_t bytes) const
	{
		mach_vm_address_t limit = addressBits >= 64 ? ~0ULL : ((1ULL << addressBits) - 1);
		IOBufferMemoryDescriptor *m = IOBufferMemoryDescriptor::inTaskWithPhysicalMask(kernel_task,
		    kIODirectionInOut | kIOMemoryHostPhysicallyContiguous | kIOMemoryMapperNone, bytes,
		    limit & ~(mach_vm_address_t)(PAGE_SIZE - 1));
		if (m == NULL) {
			return NULL;
		}
		if (m->prepare() != kIOReturnSuccess) {
			m->release();
			return NULL;
		}
		bzero(m->getBytesNoCopy(), bytes);
		if (!coherent) {
			FlushPoC_DcacheRegion((vm_offset_t)m->getBytesNoCopy(), bytes);
		}
		return m;
	}

	static uint64_t
	physical(IOBufferMemoryDescriptor *m, IOByteCount offset = 0)
	{
		return m->getPhysicalSegment(offset, NULL, kIOMemoryMapperNone);
	}

	// Before the device reads what the CPU wrote.
	void
	toDevice(const volatile void *p, size_t bytes) const
	{
		if (coherent) {
			__builtin_arm_dmb(0x2);     // DMB OSHST: the writes before what follows
		} else {
			FlushPoC_DcacheRegion((vm_offset_t)p, bytes);
		}
	}

	// Before the CPU reads what the device wrote. The lines are clean (they
	// were cleaned when the CPU last wrote them), so cleaning and
	// invalidating writes nothing back.
	void
	fromDevice(const volatile void *p, size_t bytes) const
	{
		if (coherent) {
			__builtin_arm_dmb(0x1);     // DMB OSHLD
		} else {
			FlushPoC_DcacheRegion((vm_offset_t)p, bytes);
		}
	}

	// An IODMACommand for data buffers: 64-bit segments within the address
	// limit (bounced above it), cache maintenance on prepare and complete
	// when not coherent, and no system mapper.
	IODMACommand *
	newCommand(uint64_t maxSegmentBytes, uint64_t maxTransferBytes) const
	{
		return IODMACommand::withSpecification(kIODMACommandOutputHost64, addressBits, maxSegmentBytes,
		           coherent ? IODMACommand::kUnmapped : IODMACommand::kNonCoherent, maxTransferBytes, 1);
	}
};

#endif
