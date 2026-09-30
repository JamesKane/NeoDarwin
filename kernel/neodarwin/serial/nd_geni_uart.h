/*
 * SPDX-License-Identifier: BSD-2-Clause
 * NeoDarwin-Language: expressibility: a pexpert serial driver, included by xnu's pe_serial.c (C) and compiled on the host for its simulation test.
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
 * Qualcomm QUPv3 GENI UART, the polled console core (docs/kernel/serial.md).
 *
 * Ported from FreeBSD's uart_dev_qcom_geni.c and qcom_geni_reg.h (freebsd-src
 * commit 124c151cbc, branch radxa-dragon-q8b; PROVENANCE.md), whose low-level
 * console path runs on the Radxa Dragon Q8B. The register block, the init
 * sequence and the transmit and receive semantics are FreeBSD's; the uart_bas
 * and bus-layer plumbing is replaced by the accessors below and pe_serial.c's
 * serial_functions glue (patch 0032).
 *
 * The serial engine runs in FIFO mode with one character per 32-bit FIFO word
 * in each direction (packing 1x8). A character goes out as a primary
 * sequencer START_TX command of length 1 followed by its FIFO word; the
 * driver waits for the command to finish. Reception is a secondary sequencer
 * START_READ command left running; RX_FIFO_STATUS counts the words waiting.
 *
 * What firmware owns and this code never touches: the serial engine's clock
 * (the global clock controller's), hence the bit rate UEFI chose, the QUP
 * wrapper, the pin mux and the protocol firmware. UEFI leaves the SPCR
 * console engine clocked and loaded with the UART protocol; an engine that
 * isn't clocked faults when touched, so only the SPCR console is driven.
 *
 * The includer defines ND_GENI_READ(base, offset) and
 * ND_GENI_WRITE(base, offset, value), 32-bit register accessors: volatile
 * device-memory loads and stores in the kernel, the register model in the
 * host test. Nothing here allocates, sleeps or takes a lock: it runs from
 * serial_init, before the VM system, and from panic.
 */

#ifndef ND_GENI_UART_H
#define ND_GENI_UART_H

#include <stdbool.h>
#include <stdint.h>

#if !defined(ND_GENI_READ) || !defined(ND_GENI_WRITE)
#error "define ND_GENI_READ and ND_GENI_WRITE before including nd_geni_uart.h"
#endif

/* Common serial engine registers (qcom_geni_reg.h) */
#define GENI_FORCE_DEFAULT_REG          0x0020
#define  GENI_FORCE_DEFAULT             (1u << 0)
#define GENI_OUTPUT_CTRL                0x0024
#define  GENI_OUTPUT_CTRL_DEFAULT       0x7f
#define GENI_CGC_CTRL                   0x0028
#define  GENI_CGC_CTRL_DEFAULT          0x7f
#define GENI_STATUS                     0x0040
#define  GENI_STATUS_M_CMD_ACTIVE       (1u << 0)
#define  GENI_STATUS_S_CMD_ACTIVE       (1u << 12)
#define GENI_FW_REVISION_RO             0x0068
#define  GENI_FW_REV_PROTOCOL(v)        (((v) >> 8) & 0xff)
#define  GENI_PROTOCOL_UART             2
#define GENI_BYTE_GRANULARITY           0x0254
#define GENI_DMA_MODE_EN                0x0258
#define  GENI_DMA_MODE_ENABLE           (1u << 0)
#define GENI_TX_PACKING_CFG0            0x0260
#define GENI_TX_PACKING_CFG1            0x0264
#define GENI_RX_PACKING_CFG0            0x0284
#define GENI_RX_PACKING_CFG1            0x0288
/* One 8-bit word per FIFO entry, LSB first: start 0, length 8, last vector. */
#define  GENI_PACKING_1x8               0x0000000f

/* Primary (M) sequencer: transmit */
#define GENI_M_CMD0                     0x0600
#define  GENI_M_OPCODE_SHIFT            27
#define GENI_M_CMD_CTRL                 0x0604
#define  GENI_M_CMD_ABORT               (1u << 1)
#define  GENI_M_CMD_CANCEL              (1u << 2)
#define GENI_M_IRQ_STATUS               0x0610
#define GENI_M_IRQ_EN                   0x0614
#define GENI_M_IRQ_CLEAR                0x0618
#define  GENI_M_CMD_DONE                (1u << 0)
#define  GENI_M_CMD_CANCEL_DONE         (1u << 4)
#define  GENI_M_CMD_ABORT_DONE          (1u << 5)

/* Secondary (S) sequencer: receive */
#define GENI_S_CMD0                     0x0630
#define  GENI_S_OPCODE_SHIFT            27
#define GENI_S_CMD_CTRL                 0x0634
#define  GENI_S_CMD_ABORT               (1u << 1)
#define GENI_S_IRQ_EN                   0x0644
#define GENI_S_IRQ_CLEAR                0x0648
#define  GENI_S_CMD_DONE                (1u << 0)
#define  GENI_S_CMD_ABORT_DONE          (1u << 5)

