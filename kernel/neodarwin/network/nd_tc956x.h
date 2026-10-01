/*
 * SPDX-License-Identifier: BSD-2-Clause
 * NeoDarwin-Language: expressibility: a FreeBSD-derived driver core, included by an IOKit C++ class and compiled on the host for its simulation test.
 *
 * Copyright (c) 2026 James Kane
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

/*
 * Toshiba TC956x PCIe Ethernet (Synopsys XGMAC 3.01), the hardware core of
 * NeoDarwinTC956x (docs/kernel/network.md, "Checkpoint 3").
 *
 * Ported from FreeBSD's tcx driver, sys/dev/tcx/if_tcx.c and if_tcxreg.h
 * (freebsd-src branch radxa-dragon-q8b at 4c5da483b9, written for and run on
 * the Radxa Dragon Q8B; PROVENANCE.md). The register map, the bring-up
 * order, the SerDes/PCS/PHY sequences, the link state machine, the
 * descriptor formats and their ring handling, interrupt moderation,
 * checksum offload and the multicast hash are FreeBSD's; iflib, newbus,
 * ifmedia and sysctl plumbing is replaced by the functions' arguments and
 * results, which NeoDarwinTC956x.cpp turns into IONetworkingFamily calls.
 *
 * The TC956x is a PCIe switch with an internal endpoint whose two PCI
 * functions each hold an XGMAC, an XPCS, a SerDes (PMA) and an MSI
 * generator. Function 0 also owns the chip-wide clocks and resets and the
 * table (TAMAP) that translates the MACs' 64GB DMA window onto PCIe. Each
 * MAC reaches its PHY (a QCA8081) over one SerDes lane, which the PHY
 * switches between 2500BASE-X at 2.5G and SGMII with in-band
 * autonegotiation at lower speeds; on each change the MAC's speed selector
 * is retuned, the PMA restarted and the XPCS reconfigured.
 *
 * A block whose clock is off or whose reset is asserted may not answer a
 * read, and on Qualcomm PCIe hosts a failed read is an SError, so blocks
 * are only touched once the clock and reset registers say they run: the
 * functions below keep FreeBSD's order exactly, and the host test checks it.
 *
 * The includer defines, before including this file:
 *   ND_TC956X_READ(sc, off)            32-bit read of BAR4 (the SFR space)
 *   ND_TC956X_WRITE(sc, off, val)      32-bit write of BAR4
 *   ND_TC956X_BRIDGE_WRITE(sc, off, val) 32-bit write of BAR0 (TAMAP)
 *   ND_TC956X_DELAY(sc, us)            wait at least us microseconds
 *   ND_TC956X_LOG(sc, fmt, ...)        one line of the driver's log
 * Offsets are within the function's own BARs; nothing here touches any
 * other device. Descriptors are little-endian, as the host is.
 */

#ifndef ND_TC956X_H
#define ND_TC956X_H

#include <stdint.h>
#ifndef __cplusplus
#include <stdbool.h>
#endif

#if !defined(ND_TC956X_READ) || !defined(ND_TC956X_WRITE) || !defined(ND_TC956X_BRIDGE_WRITE) || \
    !defined(ND_TC956X_DELAY) || !defined(ND_TC956X_LOG)
#error "define the ND_TC956X_* accessors before including nd_tc956x.h"
#endif

#if defined(__BYTE_ORDER__) && __BYTE_ORDER__ != __ORDER_LITTLE_ENDIAN__
#error "nd_tc956x.h writes descriptors in host order, which must be little-endian"
#endif

#define TC956X_VENDOR_TOSHIBA           0x1179
#define TC956X_DEVICE_ETH               0x0220

/*
 * BAR0: the bridge configuration, with the AXI slave 0 address translation
 * table (TAMAP), four entries. BAR4: the chip's SFR space.
 */
#define TC956X_BAR0_MIN                 0x1000
#define TC956X_TAMAP_BASE               0x0800
#define TC956X_TAMAP_STRIDE             0x20
#define TC956X_TAMAP_NENTRIES           4
#define TC956X_TAMAP_SRC_LO             0x00
#define  TC956X_TAMAP_IMPL              (1u << 0)
#define  TC956X_TAMAP_SIZE_SHIFT        1       /* window is 2^(size + 1) */
#define TC956X_TAMAP_SRC_HI             0x04
#define TC956X_TAMAP_TRSL_LO            0x08
#define TC956X_TAMAP_TRSL_HI            0x0c
#define TC956X_TAMAP_TRSL_PARAM         0x10

/*
 * The XGMAC reaches host memory through TAMAP entry 0: its AXI addresses
 * from TC956X_DMA_OFFSET up are sent to PCIe address 0 up, for a window of
 * 2^(TC956X_TAMAP_SIZE + 1) = 64GB, hence 36 address bits.
 */
#define TC956X_DMA_OFFSET               0x1000000000ULL
#define TC956X_TAMAP_SIZE               35
#define TC956X_DMA_BITS                 36

/* BAR4: chip control */
#define TC956X_NCID                     0x0000
#define  TC956X_NCID_REV_MASK           0xffu
#define TC956X_NCLKCTRL(n)              ((n) == 0 ? 0x1004u : 0x100cu)
#define TC956X_NRSTCTRL(n)              ((n) == 0 ? 0x1008u : 0x1010u)
#define TC956X_NEMACCTL(mac)            (0x1070u + (uint32_t)(mac) * 4)
#define  TC956X_EMACCTL_SP_SEL_MASK     0xfu
#define  TC956X_EMACCTL_SP_2500         4
#define  TC956X_EMACCTL_SP_1000         5
#define  TC956X_EMACCTL_SP_100          6
#define  TC956X_EMACCTL_SP_10           7
#define  TC956X_EMACCTL_PHY_INF_MASK    (0x3u << 4)
#define  TC956X_EMACCTL_PHY_INF_PHYCLK  (0x1u << 4)
#define  TC956X_EMACCTL_INV_SGM_SIGDET  (1u << 6)
#define  TC956X_EMACCTL_LPIHWCLKEN      (1u << 8)
#define  TC956X_EMACCTL_INIT_DONE       (1u << 21)

/* Chip-wide clock and reset bits, in NCLKCTRL(0) and NRSTCTRL(0) */
#define TC956X_CLK_MSIGEN               (1u << 18)
#define TC956X_RST_MSIGEN               (1u << 18)

/* Per-MAC clock and reset bits, in NCLKCTRL(mac) and NRSTCTRL(mac) */
#define TC956X_CLK_MAC_TX               (1u << 7)
#define TC956X_CLK_MAC_RX               (1u << 14)
#define TC956X_CLK_MAC_RMII             (1u << 15)      /* MAC 1 only */
#define TC956X_CLK_MAC_ALL              (1u << 31)
#define TC956X_RST_MAC                  (1u << 7)
#define TC956X_RST_PMA                  (1u << 30)
#define TC956X_RST_XPCS                 (1u << 31)

/*
 * MSI generator, one per function. The DMA channel interrupts are level
 * sources; it sends one MSI and then holds off until MASK_CLR is written,
 * when it sends another if a source is still asserted.
 */
#define TC956X_MSIGEN_BASE(mac)         (0xf000u + (uint32_t)(mac) * 0x100)
#define TC956X_MSI_OUT_EN               0x00    /* bit per source */
#define TC956X_MSI_MASK_CLR             0x0c
#define  TC956X_MSI_MASK_CLR_ALL        (1u << 0)
#define TC956X_MSI_SRC_TX(c)            (3 + (c))       /* DMA channel c */
#define TC956X_MSI_SRC_RX(c)            (11 + (c))

/* The QCA8081 PHY on each port, as wired on the Radxa Dragon Q8B */
#define TC956X_PHY_ADDR                 0x1c

/* XGMAC and the blocks behind it */
#define TC956X_XGMAC_BASE(mac)          (0x40000u + (uint32_t)(mac) * 0x8000)
#define TC956X_XPCS_OFFSET              0x3a00
#define TC956X_PMA_OFFSET               0x4000
/* The highest BAR4 offset the driver touches is below this. */
#define TC956X_SFR_MIN                  0x50000

/*
 * SerDes (PMA) registers, relative to the XGMAC base plus
 * TC956X_PMA_OFFSET. PMA init powers the CML buffers and switches five
 * lanes' reference clock to the internal CLK_REF_I.
 */
#define TC956X_PMA_CML_GL_PM_CFG0       0x01b8
#define TC956X_PMA_NLANES               5
#define TC956X_PMA_HWT_REFCK_R_EN(i)    (0x1080u + (uint32_t)(i) * 0x14)
#define TC956X_PMA_HWT_REFCK_TERM_EN(i) (0x1090u + (uint32_t)(i) * 0x14)
#define TC956X_PMA_COMM_CFG_0_1(i)      (0x1888u + (uint32_t)(i) * 8)
#define  TC956X_PMA_COMM_CFG_REFCLK_I   ((0xf7u << 9) | (1u << 8) | 0x04)

/*
 * XPCS registers are reached through a 1KB window at XGMAC base plus
 * TC956X_XPCS_OFFSET: an MMD register (mmd << 16 | reg) has its bits 20:8
 * written to the viewport, and bits 7:0 select the 32-bit word. MMD 31
 * starts with the clause 22 registers, laid out as in mii.h.
 */
#define TC956X_XPCS_VIEWPORT            (0xff * 4)
#define XPCS_MMD_PCS                    3
#define XPCS_MMD_VEND2                  31
#define XPCS_PCS_CTRL2                  7       /* MMD 3 */
#define  XPCS_PCS_TYPE_SEL_MODAL        4       /* reserved: honour mode bits */
#define XPCS_PCS_STAT2                  8       /* MMD 3 */
#define  XPCS_PCS_STAT2_10GBR           (1u << 0)
#define XPCS_VR_MII_DIG_CTRL1           0x8000  /* MMD 31 */
#define  XPCS_DIG_CTRL1_2G5_EN          (1u << 2)
#define  XPCS_DIG_CTRL1_MAC_AUTO_SW     (1u << 9)
#define XPCS_VR_MII_AN_CTRL             0x8001  /* MMD 31 */
#define  XPCS_AN_CTRL_PCS_MODE_MASK     (0x3u << 1)
#define  XPCS_AN_CTRL_PCS_MODE_SGMII    (0x2u << 1)
#define  XPCS_AN_CTRL_TX_CONFIG_PHY     (1u << 3)       /* else MAC side */
#define XPCS_VR_MII_AN_INTR_STS         0x8002  /* MMD 31 */

