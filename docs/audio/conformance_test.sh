#!/usr/bin/env bash
# SPDX-License-Identifier: BSD-2-Clause
# spec-conventions.md §3 and §4.8, checked mechanically over audio-service.md:
#  - every MUST closes with a requirement id [AU-AREA-NNN]; the id ends its block, which
#    may span a list (a block ends at the id, a heading, or a new bold-led paragraph);
#  - every id whose block contains a MUST is covered by a test in the §8 table;
#  - every id the table cites is defined; ids and test ids are unique.
set -uo pipefail
spec="$1"

awk '
function flush(   i) {
	if (pending != "") { print "UNMARKED\t" pending; }
	pending = ""; must = 0
}
/^## 8\. Conformance/ { flush(); intable = 1; next }
/^## 9\./ { intable = 0 }
intable {
	if ($0 ~ /^\| AU-T-[0-9][0-9][0-9]/) {
		split($0, col, "|"); t = col[2]; gsub(/ /, "", t)
		if (seen_t[t]++) print "DUPTEST\t" t
		s = col[3]
		while (match(s, /AU-[A-Z0-9]+-[0-9][0-9][0-9]/)) { print "COVERED\t" substr(s, RSTART, RLENGTH); s = substr(s, RSTART + RLENGTH) }
		ntests++
	}
	next
}
/^#/ || /^\*\*/ || /^- \*\*/ { flush() }
{
	line = $0
	if (line ~ /(^|[^A-Za-z])MUST([^A-Za-z]|$)/ && line !~ /^\|/ && line !~ /MUST, MUST NOT/) {
		must = 1; if (pending == "") pending = NR ": " line
	}
	found = 0
	while (match(line, /\[AU-[A-Z0-9]+-[0-9][0-9][0-9]\]/)) {
		id = substr(line, RSTART + 1, RLENGTH - 2); line = substr(line, RSTART + RLENGTH)
		print "DEFINED\t" id "\t" must
		found = 1
	}
	if (found) { pending = ""; must = 0 }
}
END { flush(); print "NTESTS\t" ntests }
' "$spec" > "${TEST_TMPDIR:-/tmp}/au_conf.$$"

out="${TEST_TMPDIR:-/tmp}/au_conf.$$"
fail=0
if grep -q '^UNMARKED' "$out"; then echo "MUST without a requirement id:"; grep '^UNMARKED' "$out" | cut -f2-; fail=1; fi
if grep -q '^DUPTEST' "$out"; then echo "duplicate test ids:"; grep '^DUPTEST' "$out" | cut -f2; fail=1; fi
dups=$(grep '^DEFINED' "$out" | cut -f2 | sort | uniq -d)
if [[ -n "$dups" ]]; then echo "requirement id defined twice:"; echo "$dups"; fail=1; fi
defined=$(grep '^DEFINED' "$out" | cut -f2 | sort -u)
must_ids=$(grep '^DEFINED' "$out" | awk -F'\t' '$3 == 1 {print $2}' | sort -u)
covered=$(grep '^COVERED' "$out" | cut -f2 | sort -u)
for id in $must_ids; do grep -qx "$id" <<<"$covered" || { echo "MUST not covered by a test: $id"; fail=1; }; done
for id in $covered; do grep -qx "$id" <<<"$defined" || { echo "table cites an undefined id: $id"; fail=1; }; done
ntests=$(grep '^NTESTS' "$out" | cut -f2)
rm -f "$out"
[[ $fail -eq 0 ]] && echo "audio conformance: ok ($(wc -w <<<"$defined" | tr -d ' ') requirement ids, $(wc -w <<<"$must_ids" | tr -d ' ') with MUST, $ntests tests)"
exit $fail
