/*
 * SPDX-License-Identifier: BSD-2-Clause
 * NeoDarwin-Language: portability: register, TRB and context layouts and pure helpers shared by the kernel's C++ xHCI driver and its host tests; Embedded Swift in kexts is not proven yet (P0-10).
 *
 * The eXtensible Host Controller Interface as NeoDarwin's console keyboard
 * driver uses it (xHCI 1.2, Intel document 625472; docs/kernel/usb-console.md):
 * capability, operational, runtime and doorbell registers, the extended
 * capabilities it reads (USB Legacy Support, Supported Protocol), TRBs,
 * slot and endpoint contexts, and the few calculations the driver makes
 * (endpoint intervals, route strings, device context indices). Written
 * from the specification.
 */
#ifndef ND_XHCI_H
#define ND_XHCI_H

#include <stdbool.h>
#include <stdint.h>

/* Capability registers (§5.3), from the controller's base. */
#define ND_XHCI_CAPLENGTH       0x00    /* u8; HCIVERSION is the u16 at 0x02 */
#define ND_XHCI_HCSPARAMS1      0x04
#define ND_XHCI_HCSPARAMS2      0x08
#define ND_XHCI_HCCPARAMS1      0x10
#define ND_XHCI_DBOFF           0x14
#define ND_XHCI_RTSOFF          0x18

#define ND_XHCI_HCS1_MAX_SLOTS(v)       ((v) & 0xff)
#define ND_XHCI_HCS1_MAX_INTRS(v)       (((v) >> 8) & 0x7ff)
#define ND_XHCI_HCS1_MAX_PORTS(v)       (((v) >> 24) & 0xff)
#define ND_XHCI_HCS2_ERST_MAX(v)        (((v) >> 4) & 0xf)
#define ND_XHCI_HCS2_SCRATCHPADS(v)     ((((v) >> 21) & 0x1f) << 5 | (((v) >> 27) & 0x1f))
#define ND_XHCI_HCC1_AC64               (1u << 0)
#define ND_XHCI_HCC1_CSZ                (1u << 2)
#define ND_XHCI_HCC1_PPC                (1u << 3)
#define ND_XHCI_HCC1_XECP(v)            (((v) >> 16) & 0xffff)  /* in dwords from the base */

/* Operational registers (§5.4), from base + CAPLENGTH. */
#define ND_XHCI_USBCMD          0x00
#define ND_XHCI_USBSTS          0x04
#define ND_XHCI_PAGESIZE        0x08
#define ND_XHCI_DNCTRL          0x14
#define ND_XHCI_CRCR            0x18    /* u64 */
#define ND_XHCI_DCBAAP          0x30    /* u64 */
#define ND_XHCI_CONFIG          0x38
#define ND_XHCI_PORTSC(port)    (0x400 + 0x10 * ((port) - 1))   /* ports from 1 */

#define ND_XHCI_CMD_RS          (1u << 0)
#define ND_XHCI_CMD_HCRST       (1u << 1)
#define ND_XHCI_CMD_INTE        (1u << 2)
#define ND_XHCI_CMD_HSEE        (1u << 3)

#define ND_XHCI_STS_HCH         (1u << 0)
#define ND_XHCI_STS_HSE         (1u << 2)
#define ND_XHCI_STS_EINT        (1u << 3)
#define ND_XHCI_STS_PCD         (1u << 4)
#define ND_XHCI_STS_CNR         (1u << 11)
#define ND_XHCI_STS_HCE         (1u << 12)

#define ND_XHCI_CRCR_RCS        (1u << 0)

/* PORTSC (§5.4.8). PED and the change bits are RW1C: a write that means to
 * leave them must write 0 there (nd_xhci_portsc_neutral). */