/* XGMAC MAC registers, relative to TC956X_XGMAC_BASE() */
#define XGMAC_TX_CONFIG                 0x0000
#define  XGMAC_TX_CONFIG_TE             (1u << 0)
#define  XGMAC_TX_CONFIG_JD             (1u << 16)
#define  XGMAC_TX_CONFIG_SS_SHIFT       29
#define  XGMAC_TX_CONFIG_SS_MASK        (0x7u << 29)
#define  XGMAC_SS_2500_GMII             0x2
#define  XGMAC_SS_1000_GMII             0x3
#define  XGMAC_SS_100_MII               0x4
#define  XGMAC_SS_10_MII                0x7
#define XGMAC_RX_CONFIG                 0x0004
#define  XGMAC_RX_CONFIG_RE             (1u << 0)
#define  XGMAC_RX_CONFIG_ACS            (1u << 1)       /* strip pad/FCS */
#define  XGMAC_RX_CONFIG_CST            (1u << 2)       /* strip FCS */
#define  XGMAC_RX_CONFIG_GPSLCE         (1u << 6)
#define  XGMAC_RX_CONFIG_WD             (1u << 7)
#define  XGMAC_RX_CONFIG_IPC            (1u << 9)       /* checksum offload */
#define  XGMAC_RX_CONFIG_GPSL_SHIFT     16
#define  XGMAC_RX_CONFIG_GPSL_MAX       16368
#define XGMAC_PACKET_FILTER             0x0008
#define  XGMAC_FILTER_PR                (1u << 0)
#define  XGMAC_FILTER_HMC               (1u << 2)       /* hash multicast */
#define  XGMAC_FILTER_PM                (1u << 4)       /* all multicast */
#define  XGMAC_FILTER_HPF               (1u << 10)      /* hash or perfect */
/*
 * Multicast hash filter: 64 bins in two registers, as HW_FEATURE1 on the
 * TC956x reports. A frame's bin is the top six bits of the complement of
 * the big-endian Ethernet CRC of its destination address.
 */
#define XGMAC_HASH_TABLE(n)             (0x0010u + (uint32_t)(n) * 4)
#define XGMAC_HASH_BITS_LOG2            6
#define XGMAC_Q_TX_FLOW_CTRL(q)         (0x0070u + (uint32_t)(q) * 4)
#define  XGMAC_TX_FLOW_TFE              (1u << 1)
#define  XGMAC_TX_FLOW_PT_SHIFT         16      /* pause time, 512 bit times */
#define XGMAC_RX_FLOW_CTRL              0x0090
#define  XGMAC_RX_FLOW_RFE              (1u << 0)
#define XGMAC_RXQ_CTRL0                 0x00a0
#define  XGMAC_RXQ_EN_DCB               0x2     /* per queue, 2 bits each */
#define XGMAC_INT_EN                    0x00b4
#define XGMAC_VERSION                   0x0110
#define  XGMAC_VERSION_SNPS_MASK        0xffu
#define XGMAC_MDIO_ADDR                 0x0200
#define  XGMAC_MDIO_ADDR_PA_SHIFT       16      /* PHY (port) address */
#define  XGMAC_MDIO_ADDR_DA_SHIFT       21      /* clause 45 device */
#define  XGMAC_MDIO_ADDR_C22_REG_MASK   0x1fu
#define XGMAC_MDIO_DATA                 0x0204
#define  XGMAC_MDIO_DATA_MASK           0xffffu
#define  XGMAC_MDIO_CMD_WRITE           (1u << 16)
#define  XGMAC_MDIO_CMD_READ            (3u << 16)
#define  XGMAC_MDIO_CR_SHIFT            19      /* MDC clock divider */
#define  XGMAC_MDIO_BUSY                (1u << 22)
#define XGMAC_MDIO_C22P                 0x0220  /* bit n: port n is clause 22 */
#define XGMAC_ADDR_HIGH(n)              (0x0300u + (uint32_t)(n) * 8)
#define  XGMAC_ADDR_HIGH_AE             (1u << 31)
#define XGMAC_ADDR_LOW(n)               (0x0304u + (uint32_t)(n) * 8)

/* MAC management counters (MMC), 64-bit where noted */
#define XGMAC_MMC_BASE                  0x0800
#define XGMAC_MMC_RX_PKT_GB             (XGMAC_MMC_BASE + 0x100)        /* 64 */
#define XGMAC_MMC_RX_CRC_ERR            (XGMAC_MMC_BASE + 0x128)        /* 64 */
#define XGMAC_MMC_RX_PAUSE              (XGMAC_MMC_BASE + 0x188)        /* 64 */
#define XGMAC_MMC_RX_FIFOOVER_PKT       (XGMAC_MMC_BASE + 0x190)        /* 64 */

/* MTL, per queue */
#define XGMAC_MTL_QS(bytes)             ((((uint32_t)(bytes)) / 256 - 1) << 16)  /* TQS, RQS */
#define XGMAC_MTL_TXQ_OPMODE(q)         (0x1100u + (uint32_t)(q) * 0x80)
#define  XGMAC_MTL_TSF                  (1u << 1)
#define  XGMAC_MTL_TXQEN_ENABLED        (0x2u << 2)
#define XGMAC_MTL_TXQ_DEBUG(q)          (0x1108u + (uint32_t)(q) * 0x80)
#define  XGMAC_MTL_TXQ_NOT_EMPTY        (1u << 4)       /* TXQSTS */
#define XGMAC_MTL_TC_ETS_CONTROL(q)     (0x1110u + (uint32_t)(q) * 0x80)
#define XGMAC_MTL_RXQ_OPMODE(q)         (0x1140u + (uint32_t)(q) * 0x80)
#define  XGMAC_MTL_RSF                  (1u << 5)
#define  XGMAC_MTL_EHFC                 (1u << 7)       /* flow control */
#define XGMAC_MTL_RXQ_MISSED(q)         (0x1144u + (uint32_t)(q) * 0x80)
#define XGMAC_MTL_RXQ_DEBUG(q)          (0x1148u + (uint32_t)(q) * 0x80)
#define  XGMAC_MTL_RXQ_NOT_EMPTY        ((0x3fffu << 16) | (0x3u << 4))
/* Thresholds count down from full: FIFO size - (1KB + n * 512 bytes). */
#define XGMAC_MTL_RXQ_FLOW_CONTROL(q)   (0x1150u + (uint32_t)(q) * 0x80)
#define  XGMAC_MTL_RFA_SHIFT            1       /* send PAUSE above this */
#define  XGMAC_MTL_RFD_SHIFT            17      /* release it below this */

/* DMA */
#define XGMAC_DMA_MODE                  0x3000
#define  XGMAC_DMA_MODE_SWR             (1u << 0)
#define  XGMAC_DMA_MODE_INTM_MASK       (0x3u << 12)
#define  XGMAC_DMA_MODE_INTM_PERCH      (0x1u << 12)
#define XGMAC_DMA_SYSBUS_MODE           0x3004
#define  XGMAC_SYSBUS_WR_OSR_SHIFT      24
#define  XGMAC_SYSBUS_RD_OSR_SHIFT      16
#define  XGMAC_SYSBUS_EAME              (1u << 11)
#define  XGMAC_SYSBUS_BLEN4_128         0x7eu
#define  XGMAC_SYSBUS_UNDEF             (1u << 0)
#define XGMAC_TX_EDMA_CTRL              0x3040
#define XGMAC_RX_EDMA_CTRL              0x3044
#define  XGMAC_EDMA_PS_MAX              0x3fffffffu
#define XGMAC_DMA_CH_CONTROL(c)         (0x3100u + (uint32_t)(c) * 0x80)
#define  XGMAC_DMA_CH_PBLX8             (1u << 16)
#define XGMAC_DMA_CH_TX_CONTROL(c)      (0x3104u + (uint32_t)(c) * 0x80)
#define  XGMAC_DMA_CH_TXST              (1u << 0)
#define  XGMAC_DMA_CH_TSE               (1u << 12)      /* allow TSO */
#define XGMAC_DMA_CH_RX_CONTROL(c)      (0x3108u + (uint32_t)(c) * 0x80)
#define  XGMAC_DMA_CH_RXST              (1u << 0)
#define  XGMAC_DMA_CH_RBSZ_SHIFT        1
#define  XGMAC_DMA_CH_RBSZ_MASK         (0x3fffu << 1)
#define  XGMAC_DMA_CH_PBL_SHIFT         16      /* TxPBL and RxPBL */
#define XGMAC_DMA_CH_TXDESC_HADDR(c)    (0x3110u + (uint32_t)(c) * 0x80)
#define XGMAC_DMA_CH_TXDESC_LADDR(c)    (0x3114u + (uint32_t)(c) * 0x80)
#define XGMAC_DMA_CH_RXDESC_HADDR(c)    (0x3118u + (uint32_t)(c) * 0x80)
#define XGMAC_DMA_CH_RXDESC_LADDR(c)    (0x311cu + (uint32_t)(c) * 0x80)
#define XGMAC_DMA_CH_TXDESC_TAIL(c)     (0x3124u + (uint32_t)(c) * 0x80)
#define XGMAC_DMA_CH_RXDESC_TAIL(c)     (0x312cu + (uint32_t)(c) * 0x80)
#define XGMAC_DMA_CH_TXDESC_RING_LEN(c) (0x3130u + (uint32_t)(c) * 0x80)
#define XGMAC_DMA_CH_RXDESC_RING_LEN(c) (0x3134u + (uint32_t)(c) * 0x80)
#define  XGMAC_DMA_CH_OWRQ_SHIFT        24      /* XGMAC 3.01a erratum */
#define XGMAC_DMA_CH_INT_EN(c)          (0x3138u + (uint32_t)(c) * 0x80)
/*
 * RX interrupt watchdog: after a frame whose descriptor did not ask for an
 * interrupt, RI is raised RWT * 256 DMA clock cycles later.
 */
#define XGMAC_DMA_CH_RX_WATCHDOG(c)     (0x313cu + (uint32_t)(c) * 0x80)
#define  XGMAC_DMA_CH_RWT_MASK          0xffu
#define XGMAC_DMA_CH_STATUS(c)          (0x3160u + (uint32_t)(c) * 0x80)
#define  XGMAC_DMA_CH_TI                (1u << 0)
#define  XGMAC_DMA_CH_TPS               (1u << 1)       /* TX stopped */
#define  XGMAC_DMA_CH_RI                (1u << 6)
#define  XGMAC_DMA_CH_RBU               (1u << 7)
#define  XGMAC_DMA_CH_FBE               (1u << 12)
#define  XGMAC_DMA_CH_AIS               (1u << 14)
#define  XGMAC_DMA_CH_NIS               (1u << 15)

/*
 * DMA descriptors, 16 bytes. The same layout is used for TX and RX; the
 * meaning of the fields depends on direction and on who owns it.
 */
struct nd_tc956x_desc {
	uint32_t des0;          /* buffer address, low */
	uint32_t des1;          /* buffer address, high */
	uint32_t des2;
	uint32_t des3;
};

#define TDES2_B1L_MASK                  0x3fffu
#define TDES2_IOC                       (1u << 31)
#define TDES3_FL_MASK                   0x7fffu
#define TDES3_CIC_IP                    (1u << 16)      /* IPv4 header */
#define TDES3_CIC_FULL                  (3u << 16)      /* and TCP/UDP */
#define TDES3_CTXT                      (1u << 30)      /* context descriptor */
#define TDES3_LD                        (1u << 28)
#define TDES3_FD                        (1u << 29)
#define TDES3_OWN                       (1u << 31)

#define RDES3_PL_MASK                   0x3fffu
#define RDES3_ES                        (1u << 15)
#define RDES3_L34T_SHIFT                20              /* packet type */
#define RDES3_L34T_MASK                 (0xfu << 20)
#define  RDES3_L34T_IP4TCP              0x1
#define  RDES3_L34T_IP4UDP              0x2
#define  RDES3_L34T_IP6TCP              0x9
#define  RDES3_L34T_IP6UDP              0xa
#define RDES3_LD                        (1u << 28)
#define RDES3_IOC                       (1u << 30)      /* read format */
#define RDES3_OWN                       (1u << 31)

