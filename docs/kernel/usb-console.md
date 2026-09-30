<!-- SPDX-License-Identifier: BSD-2-Clause -->
# The console USB keyboard: a minimal xHCI driver (P1-18)

**P1-18.** The Radxa Dragon Q8B shows its console on HDMI (the framebuffer console, `arm64-sbsa-bringup.md` §2.1.7), but its only other console is a GENI UART on 1.8 V pads (`serial.md`). Without an adapter for those pads, nothing can be typed at `login:`. P1-18 adds a USB keyboard as console input. `NeoDarwinXHCI` is a small xHCI host controller driver built into the kernel. It attaches over PCI and as an ACPI platform device. It drives HID boot-protocol keyboards, and the USB 2 hubs in front of them. Each key becomes console input the way a serial character does: through `cons_cinput` into the console tty.

It is a **bring-up aid**, kept small and easy to replace. P3-07's USB stack (an xHCI driver and USB core in `NDUSBFamily`, HID and the rest as DriverKit dexts; `docs/architecture/drivers.md`) replaces it. When P3-07 lands, this driver's personalities and patch 0033 go.

## Pieces

| Where | What |
|---|---|
| `kernel/neodarwin/usb/NeoDarwinXHCI.{h,cpp}` | the driver, an `IOService` on an `IOPCIDevice` or an `IOACPIPlatformDevice` |
| `kernel/neodarwin/usb/nd_xhci.h` | xHCI 1.2 registers, TRBs, contexts, and the calculations the driver makes (intervals, route strings, DCIs, PORTSC and USBLEGCTLSTS values) |
| `kernel/neodarwin/usb/nd_usb_desc.h` | USB chapter 9 and chapter 11 descriptors and requests, and the configuration parser that picks the interface to drive |
| `kernel/neodarwin/usb/nd_hid_kbd.h` | the boot keyboard's report-to-bytes state machine (US layout) |
| `kernel/neodarwin/usb/nd_dwc3.h` | the DWC3 role-switch set-up, ported from FreeBSD (`PROVENANCE.md`) |
| `kernel/neodarwin/usb/test/usb_logic_test.c` | host test of the four headers (`//kernel/neodarwin/usb:usb_logic_test`) |
| `kernel/patches/0033-iokit-build-console-usb-keyboard.patch` | the build-list line, search paths and the two personalities |
| `tools/efi/qemu_efi_test.sh --sendkey-after`, `--sendkey-on-screen` | typing on QEMU's USB keyboard through its monitor |

The overlay puts the directory at `iokit/ndusb`. The driver includes the storage drivers' `NeoDarwinStorageDMA.h` (the DMA policy: `dma-coherent`, `dma-address-bits`, allocation and cache maintenance), and the PCI and ACPI headers.

**Language (T4).** `NeoDarwinXHCI` is C++ because IOKit classes are; that is the expressibility ground, and it applies to every kext class. The headers are C, and `kext_swift` is not proven (P0-10), so kernel logic outside an IOKit class cannot yet be Embedded Swift. Being C lets them build unchanged in the kernel and in the host test (portability), as `nd_geni_uart.h` does. `nd_dwc3.h` is FreeBSD code, which stays C. Each file carries its `NeoDarwin-Language:` line, and `//kernel/neodarwin/usb:usb_lang_audit` checks it.

## Attach

| Provider | Personality | Notes |
|---|---|---|
| `IOPCIDevice` | `IOPCIClassMatch` `0x0c033000&0xffffff00` (class 0C0330, xHCI) | QEMU's `qemu-xhci` (1b36:000d); any PCIe xHCI (a Renesas card, a Thunderbolt dock) |
| `IOACPIPlatformDevice` | `IONameMatch` `PNP0D10`, `PNP0D15` | xHCI with and without debug capability: SbsaQemu's `\_SB.USB0`; the Q8B's multiport controller `\_SB.USB2` (`QCOM06A1`, `_CID` `PNP0D15`) |
| `IOACPIPlatformDevice` | `IONameMatch` `PNP0CA1` (USB Role Switch) | the Q8B's USB-C controllers `\_SB.URS0` (`QCOM068B`) and `URS1` (`QCOM068C`). `probe` declines one without a host-role child |

