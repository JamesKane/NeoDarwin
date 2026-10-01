// SPDX-License-Identifier: BSD-2-Clause
// NeoDarwin-Language: expressibility: register and ring layouts shared with the kernel's C++ IOKit drivers, which include this header.
//
// Virtio 1.x (OASIS Virtual I/O Device, version 1.2) as NeoDarwin's
// drivers use it: the PCI transport's capabilities and common
// configuration (§4.1.4), device status and the device-independent feature
// bits (§2.1, §6), and the split virtqueue (§2.7). Each device type's
// layouts are in its own header (nd_virtio_blk.h, nd_virtio_net.h).
// Offsets are in bytes; everything in memory is little-endian, as the CPU is.
#ifndef ND_VIRTIO_H
#define ND_VIRTIO_H

#include <stdint.h>

// PCI: vendor 0x1af4; modern device IDs are 0x1040 + the virtio device ID,
// transitional ones are 0x1000-0x103f (network: 0x1000, block: 0x1001),
// which also carry the modern capabilities.
enum {
	kNDVirtioPCIVendor = 0x1af4,
	kNDVirtioPCINetTransitional = 0x1000,
	kNDVirtioPCIBlockTransitional = 0x1001,
	kNDVirtioPCINetModern = 0x1041,
	kNDVirtioPCIBlockModern = 0x1042,
};

// Vendor-specific capability (ID 0x09): struct virtio_pci_cap.
enum {
	kNDVirtioCapCfgType = 3,        // u8
	kNDVirtioCapBar = 4,            // u8
	kNDVirtioCapOffset = 8,         // u32
	kNDVirtioCapLength = 12,        // u32
	kNDVirtioCapNotifyMultiplier = 16,  // u32, notify capability only
};
enum {
	kNDVirtioCapCommon = 1,
	kNDVirtioCapNotify = 2,
	kNDVirtioCapISR = 3,
	kNDVirtioCapDevice = 4,
	kNDVirtioCapPCIConfig = 5,
};

// struct virtio_pci_common_cfg.
enum {
	kNDVirtioDeviceFeatureSelect = 0x00,    // u32
	kNDVirtioDeviceFeature = 0x04,          // u32
	kNDVirtioDriverFeatureSelect = 0x08,    // u32
	kNDVirtioDriverFeature = 0x0c,          // u32
	kNDVirtioConfigMSIXVector = 0x10,       // u16
	kNDVirtioNumQueues = 0x12,              // u16
	kNDVirtioDeviceStatus = 0x14,           // u8
	kNDVirtioConfigGeneration = 0x15,       // u8
	kNDVirtioQueueSelect = 0x16,            // u16
	kNDVirtioQueueSize = 0x18,              // u16
	kNDVirtioQueueMSIXVector = 0x1a,        // u16
	kNDVirtioQueueEnable = 0x1c,            // u16
	kNDVirtioQueueNotifyOff = 0x1e,         // u16
	kNDVirtioQueueDesc = 0x20,              // u64
	kNDVirtioQueueDriver = 0x28,            // u64 (available ring)
	kNDVirtioQueueDevice = 0x30,            // u64 (used ring)
	kNDVirtioCommonMinLength = 0x38,
};
static const uint16_t kNDVirtioNoVector = 0xffff;

// Device status.
enum {
	kNDVirtioStatusAcknowledge = 0x01,
	kNDVirtioStatusDriver = 0x02,
	kNDVirtioStatusDriverOK = 0x04,
	kNDVirtioStatusFeaturesOK = 0x08,
	kNDVirtioStatusNeedsReset = 0x40,
	kNDVirtioStatusFailed = 0x80,
};

// ISR status (INTx only): bit 0 a queue, bit 1 a configuration change.
enum {
	kNDVirtioISRQueue = 0x1,
	kNDVirtioISRConfig = 0x2,
};

// The device-independent feature bits.
enum : uint64_t {
	kNDVirtioFRingIndirect = 1ULL << 28,
	kNDVirtioFRingEventIdx = 1ULL << 29,
	kNDVirtioFVersion1 = 1ULL << 32,
	kNDVirtioFAccessPlatform = 1ULL << 33,
	kNDVirtioFRingPacked = 1ULL << 34,
	kNDVirtioFOrderPlatform = 1ULL << 36,
};

// Split virtqueue: 16-byte descriptors, the available ring (the driver's)
// and the used ring (the device's).
struct nd_virtq_desc {
	uint64_t addr;
	uint32_t len;
	uint16_t flags;
	uint16_t next;
};
enum {
	kNDVirtqDescNext = 1,
	kNDVirtqDescWrite = 2,
	kNDVirtqDescIndirect = 4,
};
// The rings, by offset: available: flags (u16) at 0, idx (u16) at 2, then
// a u16 descriptor head per entry from 4; used: flags at 0, idx at 2, then
// an 8-byte element (u32 head, u32 bytes written) per entry from 4.
enum {
	kNDVirtqRingFlags = 0,
	kNDVirtqRingIdx = 2,
	kNDVirtqRingEntries = 4,
	kNDVirtqUsedElemSize = 8,
};
// Available ring flags: the driver asks for no interrupt (a hint).
enum {
	kNDVirtqAvailNoInterrupt = 1,
};

#endif