/* The MII registers the driver uses (FreeBSD's dev/mii/mii.h). */
#define ND_MII_BMCR                     0x00
#define  ND_BMCR_RESET                  0x8000
#define  ND_BMCR_S100                   0x2000
#define  ND_BMCR_AUTOEN                 0x1000
#define  ND_BMCR_STARTNEG               0x0200
#define  ND_BMCR_S1000                  0x0040
#define ND_MII_BMSR                     0x01
#define ND_MII_PHYIDR1                  0x02
#define ND_MII_PHYIDR2                  0x03
#define ND_MII_ANAR                     0x04
#define  ND_ANAR_10                     0x0020
#define  ND_ANAR_10_FD                  0x0040
#define  ND_ANAR_TX                     0x0080
#define  ND_ANAR_TX_FD                  0x0100
#define  ND_ANAR_PAUSE_SYM              (1u << 10)
#define  ND_ANAR_PAUSE_ASYM             (2u << 10)
#define ND_MII_ANLPAR                   0x05
#define  ND_ANLPAR_PAUSE_SYM            (1u << 10)
#define  ND_ANLPAR_PAUSE_ASYM           (2u << 10)
#define ND_MII_100T2CR                  0x09
#define  ND_GTCR_ADV_1000TFDX           0x0200
#define  ND_GTCR_ADV_1000THDX           0x0100
#define ND_MII_MMDACR                   0x0d
#define  ND_MMDACR_FN_DATANPI           (1u << 14)
#define ND_MII_MMDAADR                  0x0e

/* The Qualcomm QCA8081 PHY */
#define QCA8081_ID                      0x004dd101u     /* PHYIDR1 << 16 | PHYIDR2 */
#define QCA808X_PHY_SPEC_STATUS         0x11
#define  QCA808X_SS_LINK                (1u << 10)
#define  QCA808X_SS_DUPLEX              (1u << 13)
#define  QCA808X_SS_SPEED_SHIFT         7
#define  QCA808X_SS_SPEED_MASK          (0x7u << 7)
#define  QCA808X_SS_SPEED_10            0
#define  QCA808X_SS_SPEED_100           1
#define  QCA808X_SS_SPEED_1000          2
#define  QCA808X_SS_SPEED_2500          4
#define ND_MMD_AN                       7       /* through the MII_MMDACR window */
#define ND_MMD_AN_10GBT_CTRL            0x0020
#define  ND_MMD_AN_10GBT_ADV2_5G        0x0080

/*
 * The QCA8081's SerDes answers at the next MDIO address. Its FIFO toward
 * the MAC must be taken out of reset on each link up and put back on link
 * down; while it is held no frames pass, although the SerDes link and SGMII
 * autonegotiation work. Firmware only releases it on ports that had a link
 * when it ran.
 */
#define QCA8081_SERDES_ADDR             (TC956X_PHY_ADDR + 1)
#define QCA8081_SERDES_FIFO_CTRL        0x9072          /* MMD 1 */
#define  QCA8081_SERDES_FIFO_RSTN       (1u << 11)

/* FreeBSD's tunables and limits */
#define ND_TC956X_TX_FIFO_BYTES         16384
#define ND_TC956X_RX_FIFO_BYTES         32768
#define ND_TC956X_FC_HEADROOM_MIN       8192
#define ND_TC956X_FC_HYSTERESIS         4096
#define ND_TC956X_PAUSE_TIME            0xffffu
#define ND_TC956X_RX_RIWT_DEFAULT       64
#define ND_TC956X_RX_COAL_FRAMES_DEFAULT 0
#define ND_TC956X_TX_COAL_FRAMES_DEFAULT 128
#define ND_TC956X_PBL                   32
#define ND_TC956X_MDIO_CR               0       /* 125MHz CSR clock / 62: 2MHz */
#define ND_TC956X_MDIO_TIMEOUT          10000   /* microseconds */
#define ND_TC956X_SWR_TIMEOUT           100000
#define ND_TC956X_STOP_TIMEOUT          100000
#define ND_TC956X_PMA_TIMEOUT           1000000
#define ND_TC956X_XPCS_RESET_TIMEOUT    600000

/* ETHER_MAX_LEN + ETHER_VLAN_ENCAP_LEN */
#define ND_TC956X_MAX_FRAME_DEFAULT     1522

/*
 * The link speeds, with how the PHY reports each, how it is advertised,
 * and how the SerDes and the MAC are set for it.
 */
struct nd_tc956x_speed {
	uint32_t mbps;
	uint32_t qca_ss;        /* QCA808X_SS_SPEED_* */
	uint32_t sp_sel;        /* TC956X_EMACCTL_SP_* */
	uint32_t xgmac_ss;      /* XGMAC_SS_* */
	uint16_t anar;          /* advertisement, full duplex */
	uint16_t gtcr;
	uint16_t adv2_5g;
};

static const struct nd_tc956x_speed nd_tc956x_speeds[] = {
	{ 2500, QCA808X_SS_SPEED_2500, TC956X_EMACCTL_SP_2500, XGMAC_SS_2500_GMII, 0, 0, ND_MMD_AN_10GBT_ADV2_5G },
	{ 1000, QCA808X_SS_SPEED_1000, TC956X_EMACCTL_SP_1000, XGMAC_SS_1000_GMII, 0, ND_GTCR_ADV_1000TFDX, 0 },
	{ 100, QCA808X_SS_SPEED_100, TC956X_EMACCTL_SP_100, XGMAC_SS_100_MII, ND_ANAR_TX_FD, 0, 0 },
	{ 10, QCA808X_SS_SPEED_10, TC956X_EMACCTL_SP_10, XGMAC_SS_10_MII, ND_ANAR_10_FD, 0, 0 },
};
#define ND_TC956X_NSPEEDS (sizeof(nd_tc956x_speeds) / sizeof(nd_tc956x_speeds[0]))

/* The link as the PHY reports it, with the PAUSE use resolved. */
struct nd_tc956x_link {
	bool up;
	bool fdx;
	bool txpause;           /* we may send PAUSE */
	bool rxpause;           /* we obey PAUSE */
	uint32_t speed;         /* Mb/s, 0 if unknown */
};

/* What a link check changed, for the includer to report. */
enum nd_tc956x_link_event {
	ND_TC956X_LINK_SAME = 0,
	ND_TC956X_LINK_WENT_DOWN,
	ND_TC956X_LINK_WENT_UP,         /* or changed speed, duplex or PAUSE */
};

/* One function's state. The includer fills the first block. */
struct nd_tc956x {
	void *bar4;                     /* the includer's: BAR4 */
	void *bar0;                     /* the includer's: BAR0 */
	void *owner;                    /* the includer's: for its log and delays */
	int mac;                        /* the PCI function, 0 or 1 */

	struct nd_tc956x_link link;     /* as last applied */
	bool link_known;
	uint32_t serdes_speed;          /* what the SerDes is set to; 0 unknown */

	/* Interrupt moderation (FreeBSD's dev.tcx.N sysctls). */
	uint32_t rx_riwt;               /* RX watchdog, 256 cycles; 0 off */
	uint32_t rx_coal_frames;        /* IOC every n refills; 0 never */
	uint32_t rx_ioc_count;
	uint32_t tx_coal_frames;        /* descriptors between IOCs */
	uint32_t tx_since_ioc;

	/* Set by nd_tc956x_init. */
	bool rx_csum;                   /* RX checksum offload on */
	uint32_t rx_bufsz;
	uint32_t max_frame;

	/* Rings: the includer allocates, the core fills. Sizes are powers of 2. */
	volatile struct nd_tc956x_desc *txd, *rxd;
	uint64_t tx_pa, rx_pa;          /* host physical addresses */
	uint32_t ntxd, nrxd;
	uint32_t tx_cidx, tx_pidx;      /* oldest not reclaimed, next to fill */
	uint32_t rx_cidx, rx_pidx;      /* next to receive, next to refill */

	/* Counters */
	uint64_t stat_intr, stat_intr_rx, stat_intr_tx, stat_rbu, stat_fbe;
	uint64_t stat_rx_err;
	uint32_t rx_err_des3;           /* status of the last frame with ES */
};

/* Where the MAC sees host address pa, through the translation window. */
static inline uint64_t
nd_tc956x_dma_addr(uint64_t pa)
{
	return pa + TC956X_DMA_OFFSET;
}

#define ND_TC956X_MAC_OFF(sc, reg)      (TC956X_XGMAC_BASE((sc)->mac) + (uint32_t)(reg))
#define ND_TC956X_MAC_READ(sc, reg)     ND_TC956X_READ((sc), ND_TC956X_MAC_OFF((sc), (reg)))
#define ND_TC956X_MAC_WRITE(sc, reg, v) ND_TC956X_WRITE((sc), ND_TC956X_MAC_OFF((sc), (reg)), (v))
#define ND_TC956X_MSI_WRITE(sc, reg, v) ND_TC956X_WRITE((sc), TC956X_MSIGEN_BASE((sc)->mac) + (uint32_t)(reg), (v))

/* Defaults for a function about to attach. */
static inline void
nd_tc956x_setup(struct nd_tc956x *sc, int mac)
{
	sc->mac = mac;
	sc->rx_riwt = ND_TC956X_RX_RIWT_DEFAULT;
	sc->rx_coal_frames = ND_TC956X_RX_COAL_FRAMES_DEFAULT;
	sc->tx_coal_frames = ND_TC956X_TX_COAL_FRAMES_DEFAULT;
	sc->max_frame = ND_TC956X_MAX_FRAME_DEFAULT;
}

/*
 * Chip level
 */

/* Wait for (SFR[off] & mask) == val. Returns false on a timeout. */
static inline bool
nd_tc956x_wait(struct nd_tc956x *sc, uint32_t off, uint32_t mask, uint32_t val, uint32_t step, uint32_t timeout)
{
	for (uint32_t i = 0; i < timeout; i += step) {
		if ((ND_TC956X_READ(sc, off) & mask) == val) {
			return true;
		}
		ND_TC956X_DELAY(sc, step);
	}
	return false;
}

static inline void
nd_tc956x_sfr_update(struct nd_tc956x *sc, uint32_t reg, uint32_t clr, uint32_t set)
{
	ND_TC956X_WRITE(sc, reg, (ND_TC956X_READ(sc, reg) & ~clr) | set);
}

static inline bool
nd_tc956x_msigen_running(struct nd_tc956x *sc)
{
	return (ND_TC956X_READ(sc, TC956X_NCLKCTRL(0)) & TC956X_CLK_MSIGEN) != 0 &&
	       (ND_TC956X_READ(sc, TC956X_NRSTCTRL(0)) & TC956X_RST_MSIGEN) == 0;
}

static inline bool
nd_tc956x_mac_running(struct nd_tc956x *sc)
{
	return (ND_TC956X_READ(sc, TC956X_NCLKCTRL(sc->mac)) & TC956X_CLK_MAC_ALL) != 0 &&
	       (ND_TC956X_READ(sc, TC956X_NRSTCTRL(sc->mac)) & TC956X_RST_MAC) == 0;
}

/*
 * Chip-wide setup, done by function 0. Firmware has been seen to leave the
 * translation table programmed already, but it is cheap to redo and must
 * be right before the MACs can DMA. Only entry 0 is used.
 */
