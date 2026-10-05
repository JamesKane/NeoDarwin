#!/usr/bin/env bash
# SPDX-License-Identifier: BSD-2-Clause
# ISA audit for the userland: every arm64 Mach-O under the given trees must
# run on the Armv8.2 baseline (the Radxa Dragon Q8B's Cortex-X1C/A78C;
# TARGET_CPU in tools/base/common.sh), which QEMU's neoverse-n2 can't check.
#   isa_audit.sh MATTR BASELINE TREE...
# Each file is disassembled twice, with every feature llvm-objdump knows and
# with MATTR only (as tools/xnu/isa_audit.sh does for the kernel); a word the
# second decodes as <unknown> needs a feature the baseline lacks. Words MATTR
# decodes as "hint #N" are NOP-space and allowed. A finding is
# "PATH<TAB>FUNCTION<TAB>MNEMONIC", PATH relative to its tree, one line per
# distinct finding. BASELINE lists the reviewed ones: data in __text that
# decodes as instructions (tables, strings), OpenSSL's capability probes and
# the routines it dispatches to at run time (OPENSSL_armcap), libunwind's
# MTE-only untagging. The set may shrink but never grow.
set -euo pipefail
mattr="$1"; baseline="$2"; shift 2
objdump="$(xcrun -f llvm-objdump)"
tmp="$(mktemp -d)"; trap 'rm -rf "$tmp"' EXIT
lines() { "$objdump" -d "$@" | awk '
	/^[0-9a-f]+ <.*>:$/ { fn = $2; gsub(/[<>:]/, "", fn); print "<" fn ">"; next }
	!/^ *[0-9a-f]+: [0-9a-f]{8} / { next }
	{ a = $1; sub(/:$/, "", a); t = $0; sub(/^[^\t]*\t/, "", t); print a "\t" $2 "\t" t }'; }
files=0
# arm64 Mach-O files (0xfeedfacf, CPU_TYPE_ARM64), each distinct one once: in
# runfiles every file is a link, and the image links and hard-links aliases
# (libz.dylib, libcrypto.dylib), so files are followed and deduplicated by
# content, keeping the first path in sort order.
for tree in "$@"; do
	while IFS= read -r -d '' f; do
		[ "$(head -c 8 "$f" | od -An -tx1 | tr -d ' \n')" = "cffaedfe0c000001" ] || continue
		printf '%s\t%s\t%s\n' "$(cksum < "$f" | tr -d ' \t')" "$tree" "${f#"$tree"/}"
	done < <(find -L "$tree" -type f -print0)
done | sort -t $'\t' -k3 | awk -F '\t' '!seen[$1]++ { print $2 "\t" $3 }' > "$tmp/files"
while IFS=$'\t' read -r tree rel; do
	f="$tree/$rel"
	{
		files=$((files + 1))
		lines "$f" > "$tmp/all"
		lines --mattr="$mattr" "$f" > "$tmp/base"
		paste -d $'\001' "$tmp/all" "$tmp/base" | awk -F '\001' -v path="$rel" '
			$1 ~ /^</ { fn = $1; gsub(/[<>]/, "", fn); next }
			{ split($1, a, "\t"); split($2, b, "\t")
			  if (a[1] != b[1]) { print "isa_audit: disassemblies disagree at " path " " a[1] > "/dev/stderr"; exit 2 }
			  if (b[3] !~ /<unknown>/ || a[3] ~ /<unknown>/) next
			  split(a[3], m, /[ \t]/); print path "\t" fn "\t" m[1] }' >> "$tmp/found"
	}
done < "$tmp/files"
[ "$files" -gt 0 ] || { echo "isa_audit: no arm64 Mach-O files under $*"; exit 1; }
touch "$tmp/found"; sort -u "$tmp/found" > "$tmp/got"
{ grep -v '^#' "$baseline" || true; } | sort -u > "$tmp/want"
new="$(comm -23 "$tmp/got" "$tmp/want")"; gone="$(comm -13 "$tmp/got" "$tmp/want")"
echo "isa_audit: $files files, $(wc -l < "$tmp/got" | tr -d ' ') findings (baseline $(wc -l < "$tmp/want" | tr -d ' '))"
[ -z "$gone" ] || { echo "isa_audit: no longer found (shrink the baseline):"; echo "$gone"; }
[ -z "$new" ] || { echo "isa_audit: FAIL: instructions outside $mattr:"; echo "$new"; exit 1; }
[ -z "$gone" ] || exit 1
echo "isa_audit: PASS"
