<!-- SPDX-License-Identifier: BSD-2-Clause -->
# radxa-dragon-q8b.acpidump

The ACPI tables of the Radxa Dragon Q8B (Qualcomm SC8280XP; firmware Qualcomm UEFI BOOT.MXF.1.1, 2026-08-18), the NeoDarwin bring-up board, in Linux `acpidump` text format for `dtdump` and neoboot's parser. They were read from the running board with `acpi-kcore.py` during the FreeBSD port of the same board, one binary file per table, then converted.

The table contents are exact. Their addresses are reconstructed, since that dump saved tables by signature, not address:
- RSDP 0xffffd000, and the XSDT and DSDT addresses, come from the tables themselves.
- CSRT (51 KB) and IORT (6 KB) are the only tables longer than a page; the gaps between the XSDT's entries fit them only at 0xfffee000 and 0xfffc8000.
- The single-page tables are assigned to the remaining XSDT slots in an assumed order.

Nothing neoboot builds depends on those slots: it finds tables by signature, and the device tree it makes carries only the RSDP's address. Firmware data, kept as test data.
