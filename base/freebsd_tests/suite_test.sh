#!/usr/bin/env bash
# SPDX-License-Identifier: BSD-2-Clause
# //kernel:sbsa_freebsd_tests_test (P4-21 checkpoint 7,
# docs/architecture/freebsd-parity.md §2.1): boot //images:freebsd_test_disk
# on QEMU virt, run one shard's directories of FreeBSD's /usr/tests with
# kyua, and hold every test case to the ratchet in expected.tsv. The design
# is //kernel:sbsa_zfs_suite_test's (kexts/zfs/tests/suite_test.sh).
#   suite_test.sh QEMU_EFI_TEST DISK GROUPS EXPECTED [QEMU_OPTION...]
# GROUPS is base/freebsd_tests/groups.txt (DIR SHARD, DIR a program's
# directory under /usr/tests: bin/cat); Bazel's sharding (TEST_SHARD_INDEX)
# picks the shard, or every directory runs when the test isn't sharded
# (KYUA_GROUPS="bin/cat usr.bin/wc" picks directories by hand).
#
# In the guest: kyua test -k /usr/tests/Kyuafile DIR..., then kyua report
# with every result (passed too) into a file, printed on the console
# between KYUA-RESULTS-BEGIN and KYUA-RESULTS-END. A case that hangs is
# killed by kyua at its timeout (ATF's default, 300 s, unless the test
# sets one) and reported broken ("timed out"): KILLED here.
#
# The ratchet (expected.tsv: PROGRAM:CASE RESULT REASON, PROGRAM under
# /usr/tests). RESULT is PASS, XFAIL (an ATF expected failure, which kyua
# counts as good), FAIL, BROKEN, SKIP, KILLED, or FLAKY (any result; give the
# reason). A reason starts with its exit class (structural:, fixable:,
# timing:), which exit_check.sh checks; the ratchet doesn't read reasons.
# The test fails if
#   - a case expected PASS or XFAIL gets anything else (a regression);
#   - a case expected FAIL, BROKEN, SKIP or KILLED now passes (PASS or
#     XFAIL: raise the list);
#   - the run and the list disagree on which cases there are (drift).
# A non-passing case whose result changes between FAIL, BROKEN, SKIP and
# KILLED is reported, not failed. Every run writes its results as
# results.tsv to $TEST_UNDECLARED_OUTPUTS_DIR, in expected.tsv's form, with
# the list's reasons kept (and kyua's reason where the list has none).
set -euo pipefail
qemu_test="$1"; disk="$2"; groups_txt="$3"; expected="$4"; shift 4

shard=""
if [ -n "${TEST_TOTAL_SHARDS:-}" ]; then
	shard="${TEST_SHARD_INDEX:-0}"
	[ -n "${TEST_SHARD_STATUS_FILE:-}" ] && touch "$TEST_SHARD_STATUS_FILE"
fi
if [ -n "${KYUA_GROUPS:-}" ]; then
	groups="$KYUA_GROUPS"
else
	groups="$(awk -v s="$shard" '!/^#/ && NF && (s == "" || $2 == s) { printf "%s ", $1 }' "$groups_txt")"
fi
groups="${groups% }"
[ -n "$groups" ] || { echo "suite_test: no directories for shard $shard"; exit 0; }
echo "suite_test: shard ${shard:-all}: $groups"

out="${TEST_UNDECLARED_OUTPUTS_DIR:-${TEST_TMPDIR:-$(mktemp -d)}}"; mkdir -p "$out"
logdir="${TEST_TMPDIR:-$(mktemp -d)}/qemu"; mkdir -p "$logdir"
timeout="${KYUA_TIMEOUT:-2400}"
# The guest's commands: no shared cache, so xnu reports every exec's
# failed __shared_region_check_np() on the console at its default trace
# level; level 0 keeps the results readable.
run="sysctl -w vm.shared_region_trace_level=0 >/dev/null; cd /tmp && kyua test -r /tmp/kyua.db -k /usr/tests/Kyuafile $groups;"
run="$run kyua report -r /tmp/kyua.db --results-filter passed,skipped,xfail,broken,failed > /tmp/kyua-report.txt 2>&1;"
run="$run echo KYUA-RESULTS-BEGIN; cat /tmp/kyua-report.txt; echo KYUA-RESULTS-END;"
# KYUA_VERBOSE=1 (--test_env=KYUA_VERBOSE): also every failing case's
# output (kyua report --verbose), after the results, for classifying them.
[ -n "${KYUA_VERBOSE:-}" ] && run="$run kyua report --verbose -r /tmp/kyua.db --results-filter broken,failed,skipped;"
run="$run echo kyua-done-\$((6*7))"
status=0
ND_QEMU_LOG_DIR="$logdir" ND_QEMU_STOP_ON='panic(cpu' "$qemu_test" "$@" \
	--dump-cpus-on 'panic(cpu' \
	--disk "$disk" \
	--until-lines \
	--absent 'panic(' \
	--send-after 'login: ' 'root\n' \
	--send-after 'root@localhost ~ # ' "$run\\n" \
	- "$timeout" \
	'KYUA-RESULTS-END' 'kyua-done-42' || status=$?