/* FIFOs */
#define GENI_TX_FIFO                    0x0700
#define GENI_RX_FIFO                    0x0780
#define GENI_RX_FIFO_STATUS             0x0804
#define  GENI_RX_FIFO_WC(v)             ((v) & 0x1ffffff)
#define GENI_RX_WATERMARK               0x0810
#define GENI_RX_RFR_WATERMARK           0x0814

/* DMA and top-level interrupt control */
#define GENI_DMA_TX_IRQ_CLR             0x0c44
#define GENI_DMA_RX_IRQ_CLR             0x0d44
#define GENI_GSI_EVENT_EN               0x0e18
#define GENI_SE_IRQ_EN                  0x0e1c
#define  GENI_SE_IRQ_DMA_RX             (1u << 0)
#define  GENI_SE_IRQ_DMA_TX             (1u << 1)
#define  GENI_SE_IRQ_M                  (1u << 2)
#define  GENI_SE_IRQ_S                  (1u << 3)
#define GENI_DMA_GENERAL_CFG            0x0e30
#define  GENI_DMA_GENERAL_CFG_CGC_ON    0x0f

/* UART protocol registers */
#define GENI_UART_TX_TRANS_CFG          0x025c
#define  GENI_UART_CTS_MASK             (1u << 1)
#define GENI_UART_TX_WORD_LEN           0x0268
#define GENI_UART_TX_STOP_BIT_LEN       0x026c
#define  GENI_UART_TX_STOP_BIT_LEN_1    0
#define GENI_UART_TX_TRANS_LEN          0x0270
#define GENI_UART_RX_TRANS_CFG          0x0280
#define GENI_UART_RX_WORD_LEN           0x028c
#define GENI_UART_RX_STALE_CNT          0x0294
#define GENI_UART_TX_PARITY_CFG         0x02a4
#define GENI_UART_RX_PARITY_CFG         0x02a8

/* UART sequencer opcodes */
#define GENI_UART_M_START_TX            1
#define GENI_UART_S_START_READ          1

/* The size of a serial engine's register window: the DT reg and the mapping. */
#define ND_GENI_UART_WINDOW             0x4000

/*
 * FIFO depth, in words, that every QUP version has. The depth fields of
 * SE_HW_PARAM_0/1 change width with the QUP version, which only the QUP
 * wrapper reports, so the depth isn't read (FreeBSD does the same). Only the
 * RFR watermark uses it: transmission keeps at most one word in the TX FIFO.
 */
#define ND_GENI_UART_FIFO_DEPTH         16
#define ND_GENI_UART_RX_WATERMARK       2
/* Flush a partially filled RX FIFO after 16 idle 10-bit character times. */
#define ND_GENI_UART_RX_STALE_BITS      (16 * 10)

/*
 * Upper bound on waiting for a sequencer, in register reads. There is no
 * clock to count with before the timebase is set up, so the bound is reads
 * of device memory, each well over 10 ns: over 10 ms, and a character takes
 * 87 us at 115200 bit/s. FreeBSD waits up to 100 ms.
 */
#ifndef ND_GENI_POLL_LIMIT
#define ND_GENI_POLL_LIMIT              (1u << 20)
#endif

/* Words discarded at most when init drains what arrived before the kernel. */
#define ND_GENI_UART_DRAIN_LIMIT        256

struct nd_geni_uart {
	uintptr_t base;         /* the engine's registers, mapped */
};

/* Polls until (register & mask) == want; false after ND_GENI_POLL_LIMIT reads. */
static inline bool
nd_geni_poll(const struct nd_geni_uart *u, uint32_t reg, uint32_t mask, uint32_t want)
{
	for (uint32_t n = 0; n < ND_GENI_POLL_LIMIT; n++) {
		if ((ND_GENI_READ(u->base, reg) & mask) == want) {
			return true;
		}
	}
	return false;
}

/*
 * Waits for a transmit command in progress (the kernel's, or one an earlier
 * boot stage left) to finish. If the sequencer makes no progress, cancels
 * the command and, failing that, aborts it.
 */
