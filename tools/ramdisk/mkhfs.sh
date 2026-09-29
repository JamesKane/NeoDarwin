#!/usr/bin/env bash
# SPDX-License-Identifier: BSD-2-Clause
# Build a raw HFS+ volume image for neoboot's ramdisk (md0).
#   mkhfs.sh OUT VOLNAME [--dir PATH]... [--tree DIR]... [--file SRC DEST MODE]...
# A --tree directory's contents are copied to the volume's root, symbolic
# links kept.
# The image is a bare volume, no partition map, since md0 is the whole
# device. Phase 1 uses the host's hdiutil (macOS; not journaled: a ramdisk
# root has nothing to replay). A NeoDarwin image writer replaces it with the
# pinned toolchain.
set -euo pipefail
out="$1"; vol="$2"; shift 2
work="$(mktemp -d)"; trap 'rm -rf "$work"' EXIT
root="$work/root"; mkdir -p "$root"
while [ $# -gt 0 ]; do
	case "$1" in
		--dir) mkdir -p "$root/$2"; shift 2 ;;
		--tree) cp -PR "$2/." "$root/"; chmod -R u+w "$root"; shift 2 ;;
		--file) mkdir -p "$(dirname "$root/$3")"; cp "$2" "$root/$3"; chmod "$4" "$root/$3"; shift 4 ;;
		*) echo "mkhfs.sh: unknown argument $1" >&2; exit 2 ;;
	esac
done
hdiutil create -quiet -srcfolder "$root" -fs HFS+ -volname "$vol" -layout NONE -format UDTO -o "$work/image"
mv "$work/image.cdr" "$out"
# Check the volume header: "H+" at byte 1024.
[ "$(dd if="$out" bs=1 skip=1024 count=2 2>/dev/null)" = "H+" ] || { echo "mkhfs.sh: $out has no HFS+ volume header" >&2; exit 1; }
