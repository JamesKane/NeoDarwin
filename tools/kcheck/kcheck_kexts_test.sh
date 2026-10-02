#!/usr/bin/env bash
# SPDX-License-Identifier: BSD-2-Clause
# kcheck a collection with kexts: the checks of kcheck_test.sh but the
# round-trip (the kernel's segments move apart around the kexts'), and at
# least one entry besides the kernel.
# Usage: kcheck_kexts_test.sh KCHECK COLLECTION
set -euo pipefail
out="$("$1" "$2")" || { printf '%s\n' "$out"; exit 1; }
printf '%s\n' "$out"
n="$(sed -nE 's/.*; ([0-9]+) entr(y|ies)$/\1/p' <<< "$out")"
[ "${n:-0}" -ge 2 ] || { echo "kcheck_kexts_test: expected the kernel and a kext, found ${n:-no} entries"; exit 1; }
