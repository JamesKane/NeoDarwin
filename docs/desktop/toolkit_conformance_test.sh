#!/usr/bin/env bash
# SPDX-License-Identifier: BSD-2-Clause
# spec-conventions.md §3, §4.8 and §7, checked mechanically over toolkit-api.md:
#  - every MUST closes with a requirement id [TK-AREA-NNN]; the id ends its block, which
#    may span a list (a block ends at the id, a heading, or a new bold-led paragraph);
#  - every id whose block contains a MUST is covered by a test in the §8 table;
#  - every id the table cites is defined; ids and test ids are unique;
#  - every lower-layer id the text cites (WP-, SC-, AU-) is defined in that
#    specification, for each specification passed as PREFIX=path.
# Usage: toolkit_conformance_test.sh toolkit-api.md [WP=window-protocol.md] [AU=...] [SC=...]
set -uo pipefail
spec="$1"; shift
tmp="${TEST_TMPDIR:-/tmp}/tk_conf.$$"

awk '
function flush() {
	if (pending != "") { print "UNMARKED\t" pending; }
	pending = ""; must = 0
}
/^## 8\. Conformance/ { flush(); intable = 1; next }
/^## 9\./ { intable = 0 }
intable {
	if ($0 ~ /^\| TK-T-[0-9][0-9][0-9]/) {
		split($0, col, "|"); t = col[2]; gsub(/ /, "", t)
		if (seen_t[t]++) print "DUPTEST\t" t
		s = col[3]
		while (match(s, /TK-[A-Z0-9]+-[0-9][0-9][0-9]/)) { print "COVERED\t" substr(s, RSTART, RLENGTH); s = substr(s, RSTART + RLENGTH) }
		ntests++
	}
	next
}
/^#/ || /^\*\*/ || /^- \*\*/ { flush() }
{
	line = $0
	if (line ~ /(^|[^A-Za-z])MUST([^A-Za-z]|$)/ && line !~ /MUST, MUST NOT/ && line !~ /^\|/) {
		must = 1; if (pending == "") pending = NR ": " line
	}
	found = 0
	while (match(line, /\[TK-[A-Z0-9]+-[0-9][0-9][0-9]\]/)) {
		id = substr(line, RSTART + 1, RLENGTH - 2); line = substr(line, RSTART + RLENGTH)
		print "DEFINED\t" id "\t" must
		found = 1
	}
	if (found) {
		pending = ""; must = 0
		# a MUST after the last id on the line opens a new block that still needs an id
		if (line ~ /(^|[^A-Za-z])MUST([^A-Za-z]|$)/ && $0 !~ /^\|/) { must = 1; pending = NR ": " line }
	}
}
END { flush(); print "NTESTS\t" ntests }
' "$spec" > "$tmp"

fail=0
if grep -q '^UNMARKED' "$tmp"; then echo "MUST without a requirement id:"; grep '^UNMARKED' "$tmp" | cut -f2-; fail=1; fi
if grep -q '^DUPTEST' "$tmp"; then echo "duplicate test ids:"; grep '^DUPTEST' "$tmp" | cut -f2; fail=1; fi
dups=$(grep '^DEFINED' "$tmp" | cut -f2 | sort | uniq -d)
if [[ -n "$dups" ]]; then echo "requirement id defined twice:"; echo "$dups"; fail=1; fi
defined=$(grep '^DEFINED' "$tmp" | cut -f2 | sort -u)
must_ids=$(grep '^DEFINED' "$tmp" | awk -F'\t' '$3 == 1 {print $2}' | sort -u)
covered=$(grep '^COVERED' "$tmp" | cut -f2 | sort -u)
for id in $must_ids; do grep -qx "$id" <<<"$covered" || { echo "MUST not covered by a test: $id"; fail=1; }; done
for id in $covered; do grep -qx "$id" <<<"$defined" || { echo "table cites an undefined id: $id"; fail=1; }; done
# Every TK id cited anywhere is defined.
for id in $(grep -oE 'TK-[A-Z0-9]+-[0-9]{3}' "$spec" | grep -v '^TK-T-' | sort -u); do
	grep -qx "$id" <<<"$defined" || { echo "text cites an undefined TK id: $id"; fail=1; }
done
ntests=$(grep '^NTESTS' "$tmp" | cut -f2)

# Lower layers: requirement ids are defined as [ID]; test ids as a table row "| ID |".
checked=""
for arg in "$@"; do
	prefix="${arg%%=*}"; lower="${arg#*=}"
	checked="$checked $prefix"
	cited=$(grep -oE "${prefix}-[A-Z0-9]+-[0-9]{3}" "$spec" | sort -u)
	n=0
	for id in $cited; do
		n=$((n + 1))
		if [[ "$id" == "${prefix}-T-"* ]]; then
			grep -qE "^\| ${id} \|" "$lower" || { echo "cites ${id}, which ${lower##*/} does not define"; fail=1; }
		else
			grep -qF "[${id}]" "$lower" || { echo "cites ${id}, which ${lower##*/} does not define"; fail=1; }
		fi
	done
	echo "cross-references: ${n} ${prefix} ids checked against ${lower##*/}"
done
rm -f "$tmp"
[[ $fail -eq 0 ]] && echo "toolkit conformance: ok ($(wc -w <<<"$defined" | tr -d ' ') requirement ids, $(wc -w <<<"$must_ids" | tr -d ' ') with MUST, $ntests tests; lower layers checked:${checked:- none})"
exit $fail
