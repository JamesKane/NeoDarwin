// SPDX-License-Identifier: BSD-2-Clause
// NeoDarwin-Language: expressibility: register and ring layouts shared with the kernel's C++ IOKit drivers, which include this header.
//
// Virtio 1.x (OASIS Virtual I/O Device, version 1.2) as NeoDarwin's
// drivers use it: the PCI transport's capabilities and common
// configuration (§4.1.4), device status and feature bits (§2.1, §6), the
// split virtqueue (§2.7) and the block device (§5.2). Offsets are in bytes;
// everything in memory is little-endian, as the CPU is.
#ifndef ND_VIRTIO_H
#define ND_VIRTIO_H

#include <stdint.h>

// PCI: vendor 0x1af4; modern device IDs are 0x1040 + the virtio device ID,
// transitional ones are 0x1000-0x103f (block: 0x1001), which also carry
// the modern capabilities.
enum {
	kNDVirtioPCIVendor = 0x1af4,
	kNDVirtioPCIBlockModern = 0x1042,
	kNDVirtioPCIBlockTransitional = 0x1001,
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

// Feature bits: the device-independent ones, then the block device's.
enum : uint64_t {
	kNDVirtioFRingIndirect = 1ULL << 28,
	kNDVirtioFRingEventIdx = 1ULL << 29,
	kNDVirtioFVersion1 = 1ULL << 32,
	kNDVirtioFAccessPlatform = 1ULL << 33,
	kNDVirtioFRingPacked = 1ULL << 34,
	kNDVirtioFOrderPlatform = 1ULL << 36,

	kNDVirtioBlkFSizeMax = 1ULL << 1,
	kNDVirtioBlkFSegMax = 1ULL << 2,
	kNDVirtioBlkFGeometry = 1ULL << 4,
	kNDVirtioBlkFRO = 1ULL << 5,
	kNDVirtioBlkFBlkSize = 1ULL << 6,
	kNDVirtioBlkFFlush = 1ULL << 9,
	kNDVirtioBlkFTopology = 1ULL << 10,
	kNDVirtioBlkFConfigWCE = 1ULL << 11,
	kNDVirtioBlkFMQ = 1ULL << 12,
	kNDVirtioBlkFDiscard = 1ULL << 13,
	kNDVirtioBlkFWriteZeroes = 1ULL << 14,
};

// struct virtio_blk_config (device configuration).
enum {
	kNDVirtioBlkCapacity = 0,       // u64, in 512-byte sectors
	kNDVirtioBlkSizeMax = 8,        // u32
	kNDVirtioBlkSegMax = 12,        // u32
	kNDVirtioBlkBlkSize = 20,       // u32
	kNDVirtioBlkPhysicalExp = 24,   // u8: topology, log2 of blocks per physical block
	kNDVirtioBlkWriteback = 32,     // u8: CONFIG_WCE
};

// Requests: a 16-byte header the device reads, the data, and a status byte
// it writes. Sectors are 512 bytes whatever the block size.
struct nd_virtio_blk_req_header {
	uint32_t type;
	uint32_t reserved;
	uint64_t sector;
};
enum {
	kNDVirtioBlkTIn = 0,
	kNDVirtioBlkTOut = 1,
	kNDVirtioBlkTFlush = 4,
	kNDVirtioBlkTGetID = 8,
};
enum {
	kNDVirtioBlkSOK = 0,
	kNDVirtioBlkSIOErr = 1,
	kNDVirtioBlkSUnsupp = 2,
};
static const uint32_t kNDVirtioBlkSectorSize = 512;
static const uint32_t kNDVirtioBlkIDBytes = 20;

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

#endif
