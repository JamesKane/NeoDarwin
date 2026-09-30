#!/usr/bin/env bash
# SPDX-License-Identifier: BSD-2-Clause
# Build a raw HFS+ volume image for neoboot's ramdisk (md0), or for a disk
# image's root partition (rules/disk.bzl).
#   mkhfs.sh OUT VOLNAME [--size SIZE] [--journaled] [--dir PATH]... [--tree DIR]... [--file SRC DEST MODE]... [--link TARGET PATH]... [--mode PATH MODE]...
#            [--trust-cache TOOL TC MANIFEST [--trust-cache-exclude PATH]...]
# A --tree directory's contents are copied to the volume's root, symbolic
# links kept. --link makes a symbolic link; --mode sets a path's mode after
# everything is in place (e.g. setuid, or 0600 for master.passwd). --size
# makes the volume that big (hdiutil's size syntax, e.g. 256m) instead of
# just big enough, leaving room to write; --journaled makes it journaled
# HFS+, which a writable root needs: HFS refuses to mount a dirty volume
# read-write without a journal to replay. --trust-cache runs TOOL
# (//tools/trustcache) over the staged root, exactly what the volume holds,
# writing its static trust cache TC and MANIFEST; --trust-cache-exclude
# leaves a path out of it (P1-15).
# The image is a bare volume, no partition map, since md0 is the whole
# device. Phase 1 uses the host's hdiutil (macOS; not journaled: a ramdisk
# root has nothing to replay). A NeoDarwin image writer replaces it with the
# pinned toolchain.
set -euo pipefail
out="$1"; vol="$2"; shift 2
work="$(mktemp -d)"; trap 'rm -rf "$work"' EXIT
root="$work/root"; mkdir -p "$root"; modes=(); size=(); fs="HFS+"; tc=(); tcx=()
while [ $# -gt 0 ]; do
	case "$1" in
		--size) size=(-size "$2"); shift 2 ;;
		--journaled) fs="Journaled HFS+"; shift ;;
		--dir) mkdir -p "$root/$2"; shift 2 ;;
		--tree) cp -PR "$2/." "$root/"; chmod -R u+w "$root"; shift 2 ;;
		--file) mkdir -p "$(dirname "$root/$3")"; cp "$2" "$root/$3"; chmod "$4" "$root/$3"; shift 4 ;;
		--link) mkdir -p "$(dirname "$root/$3")"; ln -sfn "$2" "$root/$3"; shift 3 ;;
		--mode) modes+=("$2" "$3"); shift 3 ;;
		--trust-cache) tc=("$2" "$3" "$4"); shift 4 ;;
		--trust-cache-exclude) tcx+=(--exclude "$2"); shift 2 ;;
		*) echo "mkhfs.sh: unknown argument $1" >&2; exit 2 ;;
	esac
done
i=0; while [ $i -lt ${#modes[@]} ]; do chmod "${modes[$((i + 1))]}" "$root/${modes[$i]}"; i=$((i + 2)); done
if [ ${#tc[@]} -gt 0 ]; then
	"${tc[0]}" create "${tc[1]}" --manifest "${tc[2]}" ${tcx[@]+"${tcx[@]}"} "$root" > /dev/null
fi
hdiutil create -quiet -srcfolder "$root" -fs "$fs" -volname "$vol" ${size[@]+"${size[@]}"} -layout NONE -format UDTO -o "$work/image"
mv "$work/image.cdr" "$out"
# Check the volume header: "H+" at byte 1024.
[ "$(dd if="$out" bs=1 skip=1024 count=2 2>/dev/null)" = "H+" ] || { echo "mkhfs.sh: $out has no HFS+ volume header" >&2; exit 1; }