static inline void
nd_tc956x_chip_init(struct nd_tc956x *sc)
{
	for (uint32_t i = 0; i < TC956X_TAMAP_NENTRIES; i++) {
		uint32_t e = TC956X_TAMAP_BASE + i * TC956X_TAMAP_STRIDE;
		uint32_t lo = 0, hi = 0;
		if (i == 0) {
			lo = (uint32_t)TC956X_DMA_OFFSET | TC956X_TAMAP_IMPL | (TC956X_TAMAP_SIZE << TC956X_TAMAP_SIZE_SHIFT);
			hi = (uint32_t)(TC956X_DMA_OFFSET >> 32);
		}
		ND_TC956X_BRIDGE_WRITE(sc, e + TC956X_TAMAP_SRC_LO, lo);
		ND_TC956X_BRIDGE_WRITE(sc, e + TC956X_TAMAP_SRC_HI, hi);
		ND_TC956X_BRIDGE_WRITE(sc, e + TC956X_TAMAP_TRSL_LO, 0);
		ND_TC956X_BRIDGE_WRITE(sc, e + TC956X_TAMAP_TRSL_HI, 0);
		ND_TC956X_BRIDGE_WRITE(sc, e + TC956X_TAMAP_TRSL_PARAM, 0);
	}

	/* Start the MSI generators; clock first, then release the reset. */
	nd_tc956x_sfr_update(sc, TC956X_NCLKCTRL(0), 0, TC956X_CLK_MSIGEN);
	nd_tc956x_sfr_update(sc, TC956X_NRSTCTRL(0), TC956X_RST_MSIGEN, 0);
}

/*
 * MDIO
 */

/*
 * One MDIO transaction on the XGMAC's controller. addr is the address
 * register's register and device fields; the port is marked clause 22 or
 * clause 45 to match. Returns the data read, or -1 if the bus stays busy.
 */
static inline int
nd_tc956x_mdio_xfer(struct nd_tc956x *sc, int phy, uint32_t addr, bool c22, uint32_t cmd, uint16_t val)
{
	uint32_t data = ND_TC956X_MAC_OFF(sc, XGMAC_MDIO_DATA);
	if (!nd_tc956x_wait(sc, data, XGMAC_MDIO_BUSY, 0, 1, ND_TC956X_MDIO_TIMEOUT)) {
		return -1;
	}
	uint32_t c22p = c22 ? 1u << phy : ND_TC956X_MAC_READ(sc, XGMAC_MDIO_C22P) & ~(1u << phy);
	ND_TC956X_MAC_WRITE(sc, XGMAC_MDIO_C22P, c22p);
	ND_TC956X_MAC_WRITE(sc, XGMAC_MDIO_ADDR, ((uint32_t)phy << XGMAC_MDIO_ADDR_PA_SHIFT) | addr);
	ND_TC956X_MAC_WRITE(sc, XGMAC_MDIO_DATA,
	    ((uint32_t)ND_TC956X_MDIO_CR << XGMAC_MDIO_CR_SHIFT) | cmd | XGMAC_MDIO_BUSY | val);
	if (!nd_tc956x_wait(sc, data, XGMAC_MDIO_BUSY, 0, 1, ND_TC956X_MDIO_TIMEOUT)) {
		return -1;
	}
	return (int)(ND_TC956X_READ(sc, data) & XGMAC_MDIO_DATA_MASK);
}

static inline int
nd_tc956x_mdio_read(struct nd_tc956x *sc, int phy, int reg)
{
	return nd_tc956x_mdio_xfer(sc, phy, (uint32_t)reg & XGMAC_MDIO_ADDR_C22_REG_MASK, true, XGMAC_MDIO_CMD_READ, 0);
}

static inline void
nd_tc956x_mdio_write(struct nd_tc956x *sc, int phy, int reg, uint16_t val)
{
	(void)nd_tc956x_mdio_xfer(sc, phy, (uint32_t)reg & XGMAC_MDIO_ADDR_C22_REG_MASK, true, XGMAC_MDIO_CMD_WRITE, val);
}

static inline int
nd_tc956x_mdio_read_c45(struct nd_tc956x *sc, int phy, int mmd, int reg)
{
	return nd_tc956x_mdio_xfer(sc, phy, ((uint32_t)mmd << XGMAC_MDIO_ADDR_DA_SHIFT) | ((uint32_t)reg & 0xffff), false,
	           XGMAC_MDIO_CMD_READ, 0);
}

static inline void
nd_tc956x_mdio_write_c45(struct nd_tc956x *sc, int phy, int mmd, int reg, uint16_t val)
{
	(void)nd_tc956x_mdio_xfer(sc, phy, ((uint32_t)mmd << XGMAC_MDIO_ADDR_DA_SHIFT) | ((uint32_t)reg & 0xffff), false,
	    XGMAC_MDIO_CMD_WRITE, val);
}

/*
 * PHY
 */

static inline int
nd_tc956x_phy_read(struct nd_tc956x *sc, int reg)
{
	return nd_tc956x_mdio_read(sc, TC956X_PHY_ADDR, reg);
}

static inline void
nd_tc956x_phy_write(struct nd_tc956x *sc, int reg, uint16_t val)
{
	nd_tc956x_mdio_write(sc, TC956X_PHY_ADDR, reg, val);
}

/* Point the PHY's clause 22 MMD window at an MMD register. */
static inline void
nd_tc956x_phy_mmd_select(struct nd_tc956x *sc, int mmd, int reg)
{
	nd_tc956x_phy_write(sc, ND_MII_MMDACR, (uint16_t)mmd);
	nd_tc956x_phy_write(sc, ND_MII_MMDAADR, (uint16_t)reg);
	nd_tc956x_phy_write(sc, ND_MII_MMDACR, (uint16_t)(ND_MMDACR_FN_DATANPI | (uint32_t)mmd));
}

static inline int
nd_tc956x_phy_mmd_read(struct nd_tc956x *sc, int mmd, int reg)
{
	nd_tc956x_phy_mmd_select(sc, mmd, reg);
	return nd_tc956x_phy_read(sc, ND_MII_MMDAADR);
}

static inline void
nd_tc956x_phy_mmd_write(struct nd_tc956x *sc, int mmd, int reg, uint16_t val)
{
	nd_tc956x_phy_mmd_select(sc, mmd, reg);
	nd_tc956x_phy_write(sc, ND_MII_MMDAADR, val);
}

/* The PHY's identifier (PHYIDR1 << 16 | PHYIDR2), or 0 if it is silent. */
static inline uint32_t
nd_tc956x_phy_id(struct nd_tc956x *sc)
{
	int id1 = nd_tc956x_phy_read(sc, ND_MII_PHYIDR1);
	int id2 = nd_tc956x_phy_read(sc, ND_MII_PHYIDR2);
	if (id1 < 0 || id2 < 0) {
		return 0;
	}
	return (uint32_t)id1 << 16 | (uint32_t)id2;
}

/* Hold the PHY's SerDes FIFO in reset while the link is down. */
static inline void
nd_tc956x_phy_serdes_fifo(struct nd_tc956x *sc, bool up)
{
	int v = nd_tc956x_mdio_read_c45(sc, QCA8081_SERDES_ADDR, 1, QCA8081_SERDES_FIFO_CTRL);
	if (v < 0) {
		return;
	}
	if (up) {
		v |= QCA8081_SERDES_FIFO_RSTN;
	} else {
		v &= ~QCA8081_SERDES_FIFO_RSTN;
	}
	nd_tc956x_mdio_write_c45(sc, QCA8081_SERDES_ADDR, 1, QCA8081_SERDES_FIFO_CTRL, (uint16_t)v);
}

/*
 * Make the PHY advertise what the MAC can do: symmetric and asymmetric
 * pause, and no half duplex, which the XGMAC does not support. Firmware or
 * the PHY's defaults may differ, and changing the advertisement means
 * renegotiating, which drops the link for a few seconds. Returns true if
 * it renegotiates.
 */
static inline bool
nd_tc956x_phy_fix_advert(struct nd_tc956x *sc)
{
	int anar = nd_tc956x_phy_read(sc, ND_MII_ANAR);
	int gtcr = nd_tc956x_phy_read(sc, ND_MII_100T2CR);
	int bmcr = nd_tc956x_phy_read(sc, ND_MII_BMCR);
	if (anar < 0 || gtcr < 0 || bmcr < 0) {
		return false;
	}
	int nanar = (int)(((uint32_t)anar & ~(uint32_t)(ND_ANAR_10 | ND_ANAR_TX)) | ND_ANAR_PAUSE_SYM | ND_ANAR_PAUSE_ASYM);
	int ngtcr = gtcr & ~ND_GTCR_ADV_1000THDX;
	if (nanar == anar && ngtcr == gtcr) {
		return false;
	}
	ND_TC956X_LOG(sc, "updating PHY advertisement, renegotiating");
	nd_tc956x_phy_write(sc, ND_MII_ANAR, (uint16_t)nanar);
	nd_tc956x_phy_write(sc, ND_MII_100T2CR, (uint16_t)ngtcr);
	nd_tc956x_phy_write(sc, ND_MII_BMCR, (uint16_t)(bmcr | ND_BMCR_AUTOEN | ND_BMCR_STARTNEG));
	return true;
}

/*
 * Advertise one speed (mbps), or all of them for 0 (autoselect), full
 * duplex only, and renegotiate. Returns 0, -1 if the PHY is silent, or -2
 * for a speed the port can't do.
 */
static inline int
nd_tc956x_phy_set_media(struct nd_tc956x *sc, uint32_t mbps)
{
	int anar = nd_tc956x_phy_read(sc, ND_MII_ANAR);
	int gtcr = nd_tc956x_phy_read(sc, ND_MII_100T2CR);
	int adv25 = nd_tc956x_phy_mmd_read(sc, ND_MMD_AN, ND_MMD_AN_10GBT_CTRL);
	if (anar < 0 || gtcr < 0 || adv25 < 0) {
		return -1;
	}
	uint32_t a = (uint32_t)anar & ~(uint32_t)(ND_ANAR_10 | ND_ANAR_10_FD | ND_ANAR_TX | ND_ANAR_TX_FD);
	uint32_t g = (uint32_t)gtcr & ~(uint32_t)(ND_GTCR_ADV_1000TFDX | ND_GTCR_ADV_1000THDX);
	uint32_t v = (uint32_t)adv25 & ~(uint32_t)ND_MMD_AN_10GBT_ADV2_5G;

	bool found = false;
	for (uint32_t i = 0; i < ND_TC956X_NSPEEDS; i++) {
		const struct nd_tc956x_speed *sp = &nd_tc956x_speeds[i];
		if (mbps != 0 && mbps != sp->mbps) {
			continue;
		}
		a |= sp->anar;
		g |= sp->gtcr;
		v |= sp->adv2_5g;
		found = true;
	}
	if (!found) {
		return -2;
	}
	a |= ND_ANAR_PAUSE_SYM | ND_ANAR_PAUSE_ASYM;

	nd_tc956x_phy_write(sc, ND_MII_ANAR, (uint16_t)a);
	nd_tc956x_phy_write(sc, ND_MII_100T2CR, (uint16_t)g);
	nd_tc956x_phy_mmd_write(sc, ND_MMD_AN, ND_MMD_AN_10GBT_CTRL, (uint16_t)v);
	nd_tc956x_phy_write(sc, ND_MII_BMCR, ND_BMCR_AUTOEN | ND_BMCR_STARTNEG);
	return 0;
}