These are FreeBSD's `generic_xhci_acpi` IDs, including its role-switch support (commits ac16521596 and cbbcf73a5d). A USB Role Switch device holds the controller's registers in its `_CRS`. Its host-role child, the `_ADR` 0 device (`URS0.USB0`), holds the interrupts. The child has no `_HID`, so the ACPI platform publishes it in IOACPIPlane only (`acpi.md`). The driver walks the role switch's IOACPIPlane children for `_ADR` 0 with an `IOInterruptSpecifiers`, and registers the child's interrupt 0. The ACPI platform publishes present devices only, which covers FreeBSD's `_STA` check.

**DWC3** (`nd_dwc3.h`, only on role switches, as FreeBSD does since cbbcf73a5d). If `GSNPSID` (0xc120) names a Synopsys DWC_usb3 (0x5533), usb31 (0x3331) or usb32 (0x3332) core, then:
- `GCTL.PRTCAPDIR` (0xc110 bits 13:12) must be host (1), or the controller is refused. The driver uses the role the firmware left and never switches it.
- `GRXTHRCFG` (0xc10c) has its receive-threshold enable cleared: bit 29 on usb3, bit 26 on usb31/32. The Q8B's UEFI leaves it on (0x04f30000), which holds SuperSpeed reads to a third; a keyboard doesn't care, but the core's reset default has it off.

The log names both the core and the value.

## The controller

Written from the xHCI 1.2 specification, in the order the specification's §4.2 gives:

1. **Interrupts first on PCI.** The driver asks for MSI-X: `configureInterrupts(kIOInterruptTypePCIMessagedX, 1, 1)`, before anything resolves the device's interrupts (`gic-its.md`). Without MSI-X it takes MSI if IOPCIFamily lists one, else INTx. On ACPI it takes the GIC interrupt of `_CRS`, or of the host-role child.
2. **DMA policy.** On PCI, `dma-coherent` and `dma-address-bits` of the nub (`pci.md`). On ACPI, `_CCA`, with a missing `_CCA` meaning not coherent, and 64 bits. A controller without `HCCPARAMS1.AC64` gets memory below 4 GiB. The boot-arg `nd_xhci_dma_bits=N` lowers the limit, and `nd_xhci_coherent=0` forces the non-coherent path.
3. **Registers.** BAR 0, or the nub's first memory range. The driver reads `CAPLENGTH`, `HCIVERSION`, `HCSPARAMS1/2` (slots, ports, scratchpads), `HCCPARAMS1` (AC64, CSZ, PPC, xECP), `DBOFF` and `RTSOFF`, and refuses offsets that fall outside the window.
4. **BIOS handoff** (§4.22.1, USB Legacy Support). If the firmware owns the controller, the driver sets OS Owned and waits up to a second for BIOS Owned to clear. If it doesn't clear, the driver clears it itself and logs that. Then the SMI enables in `USBLEGCTLSTS` go off and its RW1C events are acknowledged. UEFI's xHCI driver normally releases the controller at `ExitBootServices`; the Q8B's firmware lists a Legacy capability (FreeBSD: `xECP capabilities <LEGACY,PROTO,PROTO,PROTO>`), QEMU's lists none.
5. **Supported Protocol** capabilities (§7.2) say which root ports are USB 2 and which USB 3.
6. **Halt and reset.** `R/S` 0, then `HCH` within 100 ms; `HCRST`; `HCRST` and `CNR` clear within a second.
7. **Memory.** One page the CPU writes holds the DCBAA (`MaxSlotsEn` + 1 entries, at most 32 slots), the command ring (64 TRBs with a Link TRB), the one-entry ERST and the scratchpad array. The event ring (256 TRBs) has a page of its own, since the controller writes it. The scratchpad buffers are `PAGESIZE`-aligned, and `DCBAA[0]` points at their array. Everything is physically contiguous, below the address limit, zeroed, and cleaned if not coherent.
8. **Run.** `CONFIG.MaxSlotsEn`, `DCBAAP`, `CRCR` (RCS 1), then interrupter 0: `ERSTSZ` 1, `ERDP`, `ERSTBA`, `IMOD` 40 µs, `IMAN.IE`. Then `USBCMD.INTE | R/S`, and `HCH` must clear. Ports are powered where `PPC` says software switches them.

Every root port is then looked at once. After that, Port Status Change events say which port to look at.