#define ND_XHCI_PORTSC_CCS      (1u << 0)
#define ND_XHCI_PORTSC_PED      (1u << 1)
#define ND_XHCI_PORTSC_OCA      (1u << 3)
#define ND_XHCI_PORTSC_PR       (1u << 4)
#define ND_XHCI_PORTSC_PLS(v)   (((v) >> 5) & 0xf)
#define ND_XHCI_PORTSC_PP       (1u << 9)
#define ND_XHCI_PORTSC_SPEED(v) (((v) >> 10) & 0xf)
#define ND_XHCI_PORTSC_PIC_MASK (3u << 14)
#define ND_XHCI_PORTSC_LWS      (1u << 16)
#define ND_XHCI_PORTSC_CSC      (1u << 17)
#define ND_XHCI_PORTSC_PEC      (1u << 18)
#define ND_XHCI_PORTSC_WRC      (1u << 19)
#define ND_XHCI_PORTSC_OCC      (1u << 20)
#define ND_XHCI_PORTSC_PRC      (1u << 21)
#define ND_XHCI_PORTSC_PLC      (1u << 22)
#define ND_XHCI_PORTSC_CEC      (1u << 23)
#define ND_XHCI_PORTSC_WCE      (1u << 25)
#define ND_XHCI_PORTSC_WDE      (1u << 26)
#define ND_XHCI_PORTSC_WOE      (1u << 27)
#define ND_XHCI_PORTSC_WPR      (1u << 31)
#define ND_XHCI_PORTSC_CHANGES  (ND_XHCI_PORTSC_CSC | ND_XHCI_PORTSC_PEC | ND_XHCI_PORTSC_WRC | ND_XHCI_PORTSC_OCC | \
	                         ND_XHCI_PORTSC_PRC | ND_XHCI_PORTSC_PLC | ND_XHCI_PORTSC_CEC)

/* What a PORTSC write keeps: the read-write bits, never PED or a change. */
static inline uint32_t
nd_xhci_portsc_neutral(uint32_t portsc)
{
	return portsc & (ND_XHCI_PORTSC_PP | ND_XHCI_PORTSC_PIC_MASK | ND_XHCI_PORTSC_WCE | ND_XHCI_PORTSC_WDE |
	       ND_XHCI_PORTSC_WOE);
}

/* Runtime registers (§5.5), from base + RTSOFF: interrupter n's set. */
#define ND_XHCI_IR(n)           (0x20 + 0x20 * (n))
#define ND_XHCI_IR_IMAN         0x00
#define ND_XHCI_IR_IMOD         0x04
#define ND_XHCI_IR_ERSTSZ       0x08
#define ND_XHCI_IR_ERSTBA       0x10    /* u64 */
#define ND_XHCI_IR_ERDP         0x18    /* u64 */
#define ND_XHCI_IMAN_IP         (1u << 0)
#define ND_XHCI_IMAN_IE         (1u << 1)
#define ND_XHCI_ERDP_EHB        (1u << 3)

/* Extended capabilities (§7). */
#define ND_XHCI_XCAP_ID(v)      ((v) & 0xff)
#define ND_XHCI_XCAP_NEXT(v)    (((v) >> 8) & 0xff)     /* in dwords */
#define ND_XHCI_XCAP_LEGACY     1
#define ND_XHCI_XCAP_PROTOCOL   2
/* USB Legacy Support (§7.1): the firmware's and the OS's ownership
 * semaphores, and the SMI enables in USBLEGCTLSTS (the next dword). */
#define ND_XHCI_LEGSUP_BIOS_OWNED       (1u << 16)
#define ND_XHCI_LEGSUP_OS_OWNED         (1u << 24)
#define ND_XHCI_LEGCTL_SMI_ENABLES      ((1u << 0) | (1u << 4) | (1u << 13) | (1u << 14) | (1u << 15))
#define ND_XHCI_LEGCTL_SMI_EVENTS       ((1u << 29) | (1u << 30) | (1u << 31))

/* The USBLEGCTLSTS value that disables every SMI and acknowledges the
 * pending ownership, PCI command and BAR events; reserved bits kept. */
static inline uint32_t
nd_xhci_legctl_quiet(uint32_t legctl)
{
	return (legctl & ~ND_XHCI_LEGCTL_SMI_ENABLES) | ND_XHCI_LEGCTL_SMI_EVENTS;
}

