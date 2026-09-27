#!/usr/bin/env bash
# SPDX-License-Identifier: BSD-2-Clause
# Implementation-defined system-register audit for generic Arm kernels.
#   sysreg_audit.sh FILE... BASELINE   (the *.unstripped kernel among FILEs is audited)
# Lists every MRS/MSR to op0=3, CRn=15 (S3_<op1>_C15_<CRm>_<op2>), the space
# Arm reserves for IMPLEMENTATION DEFINED registers. Apple silicon keeps its
# PMCs, HID and IPI registers there; a generic core raises an undefined-
# instruction exception on any of them. The set may shrink but never grow.
set -euo pipefail
baseline="${@: -1}"; kernel=""
for f in "${@:1:$#-1}"; do case "$f" in *.unstripped) kernel="$f" ;; esac; done
[ -n "$kernel" ] || { echo "no *.unstripped kernel among inputs"; exit 1; }
current="$(mktemp)"; base="$(mktemp)"; trap 'rm -f "$current" "$base"' EXIT
"$(xcrun -f llvm-objdump)" -d --no-show-raw-insn "$kernel" | awk '
	/^[0-9a-f]+ <.*>:$/ { fn = $2; gsub(/[<>:]/, "", fn) }
	{ l = tolower($0) }
	l ~ /\t(msr|mrs)\t/ && match(l, /s3_[0-7]_c15_c[0-9]+_[0-7]/) {
		op = (l ~ /\tmsr\t/) ? "write" : "read"
		print fn "\t" op "\t" substr(l, RSTART, RLENGTH)
	}' | sort -u > "$current"
grep -v '^#' "$baseline" | sed '/^$/d' | sort -u > "$base"
echo "implementation-defined register accesses: $(wc -l < "$current" | tr -d ' ') now, $(wc -l < "$base" | tr -d ' ') in baseline"
gone="$(comm -13 "$current" "$base")"; new="$(comm -23 "$current" "$base")"
[ -z "$gone" ] || { echo "removed since baseline; update the baseline:"; echo "$gone" | sed 's/^/  - /'; }
[ -z "$new" ] || { echo "NEW accesses:"; echo "$new" | sed 's/^/  + /'; exit 1; }