**Contexts.** 32 bytes each, or 64 with `CSZ`; the driver indexes them by `contextBytes`. A device's DMA page holds, in order: its output context (the controller writes it), its input context, EP0's ring (32 TRBs), its interrupt ring (16 TRBs), a 1 KiB control buffer and a 1 KiB report buffer. Each piece has cache lines of its own, so on a non-coherent controller cleaning what the CPU wrote never overwrites what the device wrote.

**Rings.** Producer rings end in a Link TRB with Toggle Cycle. A TD never straddles the link: the ring's tail is filled with No-op TRBs first. A control transfer's Setup TRB gets its cycle bit last, after the Data and Status TRBs are in place. Doorbells follow a `DSB SY`, which comes after the ring has been cleaned (non-coherent) or ordered (`DMB OSHST`).

**Events.** The event ring is read up to the first TRB whose cycle bit is not the consumer's. Each TRB is invalidated before it's read (non-coherent), with a load barrier after the cycle bit. Then `ERDP` is written with `EHB` once per batch. Command completions are matched by TRB address; one command is in flight at a time. Transfer events are matched by slot and endpoint, then by TRB address. Port status changes mark the port and schedule a scan. Host Controller events are logged.

**Interrupts.** MSI-X and MSI use an `IOInterruptEventSource`: the controller clears `IMAN.IP` itself when it writes the message. INTx and ACPI SPIs are level-triggered, and INTx may be shared. For them, an `IOFilterInterruptEventSource` whose filter, in primary interrupt context, claims the interrupt only if interrupter 0's `IMAN.IP` is set. It then clears `IP` (which deasserts the line) and `USBSTS.EINT`. The work loop does the rest.

**Polling fallback.** A timer on the work loop looks at the event ring every second. An event that was already pending at the previous look has waited a second without an interrupt. The driver then logs `an event waited a second without an interrupt` and polls every 10 ms from then on. `nd_xhci_poll=1` polls from the start, with no interrupt registered. On the Q8B this covers an SPI that never arrives (a wrong trigger type, or a GIC route).

## Enumeration

Enumeration runs on the work loop (the scan timer). It waits for its commands and control transfers by processing the event ring itself, so keyboard reports and port changes that arrive meanwhile are handled as usual.

1. **Root port.** Change bits are acknowledged; a device that left (or reconnected) is detached. On a USB 2 port, a new connection is reset (`PR`, then `PRC` with `PED` within 500 ms, then 10 ms `TRSTRCY`). USB 3 ports train by themselves, and must show `PED`. The speed is the port's speed ID; the driver uses the default IDs.
2. **Enable Slot** and **Address Device.** The input context has the slot context (route string, speed, root port, TT hub slot and port, one entry) and EP0 (control, CErr 3, a packet size of 8/8/64/512 for low/full/high/SuperSpeed). The controller sends `SET_ADDRESS`, then the driver waits 2 ms.
3. **`GET_DESCRIPTOR(Device, 8)`**, and **Evaluate Context** if `bMaxPacketSize0` differs from the guess; then the whole device descriptor, the product string (language 0x0409), and the configuration descriptor (9 bytes, then `wTotalLength` up to 1 KiB).
4. **The interface to drive** (`nd_usb_parse_config`). The first HID boot keyboard (class 3, subclass 1, protocol 1) with an interrupt IN endpoint wins. Failing that, a hub (class 9). Anything else is logged (`no driver (interface class 8/6/80)`) and left addressed. Descriptor lengths are checked against the buffer, and a malformed descriptor stops the parse.
5. **`SET_CONFIGURATION`** and **Configure Endpoint**. The endpoint context has the interval (§6.2.3.6: from frames for low and full speed, 2^(bInterval−1) microframes otherwise), the packet size, Max Burst from a SuperSpeed companion, Max ESIT Payload and the average TRB length. For a hub, the same command also writes the slot context's hub fields.

A control transfer that stalls halts EP0. The driver issues **Reset Endpoint**, then **Set TR Dequeue Pointer** to the ring's enqueue position, past the failed TD. After a timeout it uses **Stop Endpoint** instead of Reset Endpoint. An interrupt endpoint that fails is recovered the same way from the scan, and given up on after 16 failures in a row. A disconnect (a root port's `CCS`, or a hub port's connection bit) sends **Disable Slot** and frees the device, children of a hub first.

### Hubs: why they are in