/* Resolve PAUSE use from both sides' advertisements (802.3 Annex 28B). */
static inline void
nd_tc956x_phy_resolve_pause(struct nd_tc956x *sc, struct nd_tc956x_link *l)
{
	l->txpause = l->rxpause = false;
	if (!l->up || !l->fdx) {
		return;
	}
	int anar = nd_tc956x_phy_read(sc, ND_MII_ANAR);
	int anlpar = nd_tc956x_phy_read(sc, ND_MII_ANLPAR);
	if (anar < 0 || anlpar < 0) {
		return;
	}
	if ((anar & ND_ANAR_PAUSE_SYM) != 0 && (anlpar & ND_ANLPAR_PAUSE_SYM) != 0) {
		l->txpause = l->rxpause = true;
	} else if ((anar & ND_ANAR_PAUSE_ASYM) != 0 && (anlpar & ND_ANLPAR_PAUSE_ASYM) != 0) {
		if ((anar & ND_ANAR_PAUSE_SYM) != 0) {
			l->rxpause = true;
		} else if ((anlpar & ND_ANLPAR_PAUSE_SYM) != 0) {
			l->txpause = true;
		}
	}
}

/*
 * Read the link state from the PHY. Returns false if the PHY is silent.
 * PAUSE use only changes with a new negotiation, which takes the link down,
 * so it is only read again when the link comes up or changes speed.
 */
static inline bool
nd_tc956x_phy_poll(struct nd_tc956x *sc, struct nd_tc956x_link *l)
{
	int ss = nd_tc956x_phy_read(sc, QCA808X_PHY_SPEC_STATUS);
	if (ss < 0) {
		return false;
	}
	l->up = (ss & QCA808X_SS_LINK) != 0;
	l->fdx = (ss & QCA808X_SS_DUPLEX) != 0;
	l->speed = 0;
	uint32_t code = ((uint32_t)ss & QCA808X_SS_SPEED_MASK) >> QCA808X_SS_SPEED_SHIFT;
	for (uint32_t i = 0; i < ND_TC956X_NSPEEDS; i++) {
		if (nd_tc956x_speeds[i].qca_ss == code) {
			l->speed = nd_tc956x_speeds[i].mbps;
		}
	}
	if (sc->link_known && sc->link.up && l->up && l->speed == sc->link.speed) {
		l->txpause = sc->link.txpause;
		l->rxpause = sc->link.rxpause;
	} else {
		nd_tc956x_phy_resolve_pause(sc, l);
	}
	return true;
}

static inline const struct nd_tc956x_speed *
nd_tc956x_speed_lookup(uint32_t mbps)
{
	for (uint32_t i = 0; i < ND_TC956X_NSPEEDS; i++) {
		if (nd_tc956x_speeds[i].mbps == mbps) {
			return &nd_tc956x_speeds[i];
		}
	}
	return (const struct nd_tc956x_speed *)0;
}

/* Set the MAC's port speed and PAUSE use for the current link. */
static inline void
nd_tc956x_mac_set_link(struct nd_tc956x *sc)
{
	const struct nd_tc956x_speed *sp = nd_tc956x_speed_lookup(sc->link.speed);
	uint32_t ss = sp != 0 ? sp->xgmac_ss : XGMAC_SS_2500_GMII;
	uint32_t v = ND_TC956X_MAC_READ(sc, XGMAC_TX_CONFIG) & ~XGMAC_TX_CONFIG_SS_MASK;
	ND_TC956X_MAC_WRITE(sc, XGMAC_TX_CONFIG, v | (ss << XGMAC_TX_CONFIG_SS_SHIFT));

	ND_TC956X_MAC_WRITE(sc, XGMAC_Q_TX_FLOW_CTRL(0),
	    sc->link.txpause ? XGMAC_TX_FLOW_TFE | (ND_TC956X_PAUSE_TIME << XGMAC_TX_FLOW_PT_SHIFT) : 0);
	ND_TC956X_MAC_WRITE(sc, XGMAC_RX_FLOW_CTRL, sc->link.rxpause ? XGMAC_RX_FLOW_RFE : 0);
}

/*
 * SerDes and PCS
 */

/* Point the XPCS viewport at an MMD register; returns its SFR offset. */
static inline uint32_t
nd_tc956x_xpcs_select(struct nd_tc956x *sc, int mmd, int reg)
{
	uint32_t win = ND_TC956X_MAC_OFF(sc, TC956X_XPCS_OFFSET);
	uint32_t csr = ((uint32_t)mmd << 16) | (uint32_t)reg;
	ND_TC956X_WRITE(sc, win + TC956X_XPCS_VIEWPORT, csr >> 8);
	return win + (csr & 0xff) * 4;
}

static inline int
nd_tc956x_xpcs_read(struct nd_tc956x *sc, int mmd, int reg)
{
	return (int)(ND_TC956X_READ(sc, nd_tc956x_xpcs_select(sc, mmd, reg)) & 0xffff);
}

static inline void
nd_tc956x_xpcs_write(struct nd_tc956x *sc, int mmd, int reg, uint16_t val)
{
	ND_TC956X_WRITE(sc, nd_tc956x_xpcs_select(sc, mmd, reg), val);
}

static inline void
nd_tc956x_xpcs_update(struct nd_tc956x *sc, int mmd, int reg, uint16_t clr, uint16_t set)
{
	nd_tc956x_xpcs_write(sc, mmd, reg, (uint16_t)(((uint32_t)nd_tc956x_xpcs_read(sc, mmd, reg) & ~(uint32_t)clr) | set));
}

/* The speed the SerDes selector in EMACCTL is set for, or 0. */
static inline uint32_t
nd_tc956x_sp_sel_speed(uint32_t emacctl)
{
	for (uint32_t i = 0; i < ND_TC956X_NSPEEDS; i++) {
		if (nd_tc956x_speeds[i].sp_sel == (emacctl & TC956X_EMACCTL_SP_SEL_MASK)) {
			return nd_tc956x_speeds[i].mbps;
		}
	}
	return 0;
}

/*
 * Restart the SerDes. It takes its rate from the speed selector, so that
 * must be valid first: out of reset it holds 8, which is no SGMII rate, and
 * in-band autonegotiation then never completes. An unknown speed gets
 * SGMII at 1G.
 */
static inline bool
nd_tc956x_pma_init(struct nd_tc956x *sc, uint32_t speed)
{
	const struct nd_tc956x_speed *sp = nd_tc956x_speed_lookup(speed);
	uint32_t emacctl = TC956X_NEMACCTL(sc->mac);
	uint32_t v = ND_TC956X_READ(sc, emacctl);
	v &= ~(TC956X_EMACCTL_SP_SEL_MASK | TC956X_EMACCTL_PHY_INF_MASK | TC956X_EMACCTL_INV_SGM_SIGDET);
	v |= (sp != 0 ? sp->sp_sel : (uint32_t)TC956X_EMACCTL_SP_1000) | TC956X_EMACCTL_PHY_INF_PHYCLK |
	    TC956X_EMACCTL_LPIHWCLKEN;
	ND_TC956X_WRITE(sc, emacctl, v);

	/* The clock settings may only change with the PMA in reset. */
	nd_tc956x_sfr_update(sc, TC956X_NRSTCTRL(sc->mac), 0, TC956X_RST_PMA);
	uint32_t pma = ND_TC956X_MAC_OFF(sc, TC956X_PMA_OFFSET);
	ND_TC956X_WRITE(sc, pma + TC956X_PMA_CML_GL_PM_CFG0, 0);
	for (uint32_t i = 0; i < TC956X_PMA_NLANES; i++) {
		ND_TC956X_WRITE(sc, pma + TC956X_PMA_HWT_REFCK_R_EN(i), 0);
		ND_TC956X_WRITE(sc, pma + TC956X_PMA_HWT_REFCK_TERM_EN(i), 0);
		ND_TC956X_WRITE(sc, pma + TC956X_PMA_COMM_CFG_0_1(i), TC956X_PMA_COMM_CFG_REFCLK_I);
	}
	nd_tc956x_sfr_update(sc, TC956X_NRSTCTRL(sc->mac), TC956X_RST_PMA, 0);

	if (!nd_tc956x_wait(sc, emacctl, TC956X_EMACCTL_INIT_DONE, TC956X_EMACCTL_INIT_DONE, 100, ND_TC956X_PMA_TIMEOUT)) {
		ND_TC956X_LOG(sc, "SerDes did not come up at %u Mb/s", (unsigned)speed);
		return false;
	}
	return true;
}

/*
 * Set the XPCS up for 2500BASE-X, or for MAC-side SGMII with in-band
 * autonegotiation and automatic speed switching. Its MMD 31 starts with
 * clause 22 registers, laid out as in mii.h.
 */
static inline bool
nd_tc956x_xpcs_config(struct nd_tc956x *sc, bool sgmii)
{
	uint32_t i;
	nd_tc956x_xpcs_write(sc, XPCS_MMD_VEND2, ND_MII_BMCR, ND_BMCR_RESET);
	for (i = 0; i < ND_TC956X_XPCS_RESET_TIMEOUT; i += 1000) {
		ND_TC956X_DELAY(sc, 1000);
		if ((nd_tc956x_xpcs_read(sc, XPCS_MMD_VEND2, ND_MII_BMCR) & ND_BMCR_RESET) == 0) {
			break;
		}
	}
	if (i >= ND_TC956X_XPCS_RESET_TIMEOUT) {
		ND_TC956X_LOG(sc, "PCS reset timed out");
		return false;
	}

	/*
	 * This PCS can do 10GBASE-R, which is its default and makes it ignore
	 * the mode bits. A reserved type selects mode switching.
	 */
	if ((nd_tc956x_xpcs_read(sc, XPCS_MMD_PCS, XPCS_PCS_STAT2) & XPCS_PCS_STAT2_10GBR) != 0) {
		nd_tc956x_xpcs_write(sc, XPCS_MMD_PCS, XPCS_PCS_CTRL2, XPCS_PCS_TYPE_SEL_MODAL);
	}

	int bmcr = nd_tc956x_xpcs_read(sc, XPCS_MMD_VEND2, ND_MII_BMCR);
	if (sgmii) {
		nd_tc956x_xpcs_write(sc, XPCS_MMD_VEND2, ND_MII_BMCR, (uint16_t)(bmcr & ~ND_BMCR_AUTOEN));
		nd_tc956x_xpcs_update(sc, XPCS_MMD_VEND2, XPCS_VR_MII_AN_CTRL,
		    XPCS_AN_CTRL_PCS_MODE_MASK | XPCS_AN_CTRL_TX_CONFIG_PHY, XPCS_AN_CTRL_PCS_MODE_SGMII);
		nd_tc956x_xpcs_update(sc, XPCS_MMD_VEND2, XPCS_VR_MII_DIG_CTRL1, XPCS_DIG_CTRL1_2G5_EN,
		    XPCS_DIG_CTRL1_MAC_AUTO_SW);
		nd_tc956x_xpcs_write(sc, XPCS_MMD_VEND2, ND_MII_BMCR, (uint16_t)(bmcr | ND_BMCR_AUTOEN));
	} else {
		nd_tc956x_xpcs_update(sc, XPCS_MMD_VEND2, XPCS_VR_MII_DIG_CTRL1, XPCS_DIG_CTRL1_MAC_AUTO_SW,
		    XPCS_DIG_CTRL1_2G5_EN);
		nd_tc956x_xpcs_write(sc, XPCS_MMD_VEND2, ND_MII_BMCR,
		    (uint16_t)((bmcr & ~(ND_BMCR_AUTOEN | ND_BMCR_S100)) | ND_BMCR_S1000));
	}
	return true;
}

