#!/usr/bin/env bash
# SPDX-License-Identifier: BSD-2-Clause
# kcheck a collection and round-trip it against its kernel.
# Usage: kcheck_test.sh KCHECK COLLECTION FILES...  (uses the kernel.* file, not .unstripped)
set -euo pipefail
kcheck="$1"; collection="$2"; shift 2
kernel=""
for f in "$@"; do case "$(basename "$f")" in *.unstripped) ;; kernel.*) kernel="$f" ;; esac; done
[[ -n "$kernel" ]] || { echo "no kernel image among: $*"; exit 1; }
exec "$kcheck" "$collection" --kernel "$kernel"
