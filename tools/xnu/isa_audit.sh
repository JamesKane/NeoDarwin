#!/usr/bin/env bash
# SPDX-License-Identifier: BSD-2-Clause
# Apple-ISA audit for generic Arm kernels.
#   sysreg_audit.sh FILE... BASELINE   (the *.unstripped kernel among FILEs is audited)
# Lists every instruction a generic Arm core would fault on or mis-execute:
#   - MRS/MSR to op0=3, CRn=15 (S3_<op1>_C15_<CRm>_<op2>), the IMPLEMENTATION
#     DEFINED register space where Apple keeps PMCs, HID and IPI registers;
#   - HVC (Apple's virtual-machine hypercalls; there is no hypervisor);
#   - Apple AMX (0x00201000-0x002013ff) and GXF (0x00201400, 0x00201420) words.
# The set may shrink but never grow. Controls guard the parser: the kernel must
# contain NOPs and system-register moves, or the disassembly format changed.
set -euo pipefail
baseline="${@: -1}"; kernel=""
for f in "${@:1:$#-1}"; do case "$f" in *.unstripped) kernel="$f" ;; esac; done
[ -n "$kernel" ] || { echo "no *.unstripped kernel among inputs"; exit 1; }
current="$(mktemp)"; base="$(mktemp)"; controls="$(mktemp)"; trap 'rm -f "$current" "$base" "$controls"' EXIT
"$(xcrun -f llvm-objdump)" -d "$kernel" | awk -v controls="$controls" '
	/^[0-9a-f]+ <.*>:$/ { fn = $2; gsub(/[<>:]/, "", fn); next }
	!/^ *[0-9a-f]+: [0-9a-f]{8} / { next }
	{
		w = $2; l = tolower($0)
		if (w == "d503201f") nops++
		if (l ~ /\t(msr|mrs)\t/) sysmoves++
		if (l ~ /\t(msr|mrs)\t/ && match(l, /s3_[0-7]_c15_c[0-9]+_[0-7]/)) {
			print fn "\t" ((l ~ /\tmsr\t/) ? "write" : "read") "\t" substr(l, RSTART, RLENGTH)
		}
		if (l ~ /\thvc\t/) print fn "\thvc\t-"
		if (w ~ /^00201[0-3][0-9a-f][0-9a-f]$/) print fn "\tamx\t" w
		if (w == "00201400" || w == "00201420") print fn "\tgxf\t" w
	}
	END { print (nops + 0) " " (sysmoves + 0) > controls }' | sort -u > "$current"
read -r nops sysmoves < "$controls"
if [ "$nops" -eq 0 ] || [ "$sysmoves" -eq 0 ]; then
	echo "parser control failed (nops=$nops system-register moves=$sysmoves): llvm-objdump output format changed"
	exit 1
fi
{ grep -v '^#' "$baseline" || true; } | sed '/^$/d' | sort -u > "$base"
echo "Apple-ISA findings: $(wc -l < "$current" | tr -d ' ') now, $(wc -l < "$base" | tr -d ' ') in baseline (controls: $nops nops, $sysmoves system-register moves)"
gone="$(comm -13 "$current" "$base")"; new="$(comm -23 "$current" "$base")"
[ -z "$gone" ] || { echo "removed since baseline; update the baseline:"; echo "$gone" | sed 's/^/  - /'; }
[ -z "$new" ] || { echo "NEW findings:"; echo "$new" | sed 's/^/  + /'; exit 1; }