/* Match the SerDes and PCS to the speed the PHY linked at. */
static inline bool
nd_tc956x_serdes_config(struct nd_tc956x *sc, uint32_t speed)
{
	bool ok = nd_tc956x_pma_init(sc, speed);
	if (ok && (sc->serdes_speed == 0 || (sc->serdes_speed == 2500) != (speed == 2500))) {
		ok = nd_tc956x_xpcs_config(sc, speed != 2500);
	}
	sc->serdes_speed = ok ? speed : 0;
	return ok;
}

/*
 * Bring the port in line with a link state read from the PHY: SerDes and
 * PCS for the new speed, the PHY's SerDes FIFO, the MAC's speed and PAUSE
 * use. Returns what the includer must report.
 */
static inline enum nd_tc956x_link_event
nd_tc956x_link_apply(struct nd_tc956x *sc, const struct nd_tc956x_link *l)
{
	if (l->up && l->speed != 0 && l->speed != sc->serdes_speed) {
		(void)nd_tc956x_serdes_config(sc, l->speed);
	}

	if (sc->link_known && l->up == sc->link.up &&
	    (!l->up || (l->speed == sc->link.speed && l->fdx == sc->link.fdx && l->txpause == sc->link.txpause &&
	    l->rxpause == sc->link.rxpause))) {
		return ND_TC956X_LINK_SAME;
	}

	bool changed = !sc->link_known || l->up != sc->link.up;
	sc->link = *l;
	sc->link_known = true;
	if (changed) {
		nd_tc956x_phy_serdes_fifo(sc, l->up);
	}
	if (!l->up) {
		return ND_TC956X_LINK_WENT_DOWN;
	}
	if (!l->fdx) {
		ND_TC956X_LOG(sc, "half-duplex link: the MAC only does full duplex, expect errors");
	}
	nd_tc956x_mac_set_link(sc);
	return ND_TC956X_LINK_WENT_UP;
}

/* One link check: poll the PHY and apply what it says. */
static inline enum nd_tc956x_link_event
nd_tc956x_link_check(struct nd_tc956x *sc)
{
	struct nd_tc956x_link l;
	if (!nd_tc956x_phy_poll(sc, &l)) {
		return ND_TC956X_LINK_SAME;
	}
	return nd_tc956x_link_apply(sc, &l);
}

/*
 * Start a MAC from reset: clocks, then MAC reset, then the SerDes at a
 * valid rate, then the PCS. The PHY's current speed is used if it has a
 * link; otherwise SGMII at 1G, which the next link change corrects.
 */
static inline bool
nd_tc956x_mac_start(struct nd_tc956x *sc)
{
	struct nd_tc956x_link l;

	/* Put the MAC, SerDes and PCS in reset. */
	nd_tc956x_sfr_update(sc, TC956X_NRSTCTRL(sc->mac), 0, TC956X_RST_MAC | TC956X_RST_PMA | TC956X_RST_XPCS);

	uint32_t clk = TC956X_CLK_MAC_TX | TC956X_CLK_MAC_RX | TC956X_CLK_MAC_ALL;
	if (sc->mac == 1) {
		clk |= TC956X_CLK_MAC_RMII;
	}
	nd_tc956x_sfr_update(sc, TC956X_NCLKCTRL(sc->mac), 0, clk);
	nd_tc956x_sfr_update(sc, TC956X_NRSTCTRL(sc->mac), TC956X_RST_MAC, 0);

	uint32_t speed = (nd_tc956x_phy_poll(sc, &l) && l.up && l.speed != 0) ? l.speed : 1000;
	bool ok = nd_tc956x_pma_init(sc, speed);
	nd_tc956x_sfr_update(sc, TC956X_NRSTCTRL(sc->mac), TC956X_RST_XPCS, 0);
	if (ok) {
		ok = nd_tc956x_xpcs_config(sc, speed != 2500);
	}
	sc->serdes_speed = ok ? speed : 0;
	return ok;
}

/*
 * The station address firmware programmed, read before any MAC reset (which
 * clears it) and only from a running MAC. Returns false if there is none
 * (the includer then makes one up); the TC956x's own address lives in an
 * I2C EEPROM the driver can't read.
 */
static inline bool
nd_tc956x_read_mac_address(struct nd_tc956x *sc, uint8_t ea[6])
{
	uint32_t hi = 0, lo = 0;
	if (nd_tc956x_mac_running(sc)) {
		hi = ND_TC956X_MAC_READ(sc, XGMAC_ADDR_HIGH(0));
		lo = ND_TC956X_MAC_READ(sc, XGMAC_ADDR_LOW(0));
	}
	ea[0] = (uint8_t)lo;
	ea[1] = (uint8_t)(lo >> 8);
	ea[2] = (uint8_t)(lo >> 16);
	ea[3] = (uint8_t)(lo >> 24);
	ea[4] = (uint8_t)hi;
	ea[5] = (uint8_t)(hi >> 8);
	bool zero = (ea[0] | ea[1] | ea[2] | ea[3] | ea[4] | ea[5]) == 0;
	return (hi & XGMAC_ADDR_HIGH_AE) != 0 && (ea[0] & 1) == 0 && !zero;
}

/*
 * The rest of FreeBSD's attach, once the address has been read: keep a MAC
 * that firmware left running if cold_init is false (so that booting from the
 * network does not drop the link), otherwise start it from reset. Returns
 * false if the MAC did not start cleanly (attach goes on, as FreeBSD's
 * does: the next link change retries the SerDes).
 */
static inline bool
nd_tc956x_attach_mac(struct nd_tc956x *sc, bool cold_init)
{
	uint32_t v = ND_TC956X_READ(sc, TC956X_NEMACCTL(sc->mac));
	if (cold_init || !nd_tc956x_mac_running(sc) || (v & TC956X_EMACCTL_INIT_DONE) == 0) {
		if (!nd_tc956x_mac_start(sc)) {
			ND_TC956X_LOG(sc, "MAC %d did not start cleanly", sc->mac);
			return false;
		}
	} else {
		sc->serdes_speed = nd_tc956x_sp_sel_speed(v);
	}
	return true;
}

/*
 * Filters
 */

/* FreeBSD's ether_crc32_be. */
static inline uint32_t
nd_tc956x_crc32_be(const uint8_t *buf, uint32_t len)
{
	uint32_t crc = 0xffffffff;
	for (uint32_t i = 0; i < len; i++) {
		uint8_t data = buf[i];
		for (int bit = 0; bit < 8; bit++, data >>= 1) {
			uint32_t carry = ((crc & 0x80000000) ? 1 : 0) ^ (data & 0x01);
			crc <<= 1;
			if (carry) {
				crc = (crc ^ 0x04c11db6) | carry;
			}
		}
	}
	return crc;
}

/* Add a multicast address to a two-word hash table. */
static inline void
nd_tc956x_hash_add(uint32_t hash[2], const uint8_t addr[6])
{
	uint32_t bin = ~nd_tc956x_crc32_be(addr, 6) >> (32 - XGMAC_HASH_BITS_LOG2);
	hash[bin >> 5] |= 1u << (bin & 0x1f);
}

/* The packet filter: promiscuous, all multicast, or the hash table. */
static inline void
nd_tc956x_set_filter(struct nd_tc956x *sc, bool promisc, bool allmulti, const uint32_t table[2])
{
	uint32_t hash[2] = { 0, 0 };
	uint32_t v = XGMAC_FILTER_HPF;
	if (promisc) {
		v |= XGMAC_FILTER_PR;
	}
	if (allmulti) {
		v |= XGMAC_FILTER_PM;
		hash[0] = hash[1] = 0xffffffff;
	} else {
		v |= XGMAC_FILTER_HMC;
		hash[0] = table[0];
		hash[1] = table[1];
	}
	ND_TC956X_MAC_WRITE(sc, XGMAC_HASH_TABLE(0), hash[0]);
	ND_TC956X_MAC_WRITE(sc, XGMAC_HASH_TABLE(1), hash[1]);
	ND_TC956X_MAC_WRITE(sc, XGMAC_PACKET_FILTER, v);
}

/*
 * The MTL flow control thresholds, which count down from a full FIFO in
 * steps of 512 bytes after the first 1KB: ask for a pause while the
 * receive FIFO still has room for two of the largest frames or 8KB,
 * whichever is more, and release it 4KB lower.
 */
static inline uint32_t
nd_tc956x_flow_thresholds(const struct nd_tc956x *sc)
{
	uint32_t headroom = 2 * sc->max_frame > ND_TC956X_FC_HEADROOM_MIN ? 2 * sc->max_frame : ND_TC956X_FC_HEADROOM_MIN;
	uint32_t rfa = (headroom - 1024 + 511) / 512;
	uint32_t rfd = rfa + ND_TC956X_FC_HYSTERESIS / 512;
	return (rfa << XGMAC_MTL_RFA_SHIFT) | (rfd << XGMAC_MTL_RFD_SHIFT);
}

/*
 * Start and stop
 */

struct nd_tc956x_init_params {
	const uint8_t *lladdr;          /* 6 bytes */
	bool promisc, allmulti;
	const uint32_t *hash;           /* two words */
	bool rx_csum;
	uint32_t rx_bufsz;              /* a multiple of 16, at most 16368 */
};

/*
 * FreeBSD's tcx_init: reset the DMA, MTL and MAC (not the PCS or PMA),
 * program them for one queue each way, and start. The receive ring is
 * empty: refilling it moves the tail pointer and lets the RX DMA go.
 * Returns false if the DMA does not come out of reset.
 */