The task was to skip hubs unless the Q8B needs them. It does. FreeBSD's boot on the board (`q8b-dmesg.txt` in the FreeBSD work's scratchpad, 2026-09-28) shows the user's keyboard, a full-speed SEM USB Keyboard (1a2c:506f, a boot keyboard plus a consumer-control interface), **behind a high-speed 1a86:8091 USB 2.0 hub** (WCH, 4 ports, multi-TT) on port 3 of the multiport controller `\_SB.USB2` at 0xa400000. That controller has six root ports (4 USB 2 and 2 USB 3). The DSDT's `RHUB` lists `MP0`–`MP3` as user-visible Type-A ports (`_UPC` types 3, 3, 0, 0), so the hub is probably an external one (or the keyboard's own) rather than on the board. Either way, the keyboard the board is used with sits behind a hub. So the driver has minimal USB 2 hub support:

- the hub descriptor (`GET_DESCRIPTOR` 0x29); the slot context's `Hub`, `Number of Ports` and, for a high-speed hub, `TTT` (think time from `wHubCharacteristics` bits 6:5). `MTT` stays 0: a multi-TT hub runs single-TT until its alternate setting 1 is selected, which the driver never does;
- `SET_FEATURE(PORT_POWER)` on every port, then `bPwrOn2PwrGood` × 2 ms (at least 100 ms);
- the status change endpoint: its bitmap marks ports to scan, and it is queued again after each scan;
- per changed port: `GET_STATUS`, every change bit cleared, a disconnect detached, a connection reset (`SET_FEATURE(PORT_RESET)`, `C_PORT_RESET` within 500 ms, enabled), the speed from the low- and high-speed bits;
- the child's route string (one nibble per tier below the root port), root port, and **transaction translator**. A low- or full-speed device behind a high-speed hub uses that hub's slot and port; behind a full-speed hub, it inherits the hub's own TT. The Q8B's keyboard needs exactly this: full speed behind a high-speed hub.

Not supported: SuperSpeed hubs (logged. A USB 3 hub's USB 2 half, on the companion USB 2 port, carries low- and full-speed devices, so a keyboard behind a USB 3 hub still works), hubs more than four deep, per-port power switching and over-current handling beyond acknowledging the change, and hub removal while its children are in use beyond detaching them.

## The keyboard

`startKeyboard`: Configure Endpoint, then `SET_PROTOCOL(0)` (boot) and `SET_IDLE(0)` (reports only on change; a stall is ignored, as some keyboards don't implement it). One Normal TRB is always queued on the interrupt endpoint, with IOC and ISP. Each completion is invalidated, read, and requeued.

`nd_hid_kbd.h` turns each 8-byte report into terminal bytes: each key in the report that wasn't in the last one is pressed, in report order.

| Keys | Bytes |
|---|---|
| letters, digits, punctuation, space | US layout, Shift, Caps Lock (letters only; Shift inverts it) |
| Enter, keypad Enter | CR (the tty's `ICRNL` makes it a newline) |
| Backspace | DEL 0x7f (the tty's `VERASE`, zsh's `backward-delete-char`) |
| Tab, Esc | 0x09, 0x1b |
| arrows, Home, End, Insert, Delete, Page Up/Down | `ESC [ A/B/C/D`, `ESC [ H`, `ESC [ F`, `ESC [ 2~`, `ESC [ 3~`, `ESC [ 5~`, `ESC [ 6~`: what zsh's line editor binds by default |
| F1–F12 | `ESC O P`–`S`, `ESC [ 15~`–`24~` |
| Ctrl + key | the control character (`^C` 0x03, `^[` 0x1b, `^@`, `^^`, `^_`, `^?`) |
| Alt + key | ESC, then the key (meta) |
| keypad | digits and operators, as with Num Lock on |

A report whose keys are all ErrorRollOver (too many keys down) is ignored.

**Typematic repeat.** The last key pressed that is still held repeats after 500 ms, 30 times a second, with the modifiers held at the time. Pressing another key restarts the delay, and releasing it stops the repeat. The timer is an `IOTimerEventSource` on the work loop.

**To the console.** `cons_cinput(ch)` (`bsd/dev/arm/km.c`) passes each byte to the console tty's line discipline. That is the serial keyboard's path (`serial_keyboard_poll`), so the tty echoes to every console, serial and framebuffer alike. It runs on the work loop, a kernel thread, because `cons_cinput` takes the tty's lock. The interrupt filter never calls it. Before BSD has created the console tty (`kminit`), input is dropped. No LEDs: Caps Lock toggles, but its LED stays off.

## The boot log

QEMU `virt`, `-device qemu-xhci -device usb-kbd` (`sbsa_usb_kbd_boot_test`). QEMU numbers its four USB 3 root ports first, so its keyboard, a high-speed device, is on port 5:

```
NeoDarwinPCIHostBridge: 0000:00:02.0 1b36:000d class 0c0330 bar0 mem 0x8000004000+0x4000 INTA gsiv 37 msi-x 16
NeoDarwinPCIMSI: 0000:00:02.0: MSI-X 1 of 16 vectors -> LPI 8192, DeviceID 0x10 on ITS 0
NeoDarwinXHCI: 00:02.0: xHCI 1.0, 1b36:000d; 64 slots (32 enabled), 8 ports (USB 3.0: 1-4, USB 2.0: 5-8), 32-byte contexts, 0 scratchpad buffers
NeoDarwinXHCI: 00:02.0: MSI-X vector 0 (LPI 8192); DMA coherent, 64 address bits
NeoDarwinXHCI: 00:02.0: port 5: high-speed device 0627:0001 "QEMU USB Keyboard", USB 2.00, on slot 1
NeoDarwinXHCI: 00:02.0: port 5: slot 1: HID boot keyboard (interface 0, endpoint 1 IN, 8 bytes every 8000 us); SET_PROTOCOL boot, SET_IDLE 0: console input
login: NeoDarwinXHCI: 00:02.0: port 5: slot 1: first keyboard report (8 bytes, by interrupt)
NeoDarwinXHCI: 00:02.0: first event by interrupt (MSI-X vector 0, LPI 8192)
```

Behind QEMU's hub, on INTx with the non-coherent path and 32-bit DMA (`sbsa_usb_kbd_hub_intx_boot_test`):

```
NeoDarwinXHCI: 00:02.0: INTx (GSIV 37); DMA not coherent, 32 address bits
NeoDarwinXHCI: 00:02.0: port 6: full-speed device 0409:55aa "QEMU USB Hub", USB 1.10, on slot 1
NeoDarwinXHCI: 00:02.0: port 6: slot 1: hub, 8 ports; ports powered
NeoDarwinXHCI: 00:02.0: port 6.3: full-speed device 0627:0001 "QEMU USB Keyboard", USB 2.00, on slot 2
NeoDarwinXHCI: 00:02.0: port 6.3: slot 2: HID boot keyboard (interface 0, endpoint 1 IN, 8 bytes every 8000 us); SET_PROTOCOL boot, SET_IDLE 0: console input
NeoDarwinXHCI: 00:02.0: first event by interrupt (INTx, GSIV 37)
```

On `sbsa-ref` (`sbsa_ref_usb_kbd_boot_test`):

```
NeoDarwinXHCI: \_SB.USB0: xHCI 1.0, PNP0D10 at 0x60110000; 64 slots (32 enabled), 8 ports (USB 3.0: 1-4, USB 2.0: 5-8), 32-byte contexts, 0 scratchpad buffers
NeoDarwinXHCI: \_SB.USB0: GSIV 43 (level); DMA coherent, 64 address bits
NeoDarwinXHCI: \_SB.USB0: port 5: high-speed device 0627:0001 "QEMU USB Keyboard", USB 2.00, on slot 1
NeoDarwinXHCI: \_SB.USB0: first event by interrupt (GSIV 43)
```

## Tests

`qemu_efi_test.sh --sendkey-after LINE KEYS` types KEYS on QEMU's keyboard once LINE appears on serial. It uses the monitor's `sendkey`, one key at a time (held 20 ms, 100 ms apart). `\n`, `\b`, `\t` and `\e` stand for Enter, Backspace, Tab and Esc, and `{name}` for any QEMU key name (`{left}`, `{up}`, `{ctrl-c}`). `--sendkey-on-screen TEXT KEYS` does the same once the last line of the screen ends with TEXT, for a console that is on the framebuffer only. Nothing is typed on serial in these tests.

| Target | Machine | Asserts |
|---|---|---|
| `//kernel/neodarwin/usb:usb_logic_test` | host | the keyboard's bytes for letters, Shift, Caps Lock, digits, punctuation, Enter, Backspace, Tab, Esc, arrows, Delete, F5, Ctrl, Ctrl-Space, Alt, keypad; rollover, ErrorRollOver, the repeat key; QEMU's keyboard, the Q8B's composite keyboard, a hub and a storage stick as configuration descriptors, and malformed ones; device, hub and string descriptors; intervals, route strings, DCIs, EP0 sizes, PORTSC and USBLEGCTLSTS values, the scratchpad count; the DWC3 set-up on a register model (not DWC3, DWC_usb31 with the Q8B's 0x04f30000, DWC_usb3, device mode refused) |
| `//kernel:sbsa_usb_kbd_boot_test` | `virt`, qemu-xhci (MSI-X) + usb-kbd, the zsh session | the controller and keyboard lines, the first event by MSI-X; log in and `echo usb-$((6*7))` on the USB keyboard; zsh's line editor: Backspace (`bs-ABC`), Left (`ARROWS-XYZ`), Up to recall history (`hist-8`) |
| `//kernel:sbsa_usb_kbd_hub_intx_boot_test` | `virt`, INTx, non-coherent path, 32-bit DMA; the keyboard on port 3 of QEMU's usb-hub on root port 6 | the hub (8 ports, powered), the keyboard at `port 6.3`, the first event by INTx; log in, `echo hub-$((6*7)); uname -sm` |
| `//kernel:sbsa_usb_kbd_poll_boot_test` | `virt`, `nd_xhci_poll=1` | polling, the first event and report by polling; log in and a command |
| `//kernel:sbsa_usb_kbd_fb_only_boot_test` | `virt`, `uart=off` + ramfb, qemu-xhci with MSI (`msi=on,msix=off`: QEMU's default offers MSI-X alone) | the Q8B's situation: log in on the USB keyboard at the framebuffer's `login:`; `fb-usb-42` and `Darwin arm64` on the screen; the kernel's banner never on serial |
| `//kernel:sbsa_ref_usb_kbd_boot_test` | `sbsa-ref`, its platform xHCI `\_SB.USB0` (PNP0D10, GSIV 43, `_CCA` 1) + usb-kbd | the ACPI attach, the first event by the SPI; log in, a command, Backspace |

## What the work found

| Finding | Fix |
|---|---|
| QEMU's xHCI numbers its USB 3 root ports first (1–4) and its USB 2 ports after (5–8); QEMU's USB port 2 is xHCI port 6, and `usb-kbd` is a high-speed device there | nothing in the driver (the Supported Protocol capabilities say which is which); the tests expect `port 5` and `port 6.3` |
| `qemu-xhci` offers MSI-X alone; with `msix=off` the driver fell back to INTx, not MSI | the MSI test uses `msi=on,msix=off` |
| Keys typed after moving the cursor are inserted at the cursor, so an arrow test that types more after the arrows edits the middle of the line | the arrow test ends its line at the edit: `echo ARROWS-XZ`, Left, `Y` gives `ARROWS-XYZ` |
| The Q8B's keyboard is behind a high-speed hub (FreeBSD's boot on the board), so "hubs later" would have left the board without input | minimal USB 2 hub support with the TT fields, tested behind QEMU's full-speed hub |
| An ACPI USB Role Switch's interrupt is on an `_ADR`-only child, which is in IOACPIPlane but not the service plane | the driver walks the role switch's IOACPIPlane children and registers the child's interrupt 0 |
| MSI and MSI-X clear `IMAN.IP` by themselves, so a filter that claims only when `IP` is set would drop them | MSI and MSI-X use a plain interrupt event source; only INTx and ACPI SPIs (level, possibly shared) use the `IP` filter |

## On the Radxa Dragon Q8B

What the board has (DSDT and FreeBSD's boot):

| Device | IDs | Registers | Interrupt (first `_CRS` entry) | `_CCA` | NeoDarwin |
|---|---|---|---|---|---|
| `\_SB.USB2`, multiport, the USB-A ports | `QCOM06A1` (`PNP0D15`) | 0xa400000 + 1 MiB | GSIV 165 | 0 | attached as xHCI; 6 root ports; **the keyboard's controller** |
| `\_SB.URS0`, USB-C | `QCOM068B` (`PNP0CA1`) | 0xa600000 + 1 MiB | host child `USB0`: GSIV 835 | 0 | role switch, DWC3 checks; IORT: behind the SMMUv3, 36 bits |
| `\_SB.URS1`, USB-C | `QCOM068C` (`PNP0CA1`) | 0xa800000 + 1 MiB | host child `USB1` | 0 | the same |

All three are non-coherent (`_CCA` 0): the driver cleans and invalidates everything, the path `sbsa_usb_kbd_hub_intx_boot_test` forces on QEMU. The Q8B's DRAM is below 36 bits, so the IORT limit of the USB-C controllers never binds; `USB2` has no IORT node (its DMA isn't translated). The SMMU is assumed to pass the USB-C controllers' streams through, as for NVMe (`storage.md`).

**What to do on the board.** Boot NeoDarwin as usual, with HDMI connected and the keyboard where it was under FreeBSD (a USB-A port, through its hub or directly). Nothing needs to be set: `boot.cfg` stays as it is.

1. At `login:` on HDMI, type `root` and Enter. The shell's prompt appears. Type `echo usb-$((6*7))`: `usb-42`. Try Backspace, the arrows and Up (history) in zsh. That is the exit.
2. If nothing echoes, read the kernel log on HDMI (scroll back is not possible; photograph it). For the multiport controller, look for these lines:
   - `NeoDarwinXHCI: \_SB.USB2: xHCI 1.x, QCOM06A1 at 0xa400000; ... 6 ports (...)`, which names the USB 2 and USB 3 ports (FreeBSD's port 3 for the hub is an xHCI port number, so `port 3` below assumes the hub is where it was), and whether the firmware released the controller (`firmware released it in N ms`, or `did not release it in 1 s: taken`);
   - `GSIV 165 (level); DMA not coherent, 64 address bits`;
   - `port 3: high-speed device 1a86:8091 ...` then `slot N: hub, 4 ports, high-speed, single TT; ports powered`, then `port 3.1: full-speed device 1a2c:506f "SEM USB Keyboard" ... (through the TT of a high-speed hub)` and `HID boot keyboard ... console input`;
   - `first event by interrupt (GSIV 165)` and, at the first key, `first keyboard report (8 bytes, by interrupt)`.
3. If `an event waited a second without an interrupt ...; polling every 10 ms` appears, the SPI isn't delivered, but typing works by polling; note it. `nd_xhci_poll=1` in `boot.cfg` forces polling from the start, if interrupts misbehave in some other way.
4. If the keyboard's line is missing but the hub's is there, move the keyboard to a USB-A port directly. If a line reads `Address Device failed` or `request ... timed out`, note the path and the completion code.
5. The USB-C controllers: `\_SB.URS0: ... DWC3 core, GSNPSID 0x3331..., host mode, GRXTHRCFG 0x04f30000 -> 0x00f30000` (or `not in host mode; not used`). A keyboard on a USB-C port (through an adapter) should work the same way.
6. `nd_xhci=0` in `boot.cfg` keeps the driver off entirely, if it ever gets in the way of booting.

Until then P1-18 stays `doing`: its exit names the board.

## Limits

- Only HID boot keyboards and USB 2 hubs are driven; mice, storage and the rest are addressed and left alone. No report protocol, no LEDs, no second layout (the keymap is US).
- One interrupter, one event ring segment, 32 device slots, 15 ports per hub, four hub tiers.
- No SuperSpeed hubs, no USB 3 warm reset or link recovery, no suspend, resume or power management, no isochronous or bulk transfers, no streams.
- No Port Speed ID tables: the default speed IDs are assumed (every known controller uses them for USB 2 and USB 3.0).
- No `_DEP` ordering: on the Q8B the controllers depend on the PEP device, which nothing starts (`acpi.md`); UEFI leaves them clocked and powered.
- Command and control-transfer timeouts are 5 s; a controller that stops processing commands is not reset.
- The keyboard feeds the console tty only. It is not an `IOHIDDevice`, and no HID event system exists yet.

## What P3-07 replaces

All of it: `NDUSBFamily` gives a real xHCI driver (several interrupters, streams, isochronous, power management, SuperSpeed hubs), a USB core with device and interface nubs, and HID and mass-storage drivers as DriverKit dexts. The console keeps its input through the HID event system. At that point patch 0033 is dropped, `kernel/neodarwin/usb` is removed, and the harness's `--sendkey-*` stays for the new stack's tests.