cp "$logdir/serial.log" "$out/serial.log" 2>/dev/null || true

# The results: the report's lines between the markers, else kyua test's
# progress lines (a kernel message can split a line on the console).
# PROGRAM:CASE  ->  RESULT[: REASON]  [TIME]
clean="$(perl -pe 's/\e\[[0-9;?]*[A-Za-z]//g; tr/\r//d' < "$logdir/serial.log")"
actual="$(printf '%s\n' "$clean" | awk '
	function res(r, why) {
		if (r == "passed") return "PASS"
		if (r == "expected_failure") return "XFAIL"
		if (r == "skipped") return "SKIP"
		if (r == "failed") return "FAIL"
		if (r == "broken") return (why ~ /timed out/ ? "KILLED" : "BROKEN")
		return ""
	}
	function parse(line, into,   m, k, r, why) {
		if (!match(line, /^[^ ]+:[^ ]+  ->  [a-z_]+/)) return
		k = line; sub(/  ->  .*/, "", k)
		r = line; sub(/^[^ ]+  ->  /, "", r); why = r; sub(/[: ].*/, "", r)
		sub(/^[a-z_]+:? ?/, "", why); sub(/ *\[[0-9.]+s\]$/, "", why)
		m = res(r, why); if (m == "") return
		if (into == "r") { R[k] = m; W[k] = why } else if (!(k in L)) { L[k] = m; LW[k] = why }
	}
	/^KYUA-RESULTS-BEGIN/ { blk = 1; next }
	/^KYUA-RESULTS-END/ { blk = 0; next }
	blk { parse($0, "r"); next }
	{ parse($0, "l") }
	END {
		for (k in R) printf "%s\t%s\t%s\n", k, R[k], W[k]
		for (k in L) if (!(k in R)) printf "%s\t%s\t%s\n", k, L[k], LW[k]
	}' | LC_ALL=C sort)"

# The list's entries for these directories.
want="$(awk -F'\t' -v groups=" $groups " '
	!/^#/ && NF { p = $1; sub(/:.*/, "", p); n = split(groups, g, " ")
		for (i = 1; i <= n; i++) if (index(p, g[i] "/") == 1) { print; break } }' "$expected")"

# The comparison (awk: macOS's /bin/bash 3.2 has no associative arrays).
fail=0
printf '%s\n' "$want" > "$logdir/want.tsv"
printf '%s\n' "$actual" > "$logdir/actual.tsv"
awk -F'\t' -v results="$out/results.tsv" '
	function good(r) { return r == "PASS" || r == "XFAIL" }
	FILENAME == ARGV[1] { if (NF) { e[$1] = $2; why[$1] = $3; all[$1] = 1 } next }
	NF { a[$1] = $2; kwhy[$1] = $3; all[$1] = 1 }
	END {
		bad = 0
		n = 0; for (p in all) keys[++n] = p
		for (i = 2; i <= n; i++) { k = keys[i]; for (j = i - 1; j >= 1 && keys[j] > k; j--) keys[j + 1] = keys[j]; keys[j + 1] = k }
		printf "" > results
		for (i = 1; i <= n; i++) {
			p = keys[i]; x = e[p]; y = a[p]
			printf "%s\t%s\t%s\n", p, (y == "" ? "MISSING" : y), (why[p] != "" ? why[p] : kwhy[p]) >> results
			if (x == "") { print "RATCHET: " p ": " y " (" kwhy[p] "), not in expected.tsv (drift)"; bad = 1; continue }
			if (y == "") { print "RATCHET: " p ": expected " x ", did not run (drift)"; bad = 1; continue }
			if (x == "FLAKY" || x == y) continue
			if (good(x) && !good(y)) { print "RATCHET: " p ": expected " x ", got " y " (" kwhy[p] ") (regression)"; bad = 1; continue }
			if (!good(x) && good(y)) { print "RATCHET: " p ": expected " x " (" why[p] "), now " y ": raise expected.tsv"; bad = 1; continue }
			print "RATCHET: " p ": expected " x ", got " y " (accepted: " (good(x) ? "both good" : "both fail") ")"
		}
		exit bad
	}' "$logdir/want.tsv" "$logdir/actual.tsv" || fail=1
counts="$(awk -F'\t' '{ n[$2]++ } END { for (r in n) printf "%s=%d ", r, n[r] }' "$out/results.tsv")"
echo "suite_test: results: $counts(results.tsv in the test's outputs)"
[ "$status" -eq 0 ] || { echo "FAIL: the QEMU run failed (status $status)"; exit 1; }
[ "$fail" -eq 0 ] || { echo "FAIL: the ratchet (base/freebsd_tests/expected.tsv) doesn't hold"; exit 1; }
echo "PASS: shard ${shard:-all}, ratchet holds"
