#!/usr/bin/env bash
# SPDX-License-Identifier: BSD-2-Clause
# ISA audit for generic Arm kernels.
#   isa_audit.sh [--mattr FEATURES] [--no-sysreg-control] FILE... BASELINE   (the *.unstripped kernel among FILEs is audited,
#                                                     or a kext bundle's executable: FILE is the .kext)
# Lists every instruction a generic Arm core would fault on or mis-execute:
#   - MRS/MSR to op0=3, CRn=15 (S3_<op1>_C15_<CRm>_<op2>), the IMPLEMENTATION
#     DEFINED register space where Apple keeps PMCs, HID and IPI registers;
#   - HVC (Apple's virtual-machine hypercalls; there is no hypervisor);
#   - Apple AMX (0x00201000-0x002013ff) and GXF (0x00201400, 0x00201420) words;
#   - with --mattr (llvm-objdump features, e.g. +v8.2a,+rcpc), every
#     instruction outside that ISA: the kernel is disassembled twice, with
#     every feature llvm knows (llvm-objdump's default) and with FEATURES
#     only, and a word the two decode differently needs a feature FEATURES
#     lacks. That catches plain instructions (FEATURES shows <unknown>) and
#     SYS/MSR aliases alike (TLBI RVAE1IS becomes "sys", MSR DIT a generic
#     S3_3_C4_C2_5). Two differences are architecturally safe and allowed:
#     words FEATURES decodes as "hint #N" (NOP-space: PACIBSP, BTI, ... are
#     NOPs where unimplemented) and MRS reads of the ID register space
#     S3_0_C0_* (RAZ where unallocated). The finding's detail is the
#     instruction's mnemonic, plus the first operand for system instructions.
# The set may shrink but never grow. Controls guard the parser: the kernel must
# contain NOPs and system-register moves, or the disassembly format changed,
# and both disassemblies must list the same addresses. --no-sysreg-control
# drops the second control for a small kext that touches no system register
# (//kexts/swift_trial); the NOP and address controls still guard the parser.
set -euo pipefail
mattr=""
[ "${1:-}" = "--mattr" ] && { mattr="$2"; shift 2; }
sysreg_control=1
[ "${1:-}" = "--no-sysreg-control" ] && { sysreg_control=0; shift; }
baseline="${@: -1}"; kernel=""
for f in "${@:1:$#-1}"; do case "$f" in *.unstripped) kernel="$f" ;; *.kext) kernel="$f/Contents/MacOS/$(basename "$f" .kext)" ;; esac; done
[ -n "$kernel" ] || { echo "no *.unstripped kernel or .kext bundle among inputs"; exit 1; }
tmp="$(mktemp -d)"; trap 'rm -rf "$tmp"' EXIT
objdump="$(xcrun -f llvm-objdump)"
# Instruction lines as "address<TAB>word<TAB>text"; function labels as "<name>".
lines() { "$objdump" -d "$@" "$kernel" | awk '
	/^[0-9a-f]+ <.*>:$/ { fn = $2; gsub(/[<>:]/, "", fn); print "<" fn ">"; next }
	!/^ *[0-9a-f]+: [0-9a-f]{8} / { next }
	{ a = $1; sub(/:$/, "", a); t = $0; sub(/^[^\t]*\t/, "", t); print a "\t" $2 "\t" t }'; }
lines > "$tmp/all"
if [ -n "$mattr" ]; then lines --mattr="$mattr" > "$tmp/base"; else cp "$tmp/all" "$tmp/base"; fi
paste -d $'\001' "$tmp/all" "$tmp/base" | awk -F '\001' -v controls="$tmp/controls" -v isa="$mattr" '
	{
		if ($1 ~ /^</) { if ($1 != $2) { bad++ } fn = substr($1, 2, length($1) - 2); next }
		split($1, x, "\t"); split($2, y, "\t")
		if (x[1] != y[1]) { bad++; next }
		w = x[2]; l = $1; b = $2
		sub(/^[^\t]*\t[^\t]*\t/, "", l); sub(/^[^\t]*\t[^\t]*\t/, "", b); l = tolower(l); b = tolower(b)
		if (w == "d503201f") nops++
		if (l ~ /^(msr|mrs)\t/) sysmoves++
		if (l ~ /^(msr|mrs)\t/ && match(l, /s3_[0-7]_c15_c[0-9]+_[0-7]/)) {
			print fn "\t" ((l ~ /^msr\t/) ? "write" : "read") "\t" substr(l, RSTART, RLENGTH)
		}
		if (l ~ /^hvc\t/) print fn "\thvc\t-"
		if (w ~ /^00201[0-3][0-9a-f][0-9a-f]$/) print fn "\tamx\t" w
		if (w == "00201400" || w == "00201420") print fn "\tgxf\t" w
		if (isa != "" && l != b && b !~ /^hint\t/ && b !~ /^mrs\t[xw][0-9]+, s3_0_c0_c[0-7]_[0-7]$/) {
			split(l, m, "\t"); d = m[1]
			if (d ~ /^(msr|mrs|sys|sysl|tlbi|dc|ic|at)$/) { o = m[2]; if (d == "mrs") sub(/^[^,]*, */, "", o); sub(/,.*/, "", o); d = d " " o }
			print fn "\tisa\t" d
		}
	}
	END { print (nops + 0) " " (sysmoves + 0) " " (bad + 0) > controls }' | sort -u > "$tmp/current"
read -r nops sysmoves bad < "$tmp/controls"
if [ "$nops" -eq 0 ] || { [ "$sysreg_control" -eq 1 ] && [ "$sysmoves" -eq 0 ]; } || [ "$bad" -ne 0 ]; then
	echo "parser control failed (nops=$nops system-register moves=$sysmoves misaligned lines=$bad): llvm-objdump output format changed"
	exit 1
fi
{ grep -v '^#' "$baseline" || true; } | sed '/^$/d' | sort -u > "$tmp/base.txt"
echo "ISA findings${mattr:+ (baseline ISA $mattr)}: $(wc -l < "$tmp/current" | tr -d ' ') now, $(wc -l < "$tmp/base.txt" | tr -d ' ') in baseline (controls: $nops nops, $sysmoves system-register moves)"
gone="$(comm -13 "$tmp/current" "$tmp/base.txt")"; new="$(comm -23 "$tmp/current" "$tmp/base.txt")"
[ -z "$gone" ] || { echo "removed since baseline; update the baseline:"; echo "$gone" | sed 's/^/  - /'; }
[ -z "$new" ] || { echo "NEW findings:"; echo "$new" | sed 's/^/  + /'; exit 1; }
