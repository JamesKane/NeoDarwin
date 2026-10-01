// SPDX-License-Identifier: BSD-2-Clause
// NeoDarwin-Language: expressibility: register and packet header layouts shared with the kernel's C++ IOKit driver, which includes this header.
//
// The virtio network device (virtio 1.2 §5.1): its feature bits, its
// configuration and the header in front of every packet. The transport and
// the split virtqueue are in nd_virtio.h (kernel/neodarwin/virtio).
#ifndef ND_VIRTIO_NET_H
#define ND_VIRTIO_NET_H

#include <stdint.h>

#include "nd_virtio.h"

enum : uint64_t {
	kNDVirtioNetFCsum = 1ULL << 0,              // the device checksums what the driver sends
	kNDVirtioNetFGuestCsum = 1ULL << 1,         // the driver accepts packets with partial checksums
	kNDVirtioNetFCtrlGuestOffloads = 1ULL << 2,
	kNDVirtioNetFMTU = 1ULL << 3,
	kNDVirtioNetFMAC = 1ULL << 5,
	kNDVirtioNetFGuestTSO4 = 1ULL << 7,
	kNDVirtioNetFGuestTSO6 = 1ULL << 8,
	kNDVirtioNetFGuestECN = 1ULL << 9,
	kNDVirtioNetFGuestUFO = 1ULL << 10,
	kNDVirtioNetFHostTSO4 = 1ULL << 11,
	kNDVirtioNetFHostTSO6 = 1ULL << 12,
	kNDVirtioNetFHostECN = 1ULL << 13,
	kNDVirtioNetFHostUFO = 1ULL << 14,
	kNDVirtioNetFMrgRxbuf = 1ULL << 15,
	kNDVirtioNetFStatus = 1ULL << 16,
	kNDVirtioNetFCtrlVQ = 1ULL << 17,
	kNDVirtioNetFCtrlRx = 1ULL << 18,
	kNDVirtioNetFCtrlVLAN = 1ULL << 19,
	kNDVirtioNetFGuestAnnounce = 1ULL << 21,
	kNDVirtioNetFMQ = 1ULL << 22,
	kNDVirtioNetFCtrlMACAddr = 1ULL << 23,
	kNDVirtioNetFSpeedDuplex = 1ULL << 63,
};

// struct virtio_net_config (device configuration).
enum {
	kNDVirtioNetConfigMAC = 0,              // u8[6], with VIRTIO_NET_F_MAC
	kNDVirtioNetConfigStatus = 6,           // u16, with VIRTIO_NET_F_STATUS
	kNDVirtioNetConfigMaxPairs = 8,         // u16, with VIRTIO_NET_F_MQ
	kNDVirtioNetConfigMTU = 10,             // u16, with VIRTIO_NET_F_MTU
	kNDVirtioNetConfigSpeed = 12,           // u32 Mb/s, with VIRTIO_NET_F_SPEED_DUPLEX
	kNDVirtioNetConfigDuplex = 16,          // u8: 0 half, 1 full, 0xff unknown
	kNDVirtioNetConfigMinLength = 8,        // what this driver reads (MAC and status)
};
enum {
	kNDVirtioNetStatusLinkUp = 1,
	kNDVirtioNetStatusAnnounce = 2,
};
static const uint32_t kNDVirtioNetSpeedUnknown = 0xffffffff;

// The queues of a device without VIRTIO_NET_F_MQ: receiveq1, transmitq1,
// then the control queue with VIRTIO_NET_F_CTRL_VQ.
enum {
	kNDVirtioNetQueueRx = 0,
	kNDVirtioNetQueueTx = 1,
	kNDVirtioNetQueueCtrl = 2,
};

// struct virtio_net_hdr, in front of every packet in both directions. With
// VIRTIO_F_VERSION_1 it always has num_buffers: 12 bytes (§5.1.6).
struct nd_virtio_net_hdr {
	uint8_t flags;
	uint8_t gso_type;
	uint16_t hdr_len;
	uint16_t gso_size;
	uint16_t csum_start;
	uint16_t csum_offset;
	uint16_t num_buffers;
};
static const uint32_t kNDVirtioNetHeaderBytes = 12;
enum {
	kNDVirtioNetHdrFNeedsCsum = 1,
	kNDVirtioNetHdrFDataValid = 2,
	kNDVirtioNetHdrGSONone = 0,
};

// Without VIRTIO_NET_F_MRG_RXBUF or the guest offloads, a receive buffer
// must hold the header and a whole frame (§5.1.6.3.1: at least 1526
// bytes): 1514 bytes of Ethernet frame, and 4 more for a VLAN tag.
static const uint32_t kNDVirtioNetMaxFrame = 1518;

#endif
