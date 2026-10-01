// SPDX-License-Identifier: BSD-2-Clause
// NeoDarwin-Language: expressibility: register and request layouts shared with the kernel's C++ IOKit drivers, which include this header.
//
// The virtio block device (virtio 1.2 §5.2): its feature bits,
// configuration and request layout. The transport and the split
// virtqueue are in nd_virtio.h (kernel/neodarwin/virtio).
#ifndef ND_VIRTIO_BLK_H
#define ND_VIRTIO_BLK_H

#include <stdint.h>

#include "nd_virtio.h"

enum : uint64_t {
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

#endif