/* Supported Protocol (§7.2): dword 0 has the major revision in bits
 * 31:24, dword 2 the first port and the port count. */
#define ND_XHCI_PROTO_MAJOR(v)  (((v) >> 24) & 0xff)
#define ND_XHCI_PROTO_MINOR(v)  (((v) >> 16) & 0xff)
#define ND_XHCI_PROTO_PORT(v)   ((v) & 0xff)
#define ND_XHCI_PROTO_COUNT(v)  (((v) >> 8) & 0xff)

/* Port speed IDs (§7.2.2.1.1, the defaults when a protocol lists no PSI). */
enum {
	ND_XHCI_SPEED_FULL = 1,
	ND_XHCI_SPEED_LOW = 2,
	ND_XHCI_SPEED_HIGH = 3,
	ND_XHCI_SPEED_SUPER = 4,
	ND_XHCI_SPEED_SUPER_PLUS = 5,
};

static inline const char *
nd_xhci_speed_name(unsigned speed)
{
	switch (speed) {
	case ND_XHCI_SPEED_FULL: return "full-speed";
	case ND_XHCI_SPEED_LOW: return "low-speed";
	case ND_XHCI_SPEED_HIGH: return "high-speed";
	case ND_XHCI_SPEED_SUPER: return "SuperSpeed";
	case ND_XHCI_SPEED_SUPER_PLUS: return "SuperSpeedPlus";
	default: return "unknown-speed";
	}
}

/* The control endpoint's packet size before the device descriptor says
 * (USB 2.0 §5.5.3, USB 3.2 §9.6.1): 8 for low and full speed (a full-speed
 * device's may be larger; the first 8 bytes of the descriptor tell). */
static inline uint16_t
nd_xhci_default_mps0(unsigned speed)
{
	switch (speed) {
	case ND_XHCI_SPEED_HIGH: return 64;
	case ND_XHCI_SPEED_SUPER:
	case ND_XHCI_SPEED_SUPER_PLUS: return 512;
	default: return 8;
	}
}

/* bMaxPacketSize0 as a byte count: an exponent on SuperSpeed. */
static inline uint16_t
nd_xhci_mps0_from_descriptor(unsigned speed, uint8_t bMaxPacketSize0)
{
	if (speed >= ND_XHCI_SPEED_SUPER) {
		return bMaxPacketSize0 >= 5 && bMaxPacketSize0 <= 9 ? (uint16_t)(1u << bMaxPacketSize0) : 512;
	}
	return bMaxPacketSize0 == 8 || bMaxPacketSize0 == 16 || bMaxPacketSize0 == 32 || bMaxPacketSize0 == 64 ?
	       bMaxPacketSize0 : nd_xhci_default_mps0(speed);
}

/* An interrupt endpoint's Interval field (§6.2.3.6): the period is
 * 2^Interval × 125 µs. Low and full speed give bInterval in 1 ms frames
 * (1–255), rounded down to a power of two; high speed and SuperSpeed give
 * 2^(bInterval−1) microframes (bInterval 1–16). */
static inline uint8_t
nd_xhci_interrupt_interval(unsigned speed, uint8_t bInterval)
{
	if (speed == ND_XHCI_SPEED_LOW || speed == ND_XHCI_SPEED_FULL) {
		unsigned frames = bInterval == 0 ? 1 : bInterval;
		unsigned log2 = 0;
		while ((frames >> (log2 + 1)) != 0) {
			log2++;
		}
		unsigned v = log2 + 3;
		return (uint8_t)(v < 3 ? 3 : (v > 10 ? 10 : v));
	}
	unsigned v = bInterval == 0 ? 0 : (unsigned)bInterval - 1;
	return (uint8_t)(v > 15 ? 15 : v);
}

/* The interval in microseconds, for the log. */
static inline uint32_t
nd_xhci_interval_us(uint8_t interval)
{
	return 125u << interval;
}

