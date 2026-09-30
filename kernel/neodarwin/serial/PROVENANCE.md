<!-- SPDX-License-Identifier: BSD-2-Clause -->
# ndserial provenance

| File | Origin | Licence |
|---|---|---|
| `nd_geni_uart.h` | ported from FreeBSD `sys/dev/uart/uart_dev_qcom_geni.c` and `sys/dev/qcom_geni/qcom_geni_reg.h`, `freebsd-src` commit `124c151cbc` ("uart: Add a driver for the Qualcomm QUPv3 GENI UART", branch `radxa-dragon-q8b`), tested there on the Radxa Dragon Q8B | BSD-2-Clause, Copyright (c) 2026 James Kane; the notice is kept in the file |
| `test/geni_uart_sim_test.c` | NeoDarwin | BSD-2-Clause |

What was taken: the register offsets and fields the driver uses, `geni_uart_init` (with 8N1 from `geni_uart_param`), `geni_uart_tx_wait`, `geni_uart_putc`, `geni_uart_rxready`, `geni_uart_getc`'s FIFO read and `geni_uart_probe`'s protocol check, with the same constants (FIFO depth 16, RX watermark 2, stale count 160 bit times). What changed: `bus_space` accessors became the includer's `ND_GENI_READ`/`ND_GENI_WRITE`; `DELAY()`-timed polls became a bound on register reads, since pe_serial runs before the timebase; `putc`/`getc` are split into pe_serial's `transmit_ready`/`transmit_data`/`receive_ready`/`receive_data`; init also drains the RX FIFO (the same read as FreeBSD's `geni_uart_bus_flush`). FreeBSD's bus layer (interrupt-driven transmit and receive), which pe_serial has no use for, isn't ported.

Linux's `drivers/tty/serial/qcom_geni_serial.c` (GPL-2.0) was not used.

FreeBSD's SPCR access-width tolerance (`24c9d8d2e6`, "uart: Tolerate an invalid access width in ACPI SPCR and DBG2") has its counterpart in neoboot, which records the access size and never checks it (`boot/neoboot/Sources/Portable/ACPI.swift`); no code was taken from it.
