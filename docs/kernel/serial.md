<!-- SPDX-License-Identifier: BSD-2-Clause -->
# Serial consoles

The kernel's serial console is a pexpert driver in `pexpert/arm/pe_serial.c`, chosen by the `compatible` of the node `/defaults serial-device` names. neoboot writes that node from the ACPI SPCR (`dt-abi.md`, `/arm-io/uart0` and "The console"). Every driver is polled: no interrupts, output from `uart_putc`, input from `serial_keyboard_poll` (`osfmk/console/serial_general.c`, every 16 ms) into the console tty. `serial=3` on the command line makes it the console; with a GOP framebuffer the video console mirrors it (patch 0021, bring-up doc §2.1.7).

| SPCR interface type | Hardware | `compatible` | Driver | Since |
|---|---|---|---|---|
| 0x03, 0x0D, 0x0E | PL011, SBSA Generic UART (32-bit and any width) | `"arm,pl011"` | xnu's `pl011_uart_*` | P1-04 |
| 0x11, 0x13 | Qualcomm QUPv3 GENI serial engine in UART mode (the DBG2 "SDM845" types, at 1.8432 and 7.372 MHz); the Radxa Dragon Q8B's console | `"qcom,geni-debug-uart"` | `geni_uart_*`, patch 0032, over `kernel/neodarwin/serial/nd_geni_uart.h` | P1-12 |
| 0x00, 0x01, 0x12 | 16550 family | — | none yet: without a GOP neoboot refuses the board, with one the console is the framebuffer alone | P1-12, next |

## The GENI UART

The Q8B's SPCR names interface type 0x13 at 0x884000, serial engine 17 of a QUP wrapper (the DSDT's `UARD`, `QCOM0616`, GSIV 615; the SPCR's IRQ field is wrong, and the console doesn't use one). The header's pads are 1.8 V.

### Where the code is

The register sequences are a NeoDarwin header, `kernel/neodarwin/serial/nd_geni_uart.h`, ported from FreeBSD's `uart_dev_qcom_geni.c` (commit 124c151cbc, which runs on the board; `PROVENANCE.md`). The kernel build overlays it at `pexpert/arm/ndserial/`, and patch 0032 includes it into `pe_serial.c` with ~80 lines of glue: the five `serial_functions` wrappers, `geni_uart_setup` (the `reg` lookup and `ml_io_map`, as `pl011_uart_setup` does), and a line in `driver_setup_functions`. `SBSA.h` defines `QCOM_GENI_UART`.

The glue has to be in `pe_serial.c`: `register_serial_functions`, `struct pe_serial_functions` and the compatible table are `static` there. The sequences don't: as a header of `static inline` functions over two accessor macros, `ND_GENI_READ` and `ND_GENI_WRITE`, they build unchanged against volatile device memory in the kernel and against a register model on the host (below), and the patch stays a hook (`docs/repository.md` §3). A separate `.c` in pexpert would also need `pexpert/conf/files` patched, for no gain.

### What the driver trusts to UEFI

UEFI's console driver leaves the SPCR engine clocked, loaded with the UART protocol firmware, muxed to its pins and running at the bit rate it chose (115200 on the Q8B: the SPCR's baud byte is 7). The kernel keeps all of that:
- **The clock and the bit rate.** The bit rate divides the serial engine's clock, which belongs to the global clock controller; nothing programs the clock or the divider registers. The model checks that no such register is written.
- **Which engine.** An engine that isn't clocked faults when touched, and ACPI doesn't say which engines UEFI left running. Only the SPCR console is ever named in the tree.
- **The protocol firmware.** `geni_uart_setup` reads `GENI_FW_REVISION_RO` and registers no console unless the protocol is the UART's (2): a wrong address gives no serial console rather than a hang.
- **The FIFO depth.** `SE_HW_PARAM_0/1` hold it in fields whose width depends on the QUP version, which only the QUP wrapper reports; the driver uses 16 words, which every version has. Only the RFR watermark uses it, since transmission keeps at most one word in the TX FIFO.

What it does program, as FreeBSD's `geni_uart_init` does: FIFO mode (DMA off), one character per FIFO word in each direction (packing 1x8), every interrupt masked and acknowledged, the engine's clock gating and output defaults, the RX watermarks and stale count, 8 data bits, one stop bit, no parity and CTS ignored.

### Transmit

