#!/usr/bin/env bash
# SPDX-License-Identifier: BSD-2-Clause
# kcgen's kernel-abi check (packaging.md §6, filesystems.md §7.3 checkpoint
# 5): the kext as built enters a collection of its declared ABI; the same
# kext with another NDKernelABI, or with none, is refused.
#   kernel_abi_test.sh KCGEN KEXT_BUNDLE ABI KERNEL_FILES...
set -euo pipefail
kcgen="$1"; kext="$2"; abi="$3"; shift 3
kernel=""
for f in "$@"; do case "$(basename "$f")" in *.unstripped) ;; kernel.*) kernel="$f" ;; esac; done
[ -n "$kernel" ] || { echo "no kernel image among: $*"; exit 1; }
work="$(mktemp -d)"; trap 'rm -rf "$work"' EXIT
declared="$(/usr/bin/plutil -extract NDKernelABI raw "$kext/Contents/Info.plist")"
[ "$declared" = "$abi" ] || { echo "FAIL: $kext declares NDKernelABI $declared, not $abi"; exit 1; }
"$kcgen" --kernel "$kernel" --output "$work/ok.kc" --kernel-version 25.0.0 --kernel-abi "$abi" --kext "$kext" > /dev/null
echo "ok: entered with NDKernelABI $abi"
name="$(basename "$kext")"
for variant in other none; do
	mkdir -p "$work/$variant"; cp -RL "$kext" "$work/$variant/"; chmod -R u+w "$work/$variant"
	plist="$work/$variant/$name/Contents/Info.plist"
	if [ "$variant" = other ]; then /usr/bin/plutil -replace NDKernelABI -string "0.$abi" "$plist"
	else /usr/bin/plutil -remove NDKernelABI "$plist"; fi
	if out="$("$kcgen" --kernel "$kernel" --output "$work/bad.kc" --kernel-version 25.0.0 --kernel-abi "$abi" --kext "$work/$variant/$name" 2>&1)"; then
		echo "FAIL: kcgen accepted a kext with NDKernelABI $variant"; exit 1
	fi
	grep -q 'built for kernel ABI' <<< "$out" || { echo "FAIL: unexpected error: $out"; exit 1; }
	echo "ok: refused ($variant): $out"
done