static inline bool
nd_tc956x_init(struct nd_tc956x *sc, const struct nd_tc956x_init_params *p)
{
	uint32_t v;
	uint64_t a;

	ND_TC956X_MAC_WRITE(sc, XGMAC_DMA_MODE, XGMAC_DMA_MODE_SWR);
	if (!nd_tc956x_wait(sc, ND_TC956X_MAC_OFF(sc, XGMAC_DMA_MODE), XGMAC_DMA_MODE_SWR, 0, 10, ND_TC956X_SWR_TIMEOUT)) {
		ND_TC956X_LOG(sc, "DMA reset timed out");
		return false;
	}

	/* DMA: bus behaviour as firmware and Linux set it. */
	ND_TC956X_MAC_WRITE(sc, XGMAC_DMA_SYSBUS_MODE, (31u << XGMAC_SYSBUS_WR_OSR_SHIFT) |
	    (31u << XGMAC_SYSBUS_RD_OSR_SHIFT) | XGMAC_SYSBUS_EAME | XGMAC_SYSBUS_BLEN4_128 | XGMAC_SYSBUS_UNDEF);
	ND_TC956X_MAC_WRITE(sc, XGMAC_TX_EDMA_CTRL, XGMAC_EDMA_PS_MAX);
	ND_TC956X_MAC_WRITE(sc, XGMAC_RX_EDMA_CTRL, XGMAC_EDMA_PS_MAX);
	/* Channel interrupts on their own lines, into the MSI generator. */
	v = ND_TC956X_MAC_READ(sc, XGMAC_DMA_MODE) & ~XGMAC_DMA_MODE_INTM_MASK;
	ND_TC956X_MAC_WRITE(sc, XGMAC_DMA_MODE, v | XGMAC_DMA_MODE_INTM_PERCH);

	/* MTL: one queue each way, store and forward. */
	ND_TC956X_MAC_WRITE(sc, XGMAC_MTL_TXQ_OPMODE(0),
	    XGMAC_MTL_TSF | XGMAC_MTL_TXQEN_ENABLED | XGMAC_MTL_QS(ND_TC956X_TX_FIFO_BYTES));
	ND_TC956X_MAC_WRITE(sc, XGMAC_MTL_TC_ETS_CONTROL(0), 0);
	ND_TC956X_MAC_WRITE(sc, XGMAC_MTL_RXQ_FLOW_CONTROL(0), nd_tc956x_flow_thresholds(sc));
	ND_TC956X_MAC_WRITE(sc, XGMAC_MTL_RXQ_OPMODE(0),
	    XGMAC_MTL_RSF | XGMAC_MTL_EHFC | XGMAC_MTL_QS(ND_TC956X_RX_FIFO_BYTES));

	/* MAC */
	const uint8_t *ea = p->lladdr;
	ND_TC956X_MAC_WRITE(sc, XGMAC_ADDR_HIGH(0), XGMAC_ADDR_HIGH_AE | ((uint32_t)ea[5] << 8) | ea[4]);
	ND_TC956X_MAC_WRITE(sc, XGMAC_ADDR_LOW(0),
	    ((uint32_t)ea[3] << 24) | ((uint32_t)ea[2] << 16) | ((uint32_t)ea[1] << 8) | ea[0]);
	nd_tc956x_set_filter(sc, p->promisc, p->allmulti, p->hash);
	v = XGMAC_RX_CONFIG_ACS | XGMAC_RX_CONFIG_CST | XGMAC_RX_CONFIG_GPSLCE | XGMAC_RX_CONFIG_WD |
	    ((uint32_t)XGMAC_RX_CONFIG_GPSL_MAX << XGMAC_RX_CONFIG_GPSL_SHIFT);
	sc->rx_csum = p->rx_csum;
	if (sc->rx_csum) {
		v |= XGMAC_RX_CONFIG_IPC;
	}
	ND_TC956X_MAC_WRITE(sc, XGMAC_RX_CONFIG, v);
	ND_TC956X_MAC_WRITE(sc, XGMAC_TX_CONFIG, XGMAC_TX_CONFIG_JD);
	/* The reset cleared the port speed; the link code keeps it current. */
	nd_tc956x_mac_set_link(sc);
	ND_TC956X_MAC_WRITE(sc, XGMAC_RXQ_CTRL0, XGMAC_RXQ_EN_DCB);
	ND_TC956X_MAC_WRITE(sc, XGMAC_INT_EN, 0);

	/* DMA channel 0. */
	sc->rx_bufsz = p->rx_bufsz;
	ND_TC956X_MAC_WRITE(sc, XGMAC_DMA_CH_CONTROL(0), XGMAC_DMA_CH_PBLX8);
	ND_TC956X_MAC_WRITE(sc, XGMAC_DMA_CH_TX_CONTROL(0),
	    ((uint32_t)ND_TC956X_PBL << XGMAC_DMA_CH_PBL_SHIFT) | XGMAC_DMA_CH_TSE);
	ND_TC956X_MAC_WRITE(sc, XGMAC_DMA_CH_RX_CONTROL(0), ((uint32_t)ND_TC956X_PBL << XGMAC_DMA_CH_PBL_SHIFT) |
	    ((sc->rx_bufsz << XGMAC_DMA_CH_RBSZ_SHIFT) & XGMAC_DMA_CH_RBSZ_MASK));

	sc->tx_cidx = sc->tx_pidx = 0;
	sc->tx_since_ioc = 0;
	sc->rx_ioc_count = 0;
	a = nd_tc956x_dma_addr(sc->tx_pa);
	ND_TC956X_MAC_WRITE(sc, XGMAC_DMA_CH_TXDESC_HADDR(0), (uint32_t)(a >> 32));
	ND_TC956X_MAC_WRITE(sc, XGMAC_DMA_CH_TXDESC_LADDR(0), (uint32_t)a);
	ND_TC956X_MAC_WRITE(sc, XGMAC_DMA_CH_TXDESC_RING_LEN(0), sc->ntxd - 1);
	ND_TC956X_MAC_WRITE(sc, XGMAC_DMA_CH_TXDESC_TAIL(0), (uint32_t)a);

	sc->rx_cidx = sc->rx_pidx = 0;
	a = nd_tc956x_dma_addr(sc->rx_pa);
	ND_TC956X_MAC_WRITE(sc, XGMAC_DMA_CH_RXDESC_HADDR(0), (uint32_t)(a >> 32));
	ND_TC956X_MAC_WRITE(sc, XGMAC_DMA_CH_RXDESC_LADDR(0), (uint32_t)a);
	ND_TC956X_MAC_WRITE(sc, XGMAC_DMA_CH_RXDESC_RING_LEN(0), (sc->nrxd - 1) | (3u << XGMAC_DMA_CH_OWRQ_SHIFT));
	ND_TC956X_MAC_WRITE(sc, XGMAC_DMA_CH_RXDESC_TAIL(0), (uint32_t)a);

	ND_TC956X_MAC_WRITE(sc, XGMAC_DMA_CH_RX_WATCHDOG(0), sc->rx_riwt & XGMAC_DMA_CH_RWT_MASK);
	ND_TC956X_MAC_WRITE(sc, XGMAC_DMA_CH_STATUS(0), 0xffffffff);
	ND_TC956X_MAC_WRITE(sc, XGMAC_DMA_CH_INT_EN(0),
	    XGMAC_DMA_CH_NIS | XGMAC_DMA_CH_AIS | XGMAC_DMA_CH_FBE | XGMAC_DMA_CH_RI | XGMAC_DMA_CH_TI);

	/* Start. */
	v = ND_TC956X_MAC_READ(sc, XGMAC_DMA_CH_TX_CONTROL(0));
	ND_TC956X_MAC_WRITE(sc, XGMAC_DMA_CH_TX_CONTROL(0), v | XGMAC_DMA_CH_TXST);
	v = ND_TC956X_MAC_READ(sc, XGMAC_TX_CONFIG);
	ND_TC956X_MAC_WRITE(sc, XGMAC_TX_CONFIG, v | XGMAC_TX_CONFIG_TE);
	v = ND_TC956X_MAC_READ(sc, XGMAC_DMA_CH_RX_CONTROL(0));
	ND_TC956X_MAC_WRITE(sc, XGMAC_DMA_CH_RX_CONTROL(0), v | XGMAC_DMA_CH_RXST);
	v = ND_TC956X_MAC_READ(sc, XGMAC_RX_CONFIG);
	ND_TC956X_MAC_WRITE(sc, XGMAC_RX_CONFIG, v | XGMAC_RX_CONFIG_RE);
	return true;
}

/*
 * Stop the datapath. The includer frees the buffers once this returns, so
 * the DMA must have stopped reading them: let the transmit DMA finish its
 * current packet, and the MAC send what it holds, before turning it off;
 * then stop receiving and let the DMA drain what the MTL holds.
 */
static inline void
nd_tc956x_stop(struct nd_tc956x *sc)
{
	uint32_t v;
	ND_TC956X_MSI_WRITE(sc, TC956X_MSI_OUT_EN, 0);
	ND_TC956X_MAC_WRITE(sc, XGMAC_DMA_CH_INT_EN(0), 0);

	v = ND_TC956X_MAC_READ(sc, XGMAC_DMA_CH_TX_CONTROL(0));
	ND_TC956X_MAC_WRITE(sc, XGMAC_DMA_CH_TX_CONTROL(0), v & ~XGMAC_DMA_CH_TXST);
	for (uint32_t i = 0; i < ND_TC956X_STOP_TIMEOUT; i += 100) {
		if ((ND_TC956X_MAC_READ(sc, XGMAC_DMA_CH_STATUS(0)) & XGMAC_DMA_CH_TPS) != 0 &&
		    (ND_TC956X_MAC_READ(sc, XGMAC_MTL_TXQ_DEBUG(0)) & XGMAC_MTL_TXQ_NOT_EMPTY) == 0) {
			break;
		}
		ND_TC956X_DELAY(sc, 100);
	}
	v = ND_TC956X_MAC_READ(sc, XGMAC_TX_CONFIG);
	ND_TC956X_MAC_WRITE(sc, XGMAC_TX_CONFIG, v & ~XGMAC_TX_CONFIG_TE);

	v = ND_TC956X_MAC_READ(sc, XGMAC_RX_CONFIG);
	ND_TC956X_MAC_WRITE(sc, XGMAC_RX_CONFIG, v & ~XGMAC_RX_CONFIG_RE);
	(void)nd_tc956x_wait(sc, ND_TC956X_MAC_OFF(sc, XGMAC_MTL_RXQ_DEBUG(0)), XGMAC_MTL_RXQ_NOT_EMPTY, 0, 100,
	    ND_TC956X_STOP_TIMEOUT);
	v = ND_TC956X_MAC_READ(sc, XGMAC_DMA_CH_RX_CONTROL(0));
	ND_TC956X_MAC_WRITE(sc, XGMAC_DMA_CH_RX_CONTROL(0), v & ~XGMAC_DMA_CH_RXST);
	ND_TC956X_MAC_WRITE(sc, XGMAC_DMA_CH_STATUS(0), 0xffffffff);
}

/*
 * Interrupts
 */

static inline void
nd_tc956x_intr_enable(struct nd_tc956x *sc)
{
	ND_TC956X_MSI_WRITE(sc, TC956X_MSI_OUT_EN, (1u << TC956X_MSI_SRC_TX(0)) | (1u << TC956X_MSI_SRC_RX(0)));
	ND_TC956X_MSI_WRITE(sc, TC956X_MSI_MASK_CLR, TC956X_MSI_MASK_CLR_ALL);
}

static inline void
nd_tc956x_intr_disable(struct nd_tc956x *sc)
{
	ND_TC956X_MSI_WRITE(sc, TC956X_MSI_OUT_EN, 0);
}

/*
 * FreeBSD's interrupt filter: the MSI is this function's alone, and the
 * channel status says what happened. Returns 0 for a stray (the generator
 * is re-armed), else the status, acknowledged, with the generator's output
 * off: the includer services the rings and calls nd_tc956x_intr_enable.
 * FBE means the channel has stopped and only a new init restarts it.
 */