`uart_putc_device` calls `transmit_ready`, then `transmit_data` for each character. `transmit_ready` always says yes; `transmit_data` is FreeBSD's `geni_uart_putc`:
1. wait for `GENI_STATUS.M_CMD_ACTIVE` to clear (a previous character, or UEFI's last output at init). If the sequencer makes no progress, `M_CMD_CTRL` CANCEL, and if that doesn't finish (`M_IRQ_STATUS.CANCEL_DONE`), ABORT;
2. clear `M_IRQ_STATUS.CMD_DONE`;
3. `UART_TX_TRANS_LEN` = 1, then `M_CMD0` = START_TX (opcode 1 at bit 27);
4. the character's FIFO word into `TX_FIFO`;
5. wait for `CMD_DONE`.

Every wait is bounded, by 2^20 register reads rather than time: pe_serial runs before the timebase exists, and a device-memory read takes well over 10 ns, so the bound is tens of milliseconds, against 87 µs per character at 115200. A dead engine therefore slows the console, but never hangs it or a panic. A readiness test on `M_CMD_ACTIVE` in `transmit_ready` would have spun forever in `uart_putc_device` instead.

One command per character costs a little over the line rate; the kernel's log is bounded by the line anyway.

### Receive

A START_READ command on the secondary sequencer (`S_CMD0`) is left running from init. `receive_ready` reads the word count of `RX_FIFO_STATUS`; if it is 0 and the read command has ended (`GENI_STATUS.S_CMD_ACTIVE` clear), it starts it again. `receive_data` reads one `RX_FIFO` word, whose bits 7:0 are the character. With one character per word, `RX_FIFO_STATUS`'s last-word fields (`RX_LAST`, `RX_LAST_BYTE_VALID`) never mark a word split; UEFI may leave the FIFO packed several characters to a word, so init drains the FIFO (up to 256 words) after stopping the read command and before changing the packing.

### neoboot

neoboot writes `/arm-io/uart0` `"qcom,geni-debug-uart"` with the engine's 16 KiB window and `serial-device`, with or without a GOP framebuffer (then both are consoles). It ignores the SPCR's access size, which the Q8B's firmware fills with 0x20 (bits) instead of an encoded size. Its own output goes through UEFI's console until `ExitBootServices`, and it never writes to the GENI UART itself: its one post-`ExitBootServices` line isn't worth a transmit path only the board can test, and the kernel driver's init is then the first thing to touch the engine after UEFI. `uart=off` in `boot.cfg` still leaves the UART out (the framebuffer alone).

### Tests

- `//kernel/neodarwin/serial:geni_uart_sim_test` builds the header on the host against a model of a serial engine: `M_CMD0` START_TX with `UART_TX_TRANS_LEN`, a TX FIFO draining one word per character time under the programmed packing, `CMD_DONE`, CANCEL and ABORT (and an engine that ignores CANCEL); `S_CMD0` START_READ, an RX FIFO filled under the programmed packing, with `RX_LAST` and `RX_LAST_BYTE_VALID` on a partial last word; `GENI_STATUS`; `FW_REVISION_RO`; `SE_HW_PARAM_0/1`. It flags a TX word written with no command or beyond the command's length, a TX FIFO overrun, a command started while one runs, a read of an empty RX FIFO, and a write to any register outside the set FreeBSD's driver programs. The tests: probe and init (1x8 packing, interrupts masked, 8N1, read command running); a string and all 256 byte values out in order, one command of length 1 each, at most one word in the FIFO; UEFI's last command finished before the kernel's first; a stuck command cancelled, one that ignores CANCEL aborted, and a dead engine that returns within the bound; nothing read from an idle line; typed characters, a burst that fills the FIFO, and input interleaved with output, all in order; UEFI's packed leftovers (two words, the last with two valid bytes) drained rather than read as garbage; and a stopped read command restarted.
- `//tools/dtdump`: the Q8B's tables and `qemu-virt-spcr-geni` (QEMU's with the Q8B's SPCR) give a tree with the GENI node, with and without `--gop`; `--uart-off` leaves it out; a tree whose UART compatible no driver matches fails the check.
- QEMU has no GENI model, and a kernel told there is one on `virt` would write to whatever lies at 0x884000; so no QEMU boot covers the driver. The existing boots cover the pe_serial path it shares with the PL011.

### On the board

What needs a 1.8 V USB serial adapter (a 3.3 V one can damage the pads): TX, RX and ground on the Q8B's debug UART header, 115200 8N1.
1. **UEFI.** Power on: firmware output appears on the adapter. This confirms the wiring, the bit rate and that engine 17 is the SPCR's.
2. **neoboot.** Its lines before `ExitBootServices` come through UEFI's console, on HDMI and, if UEFI's ConOut includes the serial port, on the adapter. It says `neoboot: ACPI: ... UART Qualcomm GENI at 0x884000, PSCI SMC`, then nothing on serial after `ExitBootServices`.
3. **Transmit.** The kernel's banner and log appear on the adapter from `serial_init` on, and the same text on HDMI. If HDMI shows the log and serial stays silent, the protocol check declined the engine (no console registered) or the engine isn't transmitting; if characters are garbled, the packing or the bit rate is off. If the kernel stops at `serial_init` with nothing on HDMI after the video console's replay, the engine faulted (not clocked): boot with `uart=off` to get the framebuffer alone and report it.
4. **Receive.** At `login:`, type `root`: the characters echo on serial and HDMI and the shell starts. Then `echo geni-$((6*7))` prints `geni-42` on both. That is `serial_keyboard_poll` → `uart_getc` → `receive_ready`/`receive_data`, and the read command restart.
5. **Speed.** A `cat` of a large file on the serial console runs at about the line rate (~11 KiB/s at 115200), not slower.

Until then the Q8B's exit for P1-12 ("the kernel console runs on the Q8B's GENI UART") is open.
