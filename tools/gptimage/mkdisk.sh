#!/usr/bin/env bash
# SPDX-License-Identifier: BSD-2-Clause
# A bootable raw GPT disk image (rules/disk.bzl, docs/kernel/storage.md):
# partition 1 an EFI System Partition (FAT32) with the given files,
# partition 2 the given HFS+ volume, or with zfs:SIZE (e.g. zfs:384m) a
# blank FreeBSD-ZFS partition of that size for a pool (rules/zfs_image.bzl).
#   mkdisk.sh OUT UUIDS GPTIMAGE SEED ROOT_VOLUME [--esp-size SIZE] [PATH=FILE]...
# --esp-size gives the ESP a size (hdiutil's syntax, e.g. 128m) instead of
# just enough for its files: room for the kernels `ndpkg system` writes
# there later (docs/architecture/packaging.md §6.1).
# PATH is where FILE goes on the ESP, e.g. EFI/BOOT/BOOTAA64.EFI=... The ESP
# is made by the host's hdiutil (macOS), as mkhfs.sh makes HFS+ volumes; its
# GUIDs, and the HFS+ partition's, come from gptimage's SEED.
set -euo pipefail
out="$1"; uuids="$2"; gptimage="$3"; seed="$4"; root="$5"; shift 5
espsize=()
if [ "${1:-}" = --esp-size ]; then espsize=(-size "$2"); shift 2; fi
work="$(mktemp -d)"; trap 'rm -rf "$work"' EXIT
mkdir -p "$work/esp"
for spec in "$@"; do
	dest="$work/esp/${spec%%=*}"; mkdir -p "$(dirname "$dest")"; cp "${spec#*=}" "$dest"; chmod u+w "$dest"
done
hdiutil create -quiet ${espsize[@]+"${espsize[@]}"} -srcfolder "$work/esp" -fs "MS-DOS FAT32" -volname NEODARWIN -layout NONE -format UDTO -o "$work/esp-image"
case "$root" in
zfs:*)
	size="${root#zfs:}"
	case "$size" in *[kK]) n=$(( ${size%?} << 10 )) ;; *[mM]) n=$(( ${size%?} << 20 )) ;; *[gG]) n=$(( ${size%?} << 30 )) ;; *) n=$size ;; esac
	dd if=/dev/zero of="$work/zfs.img" bs=1 count=0 seek="$n" 2> /dev/null
	part=(--partition 516E7CBA-6ECF-11D6-8FF8-00022D09712B ndpool "$work/zfs.img") ;;
*) part=(--partition hfs NeoDarwin "$root") ;;
esac
"$gptimage" "$out" --seed "$seed" --uuids "$uuids" \
	--partition esp "EFI System Partition" "$work/esp-image.cdr" \
	"${part[@]}" > /dev/null