static inline void
nd_geni_uart_tx_wait(const struct nd_geni_uart *u)
{
	if (nd_geni_poll(u, GENI_STATUS, GENI_STATUS_M_CMD_ACTIVE, 0)) {
		return;
	}
	ND_GENI_WRITE(u->base, GENI_M_CMD_CTRL, GENI_M_CMD_CANCEL);
	if (!nd_geni_poll(u, GENI_M_IRQ_STATUS, GENI_M_CMD_CANCEL_DONE, GENI_M_CMD_CANCEL_DONE)) {
		ND_GENI_WRITE(u->base, GENI_M_CMD_CTRL, GENI_M_CMD_ABORT);
		(void)nd_geni_poll(u, GENI_M_IRQ_STATUS, GENI_M_CMD_ABORT_DONE, GENI_M_CMD_ABORT_DONE);
		ND_GENI_WRITE(u->base, GENI_M_IRQ_CLEAR, GENI_M_CMD_ABORT_DONE);
	}
	ND_GENI_WRITE(u->base, GENI_M_IRQ_CLEAR, GENI_M_CMD_CANCEL_DONE);
}

static inline void
nd_geni_uart_rx_start(const struct nd_geni_uart *u)
{
	ND_GENI_WRITE(u->base, GENI_S_CMD0, (uint32_t)GENI_UART_S_START_READ << GENI_S_OPCODE_SHIFT);
}

static inline void
nd_geni_uart_rx_stop(const struct nd_geni_uart *u)
{
	ND_GENI_WRITE(u->base, GENI_S_CMD_CTRL, GENI_S_CMD_ABORT);
	(void)nd_geni_poll(u, GENI_S_CMD_CTRL, GENI_S_CMD_ABORT, 0);
	ND_GENI_WRITE(u->base, GENI_S_IRQ_CLEAR, GENI_S_CMD_DONE | GENI_S_CMD_ABORT_DONE);
	ND_GENI_WRITE(u->base, GENI_FORCE_DEFAULT_REG, GENI_FORCE_DEFAULT);
}

static inline uint32_t
nd_geni_uart_rx_count(const struct nd_geni_uart *u)
{
	return GENI_RX_FIFO_WC(ND_GENI_READ(u->base, GENI_RX_FIFO_STATUS));
}

/* Whether the engine runs the UART protocol firmware. */
static inline bool
nd_geni_uart_probe(const struct nd_geni_uart *u)
{
	return GENI_FW_REV_PROTOCOL(ND_GENI_READ(u->base, GENI_FW_REVISION_RO)) == GENI_PROTOCOL_UART;
}

/*
 * FreeBSD's geni_uart_init with 8 data bits, one stop bit and no parity,
 * without touching the bit rate: FIFO mode, one character per FIFO word,
 * every interrupt masked (the console is polled), receive started. The one
 * addition is draining what UEFI's packing left in the RX FIFO, which would
 * otherwise read as garbage under the 1x8 packing (FreeBSD's bus-layer
 * receive flush does the same read).
 */
static inline void
nd_geni_uart_init(const struct nd_geni_uart *u)
{
	/* Let output queued by earlier boot stages drain first. */
	nd_geni_uart_tx_wait(u);
	nd_geni_uart_rx_stop(u);

	/* One character per FIFO entry, in both directions. */
	ND_GENI_WRITE(u->base, GENI_TX_PACKING_CFG0, GENI_PACKING_1x8);
	ND_GENI_WRITE(u->base, GENI_TX_PACKING_CFG1, 0);
	ND_GENI_WRITE(u->base, GENI_RX_PACKING_CFG0, GENI_PACKING_1x8);
	ND_GENI_WRITE(u->base, GENI_RX_PACKING_CFG1, 0);
	ND_GENI_WRITE(u->base, GENI_BYTE_GRANULARITY, 0);

	/* Mask and acknowledge every interrupt source. */
	ND_GENI_WRITE(u->base, GENI_GSI_EVENT_EN, 0);
	ND_GENI_WRITE(u->base, GENI_M_IRQ_EN, 0);
	ND_GENI_WRITE(u->base, GENI_S_IRQ_EN, 0);
	ND_GENI_WRITE(u->base, GENI_M_IRQ_CLEAR, 0xffffffffu);
	ND_GENI_WRITE(u->base, GENI_S_IRQ_CLEAR, 0xffffffffu);
	ND_GENI_WRITE(u->base, GENI_DMA_TX_IRQ_CLR, 0xffffffffu);
	ND_GENI_WRITE(u->base, GENI_DMA_RX_IRQ_CLR, 0xffffffffu);

	/* Ungate the engine clocks and return the I/O pins to defaults. */
	ND_GENI_WRITE(u->base, GENI_CGC_CTRL, ND_GENI_READ(u->base, GENI_CGC_CTRL) | GENI_CGC_CTRL_DEFAULT);
	ND_GENI_WRITE(u->base, GENI_DMA_GENERAL_CFG,
	    ND_GENI_READ(u->base, GENI_DMA_GENERAL_CFG) | GENI_DMA_GENERAL_CFG_CGC_ON);
	ND_GENI_WRITE(u->base, GENI_OUTPUT_CTRL, GENI_OUTPUT_CTRL_DEFAULT);
	ND_GENI_WRITE(u->base, GENI_FORCE_DEFAULT_REG, GENI_FORCE_DEFAULT);

	/* FIFO mode. The SE interrupt routing is FreeBSD's; every source stays masked. */
	ND_GENI_WRITE(u->base, GENI_DMA_MODE_EN, ND_GENI_READ(u->base, GENI_DMA_MODE_EN) & ~GENI_DMA_MODE_ENABLE);
	ND_GENI_WRITE(u->base, GENI_SE_IRQ_EN, GENI_SE_IRQ_DMA_RX | GENI_SE_IRQ_DMA_TX | GENI_SE_IRQ_M | GENI_SE_IRQ_S);

	ND_GENI_WRITE(u->base, GENI_RX_WATERMARK, ND_GENI_UART_RX_WATERMARK);
	ND_GENI_WRITE(u->base, GENI_RX_RFR_WATERMARK, ND_GENI_UART_FIFO_DEPTH - 2);
	ND_GENI_WRITE(u->base, GENI_UART_RX_STALE_CNT, ND_GENI_UART_RX_STALE_BITS);

	/* 8N1, CTS ignored (geni_uart_param); the bit rate stays UEFI's. */
	ND_GENI_WRITE(u->base, GENI_UART_TX_TRANS_CFG, GENI_UART_CTS_MASK);
	ND_GENI_WRITE(u->base, GENI_UART_TX_PARITY_CFG, 0);
	ND_GENI_WRITE(u->base, GENI_UART_RX_TRANS_CFG, 0);
	ND_GENI_WRITE(u->base, GENI_UART_RX_PARITY_CFG, 0);
	ND_GENI_WRITE(u->base, GENI_UART_TX_WORD_LEN, 8);
	ND_GENI_WRITE(u->base, GENI_UART_RX_WORD_LEN, 8);
	ND_GENI_WRITE(u->base, GENI_UART_TX_STOP_BIT_LEN, GENI_UART_TX_STOP_BIT_LEN_1);

	/* Discard what arrived before the kernel took the port. */
	for (uint32_t n = 0; n < ND_GENI_UART_DRAIN_LIMIT && nd_geni_uart_rx_count(u) != 0; n++) {
		(void)ND_GENI_READ(u->base, GENI_RX_FIFO);
	}

	nd_geni_uart_rx_start(u);
}