/* A device's route string (§8.9): one 4-bit port number per hub tier below
 * the root port, the first tier in the low nibble. `depth` is the number
 * of hubs between the root port and the device's hub's own port, i.e. the
 * hub's depth (0 for a hub on a root port). Ports above 15 are 15. */
static inline uint32_t
nd_xhci_route(uint32_t hubRoute, unsigned hubDepth, unsigned port)
{
	if (hubDepth >= 5) {
		return hubRoute;
	}
	return hubRoute | ((uint32_t)(port > 15 ? 15 : port) << (4 * hubDepth));
}

/* The Device Context Index of an endpoint address (§4.5.1): EP0 is 1,
 * OUT n is 2n, IN n is 2n + 1. */
static inline unsigned
nd_xhci_dci(uint8_t endpointAddress)
{
	unsigned n = endpointAddress & 0xf;
	if (n == 0) {
		return 1;
	}
	return 2 * n + ((endpointAddress & 0x80) ? 1 : 0);
}

/* TRBs (§6.4): 16 bytes. */
struct nd_xhci_trb {
	uint64_t param;
	uint32_t status;
	uint32_t control;
};

#define ND_XHCI_TRB_CYCLE       (1u << 0)
#define ND_XHCI_TRB_TC          (1u << 1)       /* Link: toggle cycle */
#define ND_XHCI_TRB_ENT         (1u << 1)
#define ND_XHCI_TRB_ISP         (1u << 2)
#define ND_XHCI_TRB_CH          (1u << 4)
#define ND_XHCI_TRB_IOC         (1u << 5)
#define ND_XHCI_TRB_IDT         (1u << 6)
#define ND_XHCI_TRB_BSR         (1u << 9)       /* Address Device: block SET_ADDRESS */
#define ND_XHCI_TRB_DIR_IN      (1u << 16)      /* Data and Status stages */
#define ND_XHCI_TRB_TYPE(t)     ((uint32_t)(t) << 10)
#define ND_XHCI_TRB_GET_TYPE(c) (((c) >> 10) & 0x3f)
#define ND_XHCI_TRB_SLOT(s)     ((uint32_t)(s) << 24)
#define ND_XHCI_TRB_GET_SLOT(c) (((c) >> 24) & 0xff)
#define ND_XHCI_TRB_EP(e)       ((uint32_t)(e) << 16)
#define ND_XHCI_TRB_GET_EP(c)   (((c) >> 16) & 0x1f)
#define ND_XHCI_TRB_TRT(t)      ((uint32_t)(t) << 16)   /* Setup: 0 none, 2 OUT, 3 IN */
#define ND_XHCI_TRB_CODE(s)     (((s) >> 24) & 0xff)
#define ND_XHCI_TRB_RESIDUE(s)  ((s) & 0xffffff)

enum {
	ND_XHCI_TRB_NORMAL = 1,
	ND_XHCI_TRB_SETUP = 2,
	ND_XHCI_TRB_DATA = 3,
	ND_XHCI_TRB_STATUS = 4,
	ND_XHCI_TRB_LINK = 6,
	ND_XHCI_TRB_NOOP = 8,
	ND_XHCI_TRB_ENABLE_SLOT = 9,
	ND_XHCI_TRB_DISABLE_SLOT = 10,
	ND_XHCI_TRB_ADDRESS_DEVICE = 11,
	ND_XHCI_TRB_CONFIGURE_ENDPOINT = 12,
	ND_XHCI_TRB_EVALUATE_CONTEXT = 13,
	ND_XHCI_TRB_RESET_ENDPOINT = 14,
	ND_XHCI_TRB_STOP_ENDPOINT = 15,
	ND_XHCI_TRB_SET_TR_DEQUEUE = 16,
	ND_XHCI_TRB_NOOP_COMMAND = 23,
	ND_XHCI_TRB_TRANSFER_EVENT = 32,
	ND_XHCI_TRB_COMMAND_COMPLETION = 33,
	ND_XHCI_TRB_PORT_STATUS_CHANGE = 34,
	ND_XHCI_TRB_HOST_CONTROLLER = 37,
};

