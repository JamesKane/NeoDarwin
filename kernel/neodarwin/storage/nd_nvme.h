// SPDX-License-Identifier: BSD-2-Clause
// NeoDarwin-Language: expressibility: register, queue and data structure layouts shared with the kernel's C++ IOKit NVMe driver, which includes this header.
//
// NVM Express as NeoDarwin's driver uses it (NVM Express Base Specification
// 1.4c; the NVM command set's Read, Write and Flush): controller registers
// (§3.1), submission and completion queue entries (§4.2, §4.6), PRPs
// (§4.3), the admin commands the driver sends (§5) and the Identify data
// it reads (§5.15). Offsets are in bytes; everything is little-endian, as
// the CPU is.
#ifndef ND_NVME_H
#define ND_NVME_H

#include <stdint.h>

#ifdef __cplusplus
#define ND_NVME_STATIC_ASSERT static_assert
#else
#define ND_NVME_STATIC_ASSERT _Static_assert
#endif

// PCI class 01h (mass storage), subclass 08h (non-volatile memory),
// programming interface 02h (NVM Express). QEMU's model is 1b36:0010.
enum {
	kNDNVMePCIClass = 0x010802,
	kNDNVMeQEMUVendor = 0x1b36,
};

// Controller registers in BAR0 (§3.1).
enum {
	kNDNVMeRegCAP = 0x00,           // u64
	kNDNVMeRegVS = 0x08,
	kNDNVMeRegINTMS = 0x0c,         // pin-based and MSI only
	kNDNVMeRegINTMC = 0x10,
	kNDNVMeRegCC = 0x14,
	kNDNVMeRegCSTS = 0x1c,
	kNDNVMeRegAQA = 0x24,
	kNDNVMeRegASQ = 0x28,           // u64
	kNDNVMeRegACQ = 0x30,           // u64
	kNDNVMeRegDoorbells = 0x1000,   // SQ y tail: 2y, CQ y head: 2y + 1, times the stride
};

// CAP fields.
#define ND_NVME_CAP_MQES(cap)   ((uint32_t)((cap) & 0xffff))            // entries - 1
#define ND_NVME_CAP_CQR(cap)    ((uint32_t)(((cap) >> 16) & 1))
#define ND_NVME_CAP_TO(cap)     ((uint32_t)(((cap) >> 24) & 0xff))      // 500 ms units
#define ND_NVME_CAP_DSTRD(cap)  ((uint32_t)(((cap) >> 32) & 0xf))       // stride 4 << DSTRD
#define ND_NVME_CAP_CSS(cap)    ((uint32_t)(((cap) >> 37) & 0xff))
#define ND_NVME_CAP_MPSMIN(cap) ((uint32_t)(((cap) >> 48) & 0xf))       // 4 KiB << MPSMIN
#define ND_NVME_CAP_MPSMAX(cap) ((uint32_t)(((cap) >> 52) & 0xf))
enum {
	kNDNVMeCSSNVM = 0x01,           // CAP.CSS bit 0: the NVM command set
};

// CC and CSTS.
enum {
	kNDNVMeCCEnable = 1u << 0,
	kNDNVMeCCCSSNVM = 0u << 4,
	kNDNVMeCCMPS4K = 0u << 7,       // memory page size 4 KiB << 0
	kNDNVMeCCAMSRoundRobin = 0u << 11,
	kNDNVMeCCShutdownNormal = 1u << 14,
	kNDNVMeCCShutdownMask = 3u << 14,
	kNDNVMeCCIOSQES = 6u << 16,     // 64-byte submission entries
	kNDNVMeCCIOCQES = 4u << 20,     // 16-byte completion entries

	kNDNVMeCSTSReady = 1u << 0,
	kNDNVMeCSTSFatal = 1u << 1,     // CFS
	kNDNVMeCSTSShutdownMask = 3u << 2,
	kNDNVMeCSTSShutdownComplete = 2u << 2,
};

// The driver's memory page: PRP entries and queues are in 4 KiB units,
// whatever the kernel's page size (16 KiB).
enum {
	kNDNVMePage = 4096,
	kNDNVMeSQESize = 64,
	kNDNVMeCQESize = 16,
};

// Submission queue entry: sixteen dwords.
struct nd_nvme_sqe {
	uint32_t cdw0;                  // opcode 7:0, fused 9:8, PSDT 15:14 (0: PRPs), CID 31:16
	uint32_t nsid;
	uint32_t cdw2, cdw3;
	uint64_t mptr;
	uint64_t prp1, prp2;
	uint32_t cdw10, cdw11, cdw12, cdw13, cdw14, cdw15;
};
ND_NVME_STATIC_ASSERT(sizeof(struct nd_nvme_sqe) == 64, "submission queue entry");

