/*-
 * SPDX-License-Identifier: BSD-2-Clause
 * NeoDarwin-Language: portability: FreeBSD-derived register sequences over two accessor macros, built into the kernel's C++ xHCI driver and on the host for its tests.
 *
 * Copyright (c) 2019 Val Packett <val@packett.cool>
 * Copyright (c) 2019 Emmanuel Vadot <manu@FreeBSD.Org>
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
 * A Synopsys DWC_usb3x core behind an ACPI USB Role Switch (PNP0CA1): the
 * Radxa Dragon Q8B's two USB-C controllers (\_SB.URS0, URS1; SC8280XP).
 * Ported from FreeBSD's generic_xhci_acpi.c (generic_xhci_acpi_urs_setup,
 * freebsd-src cbbcf73a5d, branch radxa-dragon-q8b, tested on the board)
 * and dwc3.h (the register offsets and fields); PROVENANCE.md.
 *
 * The controller is used in the role the firmware left it in, which must be
 * host mode, and a receive threshold the firmware left enabled is turned
 * off (the core's reset default); on the Q8B's USB-C controllers it holds
 * SuperSpeed reads to a third. The includer defines ND_DWC3_READ(regs,
 * offset) and ND_DWC3_WRITE(regs, offset, value) on the controller's
 * register window `regs`.
 */
#ifndef ND_DWC3_H
#define ND_DWC3_H

#include <stdint.h>

#define ND_DWC3_IP_ID                   0x5533  /* DWC_usb3 */
#define ND_DWC3_1_IP_ID                 0x3331  /* DWC_usb31 */
#define ND_DWC3_2_IP_ID                 0x3332  /* DWC_usb32 */
#define ND_DWC3_VERSION(x)              (((x) & 0xffff0000u) >> 16)
#define ND_DWC3_GRXTHRCFG               0xc10c
#define ND_DWC3_GRXTHRCFG_PKTCNTSEL     (1u << 29)
#define ND_DWC31_GRXTHRCFG_PKTCNTSEL    (1u << 26)
#define ND_DWC3_GCTL                    0xc110
#define ND_DWC3_GCTL_PRTCAPDIR_MASK     (0x3u << 12)
#define ND_DWC3_GCTL_PRTCAPDIR_HOST     (0x1u << 12)
#define ND_DWC3_GSNPSID                 0xc120

enum nd_dwc3_result {
	ND_DWC3_NOT_DWC3 = 0,   /* no DWC3 identification: nothing touched */
	ND_DWC3_READY,          /* a DWC3 core in host mode */
	ND_DWC3_NOT_HOST,       /* a DWC3 core the firmware did not leave in host mode */
};

/*
 * `size` is the register window's length. On return *gsnpsid is the
 * identification register (0 when the window is too small) and
 * *grxthrcfg its receive threshold configuration as found.
 */
static inline enum nd_dwc3_result
nd_dwc3_urs_setup(void *regs, uint64_t size, uint32_t *gsnpsid, uint32_t *grxthrcfg)
{
	uint32_t id, reg, sel;

	*gsnpsid = 0;
	*grxthrcfg = 0;
	if (size <= ND_DWC3_GSNPSID) {
		return ND_DWC3_NOT_DWC3;
	}
	*gsnpsid = ND_DWC3_READ(regs, ND_DWC3_GSNPSID);
	id = ND_DWC3_VERSION(*gsnpsid);
	if (id == ND_DWC3_IP_ID) {
		sel = ND_DWC3_GRXTHRCFG_PKTCNTSEL;
	} else if (id == ND_DWC3_1_IP_ID || id == ND_DWC3_2_IP_ID) {
		sel = ND_DWC31_GRXTHRCFG_PKTCNTSEL;
	} else {
		return ND_DWC3_NOT_DWC3;
	}
	if ((ND_DWC3_READ(regs, ND_DWC3_GCTL) & ND_DWC3_GCTL_PRTCAPDIR_MASK) != ND_DWC3_GCTL_PRTCAPDIR_HOST) {
		return ND_DWC3_NOT_HOST;
	}
	reg = ND_DWC3_READ(regs, ND_DWC3_GRXTHRCFG);
	*grxthrcfg = reg;
	if ((reg & sel) != 0) {
		ND_DWC3_WRITE(regs, ND_DWC3_GRXTHRCFG, reg & ~sel);
	}
	return ND_DWC3_READY;
}

#endif /* ND_DWC3_H */