static inline uint32_t
nd_tc956x_intr_status(struct nd_tc956x *sc)
{
	uint32_t st = ND_TC956X_MAC_READ(sc, XGMAC_DMA_CH_STATUS(0));
	if (st == 0) {
		ND_TC956X_MSI_WRITE(sc, TC956X_MSI_MASK_CLR, TC956X_MSI_MASK_CLR_ALL);
		return 0;
	}
	ND_TC956X_MSI_WRITE(sc, TC956X_MSI_OUT_EN, 0);
	ND_TC956X_MAC_WRITE(sc, XGMAC_DMA_CH_STATUS(0), st);
	sc->stat_intr++;
	if ((st & XGMAC_DMA_CH_RI) != 0) {
		sc->stat_intr_rx++;
	}
	if ((st & XGMAC_DMA_CH_TI) != 0) {
		sc->stat_intr_tx++;
	}
	if ((st & XGMAC_DMA_CH_RBU) != 0) {
		sc->stat_rbu++;
	}
	if ((st & XGMAC_DMA_CH_FBE) != 0) {
		sc->stat_fbe++;
		ND_TC956X_LOG(sc, "DMA bus error, status 0x%08x", (unsigned)st);
	}
	return st;
}

/*
 * Transmit
 */

/* A buffer of a packet: host physical address and length. */
struct nd_tc956x_seg {
	uint64_t pa;
	uint32_t len;
};

/* Checksum insertion for the first descriptor of a packet. */
enum nd_tc956x_tx_csum {
	ND_TC956X_TX_CSUM_NONE = 0,
	ND_TC956X_TX_CSUM_IP,           /* the IPv4 header only */
	ND_TC956X_TX_CSUM_FULL,         /* IPv4 header and TCP/UDP, IPv4 or IPv6 */
};

/* Descriptors free: one always stays empty, so full is not empty. */
static inline uint32_t
nd_tc956x_tx_free(const struct nd_tc956x *sc)
{
	return sc->ntxd - 1 - ((sc->tx_pidx - sc->tx_cidx) & (sc->ntxd - 1));
}

static inline void
nd_tc956x_tx_desc(volatile struct nd_tc956x_desc *d, uint64_t a, uint32_t des2, uint32_t des3)
{
	d->des0 = (uint32_t)a;
	d->des1 = (uint32_t)(a >> 32);
	d->des2 = des2;
	d->des3 = des3;
}

/* Should the last descriptor of a packet of ndesc ask for an interrupt? */
static inline bool
nd_tc956x_tx_want_ioc(struct nd_tc956x *sc, uint32_t ndesc)
{
	uint32_t quarter = sc->ntxd / 4;
	sc->tx_since_ioc += ndesc;
	if (sc->tx_since_ioc < (sc->tx_coal_frames < quarter ? sc->tx_coal_frames : quarter)) {
		return false;
	}
	sc->tx_since_ioc = 0;
	return true;
}

/*
 * One packet, one descriptor per segment (FreeBSD's tcx_txd_encap without
 * TSO): FD and the checksum insertion on the first, LD on the last, the
 * frame length on each. Returns the index of the last descriptor, or -1 if
 * the ring lacks room or a segment is too long (the includer then drops or
 * copies the packet). The descriptors are the DMA's once the includer has
 * made them visible and called nd_tc956x_tx_flush.
 */
static inline int
nd_tc956x_tx_encap(struct nd_tc956x *sc, const struct nd_tc956x_seg *segs, uint32_t nsegs, uint32_t pktlen,
    enum nd_tc956x_tx_csum csum)
{
	uint32_t mask = sc->ntxd - 1;
	if (nsegs == 0 || nsegs > nd_tc956x_tx_free(sc) || pktlen > TDES3_FL_MASK) {
		return -1;
	}
	for (uint32_t i = 0; i < nsegs; i++) {
		if (segs[i].len == 0 || segs[i].len > TDES2_B1L_MASK) {
			return -1;
		}
	}
	uint32_t pidx = sc->tx_pidx, last = 0;
	for (uint32_t i = 0; i < nsegs; i++) {
		uint32_t des3 = TDES3_OWN | (pktlen & TDES3_FL_MASK);
		if (i == 0) {
			des3 |= TDES3_FD;
			des3 |= csum == ND_TC956X_TX_CSUM_FULL ? TDES3_CIC_FULL : csum == ND_TC956X_TX_CSUM_IP ? TDES3_CIC_IP : 0;
		}
		uint32_t des2 = segs[i].len & TDES2_B1L_MASK;
		if (i == nsegs - 1) {
			des3 |= TDES3_LD;
			if (nd_tc956x_tx_want_ioc(sc, nsegs)) {
				des2 |= TDES2_IOC;
			}
		}
		nd_tc956x_tx_desc(&sc->txd[pidx], nd_tc956x_dma_addr(segs[i].pa), des2, des3);
		last = pidx;
		pidx = (pidx + 1) & mask;
	}
	sc->tx_pidx = pidx;
	return (int)last;
}

/* The DMA works up to, but not including, the tail descriptor. */
static inline void
nd_tc956x_tx_flush(struct nd_tc956x *sc)
{
	ND_TC956X_MAC_WRITE(sc, XGMAC_DMA_CH_TXDESC_TAIL(0),
	    (uint32_t)nd_tc956x_dma_addr(sc->tx_pa + (uint64_t)sc->tx_pidx * sizeof(struct nd_tc956x_desc)));
}

/*
 * Descriptors the DMA has finished with, whole packets only (FreeBSD's
 * tcx_txd_credits_update with clear): the DMA clears OWN in each, and the
 * last of a packet has LD. Advances tx_cidx and returns how far; the
 * includer frees the packets whose last descriptors it passed.
 */
static inline uint32_t
nd_tc956x_tx_reclaim(struct nd_tc956x *sc)
{
	uint32_t mask = sc->ntxd - 1, count = 0, done = 0;
	for (uint32_t idx = sc->tx_cidx; idx != sc->tx_pidx; idx = (idx + 1) & mask) {
		uint32_t des3 = sc->txd[idx].des3;
		if ((des3 & TDES3_OWN) != 0) {
			break;
		}
		count++;
		if ((des3 & (TDES3_CTXT | TDES3_LD)) == TDES3_LD) {
			done = count;
		}
	}
	sc->tx_cidx = (sc->tx_cidx + done) & mask;
	return done;
}

/*
 * Receive
 */

/* Does the next refilled receive descriptor ask for an interrupt? */
static inline bool
nd_tc956x_rx_want_ioc(struct nd_tc956x *sc)
{
	if (sc->rx_riwt == 0) {
		return true;
	}
	if (sc->rx_coal_frames == 0) {
		return false;
	}
	if (++sc->rx_ioc_count < sc->rx_coal_frames) {
		return false;
	}
	sc->rx_ioc_count = 0;
	return true;
}

/*
 * Hand the descriptor at rx_pidx to the DMA with a buffer at host address
 * pa, and move rx_pidx on. One descriptor always stays empty, so the
 * includer fills at most nrxd - 1; nd_tc956x_rx_flush then moves the tail.
 */
static inline void
nd_tc956x_rx_refill(struct nd_tc956x *sc, uint64_t pa)
{
	volatile struct nd_tc956x_desc *d = &sc->rxd[sc->rx_pidx];
	uint64_t a = nd_tc956x_dma_addr(pa);
	d->des0 = (uint32_t)a;
	d->des1 = (uint32_t)(a >> 32);
	d->des2 = 0;
	d->des3 = RDES3_OWN | (nd_tc956x_rx_want_ioc(sc) ? RDES3_IOC : 0);
	sc->rx_pidx = (sc->rx_pidx + 1) & (sc->nrxd - 1);
}

static inline void
nd_tc956x_rx_flush(struct nd_tc956x *sc)
{
	ND_TC956X_MAC_WRITE(sc, XGMAC_DMA_CH_RXDESC_TAIL(0),
	    (uint32_t)nd_tc956x_dma_addr(sc->rx_pa + (uint64_t)sc->rx_pidx * sizeof(struct nd_tc956x_desc)));
}

/* Descriptors posted to the DMA and not yet received. */
static inline uint32_t
nd_tc956x_rx_posted(const struct nd_tc956x *sc)
{
	return (sc->rx_pidx - sc->rx_cidx) & (sc->nrxd - 1);
}

/* How the MAC checked a received frame. */
enum nd_tc956x_rx_csum {
	ND_TC956X_RX_CSUM_NONE = 0,     /* not checked: the stack checks */
	ND_TC956X_RX_CSUM_IP4TCP,       /* IPv4 header and TCP checksum good */
	ND_TC956X_RX_CSUM_IP4UDP,
	ND_TC956X_RX_CSUM_IP6TCP,       /* TCP checksum good */
	ND_TC956X_RX_CSUM_IP6UDP,
};

/* One received frame, from nd_tc956x_rx_next. */
struct nd_tc956x_rx_frame {
	uint32_t first;                 /* its first descriptor */
	uint32_t ndesc;                 /* descriptors it spans */
	uint32_t len;                   /* bytes, without the FCS */
	uint32_t des3;                  /* the last descriptor's status */
	enum nd_tc956x_rx_csum csum;
};

/*
 * Report what the MAC checked. With checksum offload on, a bad IPv4 header
 * or TCP/UDP checksum sets the error summary, as for other receive errors.
 * Such frames are passed up unchecked, for the stack to judge.
 */
static inline enum nd_tc956x_rx_csum
nd_tc956x_rx_csum(struct nd_tc956x *sc, uint32_t des3)
{
	if ((des3 & RDES3_ES) != 0) {
		sc->stat_rx_err++;
		sc->rx_err_des3 = des3;
		return ND_TC956X_RX_CSUM_NONE;
	}
	if (!sc->rx_csum) {
		return ND_TC956X_RX_CSUM_NONE;
	}
	switch ((des3 & RDES3_L34T_MASK) >> RDES3_L34T_SHIFT) {
	case RDES3_L34T_IP4TCP:
		return ND_TC956X_RX_CSUM_IP4TCP;
	case RDES3_L34T_IP4UDP:
		return ND_TC956X_RX_CSUM_IP4UDP;
	case RDES3_L34T_IP6TCP:
		return ND_TC956X_RX_CSUM_IP6TCP;
	case RDES3_L34T_IP6UDP:
		return ND_TC956X_RX_CSUM_IP6UDP;
	default:
		return ND_TC956X_RX_CSUM_NONE;
	}
}

/*
 * The next complete frame the DMA has written, from rx_cidx on (FreeBSD's
 * tcx_rxd_available and tcx_rxd_pkt_get): every descriptor up to the one
 * with LD has OWN clear. Returns false if there is none yet. On true,
 * rx_cidx moves past the frame; its descriptors are the includer's to
 * read and then to refill.
 */
static inline bool
nd_tc956x_rx_next(struct nd_tc956x *sc, struct nd_tc956x_rx_frame *f)
{
	uint32_t mask = sc->nrxd - 1, n = 0;
	for (uint32_t idx = sc->rx_cidx; idx != sc->rx_pidx; idx = (idx + 1) & mask) {
		uint32_t des3 = sc->rxd[idx].des3;
		if ((des3 & RDES3_OWN) != 0) {
			return false;
		}
		n++;
		if ((des3 & RDES3_LD) != 0) {
			f->first = sc->rx_cidx;
			f->ndesc = n;
			/* The last descriptor holds the whole length. */
			f->len = des3 & RDES3_PL_MASK;
			f->des3 = des3;
			f->csum = nd_tc956x_rx_csum(sc, des3);
			sc->rx_cidx = (idx + 1) & mask;
			return true;
		}
	}
	return false;
}

#endif /* ND_TC956X_H */