// Completion queue entry: DW0 is command specific, DW2 the SQ head and SQ
// ID, DW3 the command ID (15:0), the phase tag (16) and the status (31:17:
// SC 24:17, SCT 27:25, CRD 29:28, M 30, DNR 31).
struct nd_nvme_cqe {
	uint32_t dw0, dw1;
	uint16_t sqhd, sqid;
	uint16_t cid;
	uint16_t status;                // bit 0: phase; 15:1 the status field
};
ND_NVME_STATIC_ASSERT(sizeof(struct nd_nvme_cqe) == 16, "completion queue entry");
#define ND_NVME_STATUS_SC(s)  (((s) >> 1) & 0xff)
#define ND_NVME_STATUS_SCT(s) (((s) >> 9) & 0x7)
#define ND_NVME_STATUS_DNR(s) (((s) >> 15) & 0x1)

enum {
	// Generic command status (SCT 0).
	kNDNVMeSCSuccess = 0x00,
	kNDNVMeSCInvalidOpcode = 0x01,
	kNDNVMeSCInvalidField = 0x02,
	kNDNVMeSCDataTransferError = 0x04,
	kNDNVMeSCAbortedPowerLoss = 0x05,
	kNDNVMeSCInternalError = 0x06,
	kNDNVMeSCAbortRequested = 0x07,
	kNDNVMeSCAbortedSQDeletion = 0x08,
	kNDNVMeSCInvalidNamespace = 0x0b,
	kNDNVMeSCNamespaceWriteProtected = 0x20,
	kNDNVMeSCLBAOutOfRange = 0x80,
	kNDNVMeSCCapacityExceeded = 0x81,
	kNDNVMeSCNamespaceNotReady = 0x82,
	// Status code types.
	kNDNVMeSCTGeneric = 0,
	kNDNVMeSCTCommandSpecific = 1,
	kNDNVMeSCTMediaError = 2,
};

// Opcodes. Admin (§5): the only ones the driver sends. NVM (NVM command
// set §6): Flush, Write, Read.
enum {
	kNDNVMeAdminCreateIOSQ = 0x01,
	kNDNVMeAdminCreateIOCQ = 0x05,
	kNDNVMeAdminIdentify = 0x06,
	kNDNVMeAdminSetFeatures = 0x09,
	kNDNVMeAdminGetFeatures = 0x0a,

	kNDNVMeCmdFlush = 0x00,
	kNDNVMeCmdWrite = 0x01,
	kNDNVMeCmdRead = 0x02,
};

// Identify CNS values, feature identifiers, queue creation flags.
enum {
	kNDNVMeIdentifyNamespace = 0x00,
	kNDNVMeIdentifyController = 0x01,
	kNDNVMeIdentifyActiveNamespaces = 0x02,     // NVMe 1.1

	kNDNVMeFeatureVolatileWriteCache = 0x06,
	kNDNVMeFeatureNumberOfQueues = 0x07,

	kNDNVMeQueuePhysicallyContiguous = 1u << 0, // CDW11 PC
	kNDNVMeCQInterruptsEnabled = 1u << 1,       // CDW11 IEN

	kNDNVMeRWForceUnitAccess = 1u << 30,        // Read/Write CDW12 FUA
};

// Identify Controller data (4 KiB), the fields the driver reads.
enum {
	kNDNVMeIdCtrlVID = 0,           // u16 PCI vendor
	kNDNVMeIdCtrlSN = 4,            // 20 ASCII characters, space padded
	kNDNVMeIdCtrlMN = 24,           // 40
	kNDNVMeIdCtrlFR = 64,           // 8
	kNDNVMeIdCtrlMDTS = 77,         // u8: 2^MDTS minimum pages, 0: no limit
	kNDNVMeIdCtrlCNTLID = 78,       // u16
	kNDNVMeIdCtrlVER = 80,          // u32 (1.2 and later)
	kNDNVMeIdCtrlOACS = 256,        // u16
	kNDNVMeIdCtrlSQES = 512,        // u8: required 3:0, maximum 7:4 (log2 bytes)
	kNDNVMeIdCtrlCQES = 513,
	kNDNVMeIdCtrlNN = 516,          // u32: namespaces
	kNDNVMeIdCtrlONCS = 520,        // u16
	kNDNVMeIdCtrlVWC = 525,         // u8: bit 0, a volatile write cache
};

// Identify Namespace data (4 KiB).
enum {
	kNDNVMeIdNsNSZE = 0,            // u64: size in logical blocks
	kNDNVMeIdNsNCAP = 8,
	kNDNVMeIdNsNUSE = 16,
	kNDNVMeIdNsNSFEAT = 24,
	kNDNVMeIdNsNLBAF = 25,          // u8: formats - 1
	kNDNVMeIdNsFLBAS = 26,          // u8: format in use 3:0, extended metadata 4
	kNDNVMeIdNsNSATTR = 99,         // u8: bit 0, write protected
	kNDNVMeIdNsNGUID = 104,         // 16 bytes
	kNDNVMeIdNsEUI64 = 120,         // 8 bytes
	kNDNVMeIdNsLBAF = 128,          // u32 per format: MS 15:0, LBADS 23:16, RP 25:24
};

#endif
