#!/usr/bin/env bash
# SPDX-License-Identifier: BSD-2-Clause
# //base:freebsd_tests_exit_check (P4-21's exit, docs/architecture/
# freebsd-parity.md §2.1): a row passes FreeBSD's tests when every case of
# it in expected.tsv is PASS or XFAIL, or carries a reviewed reason:
#   structural: a decided reason (an Apple implementation, an equivalent,
#               HFS+, a subsystem §5 puts out of scope, Darwin's or xnu's
#               semantics, a program the base replaced by decision, FreeBSD's
#               own architecture limits); it counts as passing;
#   timing:     a FLAKY case that fails only under load; it counts as passing
#               (it has passed);
#   fixable:    anything else (a missing port, file system, data file, uid,
#               a bug); the row is blocked until it's fixed.
#   exit_check.sh EXPECTED INVENTORY [--require-exit]
# The check fails if a non-passing case has none of the three prefixes, a
# timing case isn't FLAKY, a suite row isn't a built inventory row (rows as
# the inventory has them: the longest row path that prefixes the test
# program's path), or a known gap below is stale. It passes while fixable
# cases remain and reports them: the exit's remaining work. --require-exit
# also fails while any fixable case remains (for the day P4-21 closes).
# The summary is printed and written as exit_summary.txt to the test's
# outputs.
set -euo pipefail
expected="$1"; inventory="$2"; shift 2
require=0
for a in "$@"; do
	case "$a" in
	--require-exit) require=1 ;;
	*) echo "exit_check: unknown option $a"; exit 2 ;;
	esac
done

# Built rows with FreeBSD tests that aren't in the suite (ROW TAB WHY).
gaps="bin/pax	its only test, legacy_test.pl, is Perl (TAP_TESTS_PERL; no Perl in the base)
usr.bin/sockstat	its only test, sockstat_test, is a unit test of FreeBSD's sockstat.c, which the base doesn't build (the row is lsof)"

out="${TEST_UNDECLARED_OUTPUTS_DIR:-${TEST_TMPDIR:-$(mktemp -d)}}"; mkdir -p "$out"
summary="$out/exit_summary.txt"

status=0
printf '%s\n' "$gaps" | awk -F'\t' -v require="$require" '
	function good(r) { return r == "PASS" || r == "XFAIL" }
	FILENAME == "-" { gap[$1] = $2; next }
	FILENAME == ARGV[2] {
		if (/^#/ || $1 == "path" || !NF) next
		built[$1] = ($6 == "yes"); status[$1] = $2; next
	}
	/^#/ || !NF { next }
	{
		p = $1; sub(/:.*/, "", p); row = ""
		for (r in built) if (index(p "/", r "/") == 1 && length(r) > length(row)) row = r
		if (row == "") { print "ERROR: " $1 ": no inventory row for " p; bad = 1; next }
		rows[row] = 1; cases[row]++; total++
		if (good($2)) { npass[row]++; cls = "pass" }
		else if ($3 ~ /^structural: /) cls = "structural"
		else if ($3 ~ /^fixable: /) cls = "fixable"
		else if ($3 ~ /^timing: /) {
			cls = "timing"
			if ($2 != "FLAKY") { print "ERROR: " $1 ": timing: on a " $2 " case (only FLAKY cases are timing)"; bad = 1 }
		} else { print "ERROR: " $1 ": " $2 " without structural:, fixable: or timing: (" $3 ")"; bad = 1; cls = "unmarked" }
		n[row, cls]++; all[cls]++
	}
	END {
		nrows = 0; passing = 0; blocked = 0
		for (r in rows) {
			nrows++
			if (!built[r]) { print "ERROR: " r ": in the suite but not a built inventory row"; bad = 1 }
			if (n[r, "fixable"] + n[r, "unmarked"] > 0) blocked++; else passing++
		}
		for (g in gap) {
			if (!(g in built)) { print "ERROR: known gap " g ": not an inventory row"; bad = 1 }
			else if (!built[g]) { print "ERROR: known gap " g ": not built"; bad = 1 }
			if (g in rows) { print "ERROR: known gap " g ": now in the suite (remove it from exit_check.sh)"; bad = 1 }
		}
		print "P4-21 exit: FreeBSD tests by row (base/freebsd_tests/expected.tsv)"
		printf "cases: %d; PASS or XFAIL %d, structural %d, timing %d, fixable %d", total, all["pass"], all["structural"], all["timing"], all["fixable"]
		if (all["unmarked"]) printf ", unmarked %d", all["unmarked"]
		print ""
		printf "rows: %d in the suite; %d pass, %d blocked by fixable cases\n", nrows, passing, blocked
		print "known gaps (built rows with FreeBSD tests not in the suite):"
		for (g in gap) print "  " g ": " gap[g] | "sort"
		close("sort")
		print "blocked rows (fixable cases; then cases, structural, timing):"
		for (r in rows) if (n[r, "fixable"] + n[r, "unmarked"] > 0)
			printf "  %-24s fixable %3d   cases %4d, structural %3d, timing %d%s\n", r, n[r, "fixable"], cases[r], n[r, "structural"], n[r, "timing"], (n[r, "unmarked"] ? ", unmarked " n[r, "unmarked"] : "") | "sort -k3,3nr -k1,1"
		close("sort -k3,3nr -k1,1")
		print "passing rows (cases; PASS or XFAIL, structural, timing):"
		for (r in rows) if (n[r, "fixable"] + n[r, "unmarked"] == 0)
			printf "  %-24s cases %4d: %4d, structural %3d, timing %d\n", r, cases[r], n[r, "pass"], n[r, "structural"], n[r, "timing"] | "sort"
		close("sort")
		if (require && all["fixable"] > 0) { print "ERROR: --require-exit: " all["fixable"] " fixable cases in " blocked " rows"; bad = 1 }
		exit bad
	}' - "$inventory" "$expected" > "$summary" || status=$?
cat "$summary"
[ "$status" -eq 0 ] || { echo "FAIL: P4-21's exit check (exit_summary.txt in the test's outputs)"; exit 1; }
echo "PASS: every non-passing case is marked"