/* Completion codes (§6.4.5). */
enum {
	ND_XHCI_CC_SUCCESS = 1,
	ND_XHCI_CC_DATA_BUFFER = 2,
	ND_XHCI_CC_BABBLE = 3,
	ND_XHCI_CC_TRANSACTION = 4,
	ND_XHCI_CC_TRB = 5,
	ND_XHCI_CC_STALL = 6,
	ND_XHCI_CC_RESOURCE = 7,
	ND_XHCI_CC_BANDWIDTH = 8,
	ND_XHCI_CC_NO_SLOTS = 9,
	ND_XHCI_CC_SLOT_NOT_ENABLED = 11,
	ND_XHCI_CC_SHORT_PACKET = 13,
	ND_XHCI_CC_PARAMETER = 17,
	ND_XHCI_CC_CONTEXT_STATE = 19,
	ND_XHCI_CC_EVENT_RING_FULL = 21,
	ND_XHCI_CC_STOPPED = 26,
	ND_XHCI_CC_STOPPED_LENGTH_INVALID = 27,
	ND_XHCI_CC_STOPPED_SHORT_PACKET = 28,
};

/* Contexts (§6.2): 32 bytes each, or 64 with HCCPARAMS1.CSZ (the second
 * half reserved). The driver reads and writes them as dwords at an offset. */
#define ND_XHCI_SLOT_ROUTE(r)           ((uint32_t)(r) & 0xfffff)
#define ND_XHCI_SLOT_SPEED(s)           ((uint32_t)(s) << 20)
#define ND_XHCI_SLOT_MTT                (1u << 25)
#define ND_XHCI_SLOT_HUB                (1u << 26)
#define ND_XHCI_SLOT_ENTRIES(n)         ((uint32_t)(n) << 27)
#define ND_XHCI_SLOT_ROOT_PORT(p)       ((uint32_t)(p) << 16)   /* dword 1 */
#define ND_XHCI_SLOT_NUM_PORTS(n)       ((uint32_t)(n) << 24)   /* dword 1 */
#define ND_XHCI_SLOT_TT_SLOT(s)         ((uint32_t)(s))         /* dword 2 */
#define ND_XHCI_SLOT_TT_PORT(p)         ((uint32_t)(p) << 8)    /* dword 2 */
#define ND_XHCI_SLOT_TTT(t)             ((uint32_t)(t) << 16)   /* dword 2 */
#define ND_XHCI_SLOT_STATE(dw3)         (((dw3) >> 27) & 0x1f)
#define ND_XHCI_SLOT_ADDRESS(dw3)       ((dw3) & 0xff)

#define ND_XHCI_EP_INTERVAL(i)          ((uint32_t)(i) << 16)   /* dword 0 */
#define ND_XHCI_EP_ESIT_HI(p)           ((uint32_t)(((p) >> 16) & 0xff) << 24)
#define ND_XHCI_EP_CERR(c)              ((uint32_t)(c) << 1)    /* dword 1 */
#define ND_XHCI_EP_TYPE(t)              ((uint32_t)(t) << 3)
#define ND_XHCI_EP_MAX_BURST(b)         ((uint32_t)(b) << 8)
#define ND_XHCI_EP_MPS(m)               ((uint32_t)(m) << 16)
#define ND_XHCI_EP_DCS                  1ull                    /* dwords 2-3: TR dequeue pointer */
#define ND_XHCI_EP_AVG_TRB(l)           ((uint32_t)(l))         /* dword 4 */
#define ND_XHCI_EP_ESIT_LO(p)           ((uint32_t)((p) & 0xffff) << 16)
#define ND_XHCI_EP_STATE(dw0)           ((dw0) & 7)

enum {
	ND_XHCI_EP_CONTROL = 4,
	ND_XHCI_EP_INTERRUPT_IN = 7,
};

/* The Slot and EP0 contexts' dword offsets, in dwords; the input context
 * starts with the Input Control Context (drop flags, add flags). */
#define ND_XHCI_ICC_DROP        0
#define ND_XHCI_ICC_ADD         1

#endif /* ND_XHCI_H */
