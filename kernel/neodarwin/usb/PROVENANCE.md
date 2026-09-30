<!-- SPDX-License-Identifier: BSD-2-Clause -->
# ndusb provenance

| File | Origin | Licence |
|---|---|---|
| `nd_dwc3.h` | ported from FreeBSD `sys/dev/usb/controller/generic_xhci_acpi.c` (`generic_xhci_acpi_urs_setup`) and `sys/dev/usb/controller/dwc3/dwc3.h` (the `GSNPSID`, `GCTL` and `GRXTHRCFG` offsets and fields), `freebsd-src` commit `cbbcf73a5d` (branch `radxa-dragon-q8b`, after `ac16521596` and `0a6f49e72d`), tested there on the Radxa Dragon Q8B | BSD-2-Clause, Copyright (c) 2019 Val Packett (`generic_xhci_acpi.c`) and (c) 2019 Emmanuel Vadot (`dwc3.h`); the notice is kept in the file |
| `NeoDarwinXHCI.{h,cpp}`, `nd_xhci.h`, `nd_usb_desc.h`, `nd_hid_kbd.h`, `test/usb_logic_test.c` | NeoDarwin, written from the xHCI 1.2 specification (Intel 625472), USB 2.0 (chapters 9 and 11), USB 3.2 (chapter 9), HID 1.11 (§7.2, appendix B) and the HID Usage Tables (page 0x07) | BSD-2-Clause |

What was taken from FreeBSD: the DWC3 check a role switch's controller gets before it is used (a DWC_usb3/31/32 core identified by `GSNPSID`, refused unless `GCTL.PRTCAPDIR` is host, `GRXTHRCFG`'s packet-count enable cleared: bit 29 on usb3, bit 26 on usb31/32), with the same constants. What changed: `bus_read_4`/`bus_write_4` on a resource became the includer's `ND_DWC3_READ`/`ND_DWC3_WRITE` on the register window, and the result is an enumeration the driver logs instead of an errno.

Followed, not copied: FreeBSD's choice of IDs (`PNP0D10`, `PNP0D15`, `PNP0CA1`) and its rule for a USB Role Switch (the first interrupt of the present `_ADR` 0 child, `urs_host_irq`; DWC3 registers touched on role switches only) are implemented in `NeoDarwinXHCI.cpp` against IOACPIPlane. The Q8B facts (`GRXTHRCFG` 0x04f30000 left by UEFI; the keyboard behind a 1a86:8091 hub on the multiport controller) come from FreeBSD's board runs.

FreeBSD's xHCI driver (`xhci.c`) and USB stack were not used; nor was any Linux code (`drivers/usb/host/xhci*`, `drivers/usb/dwc3`, `drivers/hid`), which was not consulted.
