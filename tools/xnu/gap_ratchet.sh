#!/usr/bin/env bash
# SPDX-License-Identifier: BSD-2-Clause
# Link-gap ratchet: the kernel's undefined-symbol set may shrink but never grow.
#   gap_ratchet.sh REPORT BASELINE
set -euo pipefail
report="$1"; baseline="$2"
current="$(mktemp)"; base="$(mktemp)"; trap 'rm -f "$current" "$base"' EXIT
sed -n '/^--- symbol/,/^--- raw/p' "$report" | sed '1d;$d' | cut -f1 | sort -u > "$current"
grep -v '^#' "$baseline" | cut -f1 | sed '/^$/d' | sort -u > "$base"
new="$(comm -23 "$current" "$base")"
gone="$(comm -13 "$current" "$base")"
echo "link gaps: $(wc -l < "$current" | tr -d ' ') now, $(wc -l < "$base" | tr -d ' ') in baseline"
if [ -n "$gone" ]; then
	echo "closed since baseline ($(echo "$gone" | wc -l | tr -d ' ')); update the baseline to lock them in:"
	echo "$gone" | sed 's/^/  - /'
fi
if [ -n "$new" ]; then
	echo "NEW undefined symbols (not in baseline):"
	echo "$new" | sed 's/^/  + /'
	exit 1
fi
