#!/usr/bin/env bash
# SPDX-License-Identifier: BSD-2-Clause
# check_kernel.sh FILE... ARCH: among FILEs, the kernel.* image must be a
# Mach-O executable for ARCH with no undefined symbols.
set -euo pipefail
arch="${@: -1}"; kernel=""
for f in "${@:1:$#-1}"; do case "$(basename "$f")" in *.unstripped) ;; kernel.*) kernel="$f" ;; esac; done
[ -n "$kernel" ] || { echo "no kernel.* among inputs"; exit 1; }
desc="$(file -b "$kernel")"; echo "$(basename "$kernel"): $desc"
case "$desc" in "Mach-O 64-bit executable $arch") ;; *) echo "expected Mach-O 64-bit executable $arch"; exit 1 ;; esac
undef="$(nm -u "$kernel" | wc -l | tr -d ' ')"
echo "undefined symbols: $undef"
[ "$undef" -eq 0 ]
