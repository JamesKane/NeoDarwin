#!/usr/bin/env bash
# SPDX-License-Identifier: BSD-2-Clause
# A bootable raw GPT disk image (rules/disk.bzl, docs/kernel/storage.md):
# partition 1 an EFI System Partition (FAT32) with the given files,
# partition 2 the given HFS+ volume.
#   mkdisk.sh OUT UUIDS GPTIMAGE SEED ROOT_VOLUME [PATH=FILE]...
# PATH is where FILE goes on the ESP, e.g. EFI/BOOT/BOOTAA64.EFI=... The ESP
# is made by the host's hdiutil (macOS), as mkhfs.sh makes HFS+ volumes; its
# GUIDs, and the HFS+ partition's, come from gptimage's SEED.
set -euo pipefail
out="$1"; uuids="$2"; gptimage="$3"; seed="$4"; root="$5"; shift 5
work="$(mktemp -d)"; trap 'rm -rf "$work"' EXIT
mkdir -p "$work/esp"
for spec in "$@"; do
	dest="$work/esp/${spec%%=*}"; mkdir -p "$(dirname "$dest")"; cp "${spec#*=}" "$dest"; chmod u+w "$dest"
done
hdiutil create -quiet -srcfolder "$work/esp" -fs "MS-DOS FAT32" -volname NEODARWIN -layout NONE -format UDTO -o "$work/esp-image"
"$gptimage" "$out" --seed "$seed" --uuids "$uuids" \
	--partition esp "EFI System Partition" "$work/esp-image.cdr" \
	--partition hfs NeoDarwin "$root" > /dev/null