/*
 * pe_serial's transmit_ready: always, since nd_geni_uart_transmit waits for
 * the sequencer itself and gives up on one that is stuck. A readiness test
 * on M_CMD_ACTIVE would spin forever in uart_putc_device on a wedged engine.
 */
static inline unsigned int
nd_geni_uart_transmit_ready(const struct nd_geni_uart *u)
{
	(void)u;
	return 1;
}

/*
 * One character: wait for the previous command, clear its completion, start
 * a one-character command, write the character's FIFO word and wait for the
 * command to finish (FreeBSD's geni_uart_putc).
 */
static inline void
nd_geni_uart_transmit(const struct nd_geni_uart *u, uint8_t c)
{
	nd_geni_uart_tx_wait(u);
	ND_GENI_WRITE(u->base, GENI_M_IRQ_CLEAR, GENI_M_CMD_DONE);
	ND_GENI_WRITE(u->base, GENI_UART_TX_TRANS_LEN, 1);
	ND_GENI_WRITE(u->base, GENI_M_CMD0, (uint32_t)GENI_UART_M_START_TX << GENI_M_OPCODE_SHIFT);
	ND_GENI_WRITE(u->base, GENI_TX_FIFO, c);
	(void)nd_geni_poll(u, GENI_M_IRQ_STATUS, GENI_M_CMD_DONE, GENI_M_CMD_DONE);
}

/*
 * pe_serial's receive_ready: a word is waiting in the RX FIFO. If none is
 * and the read command has ended, it is restarted (FreeBSD's
 * geni_uart_rxready).
 */
static inline unsigned int
nd_geni_uart_receive_ready(const struct nd_geni_uart *u)
{
	if (nd_geni_uart_rx_count(u) != 0) {
		return 1;
	}
	if ((ND_GENI_READ(u->base, GENI_STATUS) & GENI_STATUS_S_CMD_ACTIVE) == 0) {
		nd_geni_uart_rx_start(u);
	}
	return 0;
}

/*
 * pe_serial's receive_data, after receive_ready: one FIFO word, which under
 * the 1x8 packing holds one character in bits 7:0. A last word that
 * RX_FIFO_STATUS marks partial (RX_LAST, RX_LAST_BYTE_VALID) still holds its
 * one character: with one byte per word no word is ever split.
 */
static inline uint8_t
nd_geni_uart_receive(const struct nd_geni_uart *u)
{
	return (uint8_t)(ND_GENI_READ(u->base, GENI_RX_FIFO) & 0xff);
}

#endif /* ND_GENI_UART_H */
