#!/usr/bin/env bash
# SPDX-License-Identifier: BSD-2-Clause
# acpidump on the build machine (//base/acpidump:acpidump_host_test), against
# the macOS host's CoreFoundation and IOKit (the base's are built to their
# ABI and install names). The tables are a QEMU capture (neoboot's
# dump-acpi, boot/neoboot/testdata/*.acpidump), turned into the property
# list the kernel's "ACPI Tables" would serialize to and read through
# acpidump's -X test seam.
#   host_test.sh ACPIDUMP CAPTURE
set -euo pipefail
acpidump="$1"; capture="$2"
work="$(mktemp -d)"; trap 'rm -rf "$work"' EXIT
# Each "SIG @ 0xADDR" block's hex bytes (columns 11-58 of its lines) -> SIG.bin.
awk -v dir="$work" '
	/^[A-Z0-9]{4} @ 0x/ { sig = $1; next }
	/^    [0-9A-F]{4}: / && sig != "" { print substr($0, 11, 48) > (dir "/" sig ".hex") }
' "$capture"
{
	echo '<?xml version="1.0" encoding="UTF-8"?>'
	echo '<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">'
	echo '<plist version="1.0"><dict><key>ACPI Tables</key><dict>'
	for h in "$work"/*.hex; do
		sig="$(basename "$h" .hex)"
		xxd -r -p "$h" > "$work/$sig.bin"
		echo "<key>$sig</key><data>$(base64 < "$work/$sig.bin" | tr -d '\n')</data>"
	done
	echo '</dict></dict></plist>'
} > "$work/tables.plist"

fails=0
expect() { # expect NAME PATTERN <<< OUTPUT
	if grep -qF -- "$2"; then echo "ok: $1"; else echo "FAIL: $1: no line with: $2"; fails=$((fails + 1)); fi
}

out="$("$acpidump" -X "$work/tables.plist" -t)"
echo "$out"
expect rsdp "  RSD PTR: OEM=BOCHS, ACPI_Rev=2.0x (2)" <<< "$out"
expect xsdt-header "  XSDT: Length=100, Revision=1, Checksum=" <<< "$out"
expect xsdt-entries "	Entries={ 0x00000000bcb43b18, " <<< "$out"
expect facp "  FACP: Length=276, Revision=6, " <<< "$out"
expect oem "	OEMID=BOCHS, OEM Table ID=BXPC, OEM Revision=0x1," <<< "$out"
expect dsdt "  DSDT: Length=" <<< "$out"
expect mcfg "	Segment Group=0x0000" <<< "$out"
for sig in APIC GTDT MCFG SPCR IORT PPTT DBG2; do
	expect "$sig" "  $sig: Length=" <<< "$out"
done
if grep -q "corrupt" <<< "$out"; then echo "FAIL: a table is corrupt"; fails=$((fails + 1)); fi
# The DSDT follows the FADT, as FreeBSD prints it.
if [ "$(grep -A5 "  FACP:" <<< "$out" | grep -c "  DSDT:")" = 1 ]; then echo "ok: dsdt-after-facp"; else
	echo "FAIL: the DSDT doesn't follow the FADT"; fails=$((fails + 1)); fi

one="$("$acpidump" -X "$work/tables.plist" -T MCFG)"
expect T-mcfg "  MCFG: Length=" <<< "$one"
if [ "$(grep -c ": Length=" <<< "$one")" = 1 ]; then echo "ok: T-one-table"; else
	echo "FAIL: -T MCFG printed other tables"; fails=$((fails + 1)); fi

# -o: no SSDTs in this capture, so the DSDT byte for byte.
"$acpidump" -X "$work/tables.plist" -o "$work/out.dsdt"
if cmp "$work/out.dsdt" "$work/DSDT.bin"; then echo "ok: -o"; else echo "FAIL: -o"; fails=$((fails + 1)); fi

# The registry has no "ACPI Tables" on macOS (Apple silicon), and -r-style
# misuse gets the usage.
if "$acpidump" -t 2> "$work/err"; then echo "FAIL: no tables but status 0"; fails=$((fails + 1)); else
	expect no-tables "ACPI Tables" < "$work/err"; fi
if "$acpidump" -T TOOLONG 2> /dev/null; then echo "FAIL: -T TOOLONG accepted"; fails=$((fails + 1)); else echo "ok: -T length"; fi

[ "$fails" = 0 ]
