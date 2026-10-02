#!/usr/bin/env bash
# SPDX-License-Identifier: BSD-2-Clause
# //kernel:sbsa_zfs_suite_test (P3-01 checkpoint 2, docs/architecture/filesystems.md §7):
# boot //images:zfs_test_disk on QEMU virt with four blank virtio-blk disks
# (a 64 GB one for FILEDIR, HFS+ file vdevs being allocated, and three 4 GB DISKS;
# sparse on the host),
# run one shard's groups of the OpenZFS test suite with nd-zfs-tests, and
# hold the results to the ratchet in expected.tsv.
#   suite_test.sh QEMU_EFI_TEST DISK GROUPS EXPECTED [QEMU_OPTION...]
# GROUPS is kexts/zfs/tests/groups.txt (GROUP SHARD); Bazel's sharding
# (TEST_SHARD_INDEX) picks the shard, or every group runs when the test
# isn't sharded (ZTS_GROUPS="g1 g2" picks groups by hand).
#
# The ratchet (expected.tsv: PATH RESULT REASON, PATH under
# tests/functional/): the test fails if
#   - a test expected to PASS doesn't (a regression);
#   - a test expected to FAIL, SKIP or KILLED now passes (the list must be
#     raised: edit expected.tsv);
#   - the run and the list disagree on which tests there are (drift).
# A non-passing test whose result changes between FAIL, SKIP and KILLED is
# reported, not failed. FLAKY accepts any result (give the reason).
# Every run writes its results as results.tsv to
# $TEST_UNDECLARED_OUTPUTS_DIR, in expected.tsv's form, with the list's
# reasons kept.
set -euo pipefail
qemu_test="$1"; disk="$2"; groups_txt="$3"; expected="$4"; shift 4

shard=""
if [ -n "${TEST_TOTAL_SHARDS:-}" ]; then
	shard="${TEST_SHARD_INDEX:-0}"
	[ -n "${TEST_SHARD_STATUS_FILE:-}" ] && touch "$TEST_SHARD_STATUS_FILE"
fi
if [ -n "${ZTS_GROUPS:-}" ]; then
	groups="$ZTS_GROUPS"
else
	groups="$(awk -v s="$shard" '!/^#/ && NF && (s == "" || $2 == s) { printf "%s ", $1 }' "$groups_txt")"
fi
groups="${groups% }"
[ -n "$groups" ] || { echo "suite_test: no groups for shard $shard"; exit 0; }
echo "suite_test: shard ${shard:-all}: $groups"

out="${TEST_UNDECLARED_OUTPUTS_DIR:-${TEST_TMPDIR:-$(mktemp -d)}}"; mkdir -p "$out"
logdir="${TEST_TMPDIR:-$(mktemp -d)}/qemu"; mkdir -p "$logdir"
timeout="${ZTS_TIMEOUT:-3300}"
cap="${ZTS_TEST_TIMEOUT:-240}"
status=0
# A panic ends the run at once, with every CPU's frame chain in cpus.txt.
ND_QEMU_LOG_DIR="$logdir" ND_QEMU_STOP_ON='panic(cpu' "$qemu_test" "$@" \
	--dump-cpus-on 'panic(cpu' \
	--disk "$disk" \
	--drive zts0=64G --device virtio-blk-pci,drive=zts0,disable-legacy=on \
	--drive zts1=4G --device virtio-blk-pci,drive=zts1,disable-legacy=on \
	--drive zts2=4G --device virtio-blk-pci,drive=zts2,disable-legacy=on \
	--drive zts3=4G --device virtio-blk-pci,drive=zts3,disable-legacy=on \
	--until-lines \
	--absent 'panic(' \
	--send-after 'login: ' 'root\n' \
	--send-after 'root@localhost ~ # ' "/usr/share/zfs/nd-zfs-tests -t $cap $groups\\n" \
	- "$timeout" \
	'ZTS: tests/functional/' 'ZTS-RESULTS-END ' 'zts-done-' || status=$?
cp "$logdir/serial.log" "$out/serial.log" 2>/dev/null || true

# The results: the final block, else the per-test lines (a kernel message
# can split a line on the console).
clean="$(perl -pe 's/\e\[[0-9;?]*[A-Za-z]//g; tr/\r//d' < "$logdir/serial.log")"
actual="$(printf '%s\n' "$clean" | awk '
	/^ZTS-RESULTS-BEGIN/ { blk = 1; next }
	/^ZTS-RESULTS-END/ { blk = 0; next }
	blk && $1 == "ZTS-R" && NF == 3 { r[$2] = $3; next }
	match($0, /ZTS: tests\/functional\/[^ ]+ \[[A-Z]+\]/) {
		s = substr($0, RSTART + 5, RLENGTH - 5); split(s, a, " ")
		res = a[2]; gsub(/[][]/, "", res); if (!(a[1] in r)) l[a[1]] = res
	}
	END { for (p in r) print p, r[p]; for (p in l) if (!(p in r)) print p, l[p] }' |
	sed 's|^tests/functional/||' | LC_ALL=C sort)"

# The list's entries for these groups.
want="$(awk -F'\t' -v groups=" $groups " '
	!/^#/ && NF { g = $1; sub(/^cli_root\//, "", g); sub(/\/.*/, "", g); if (index(groups, " " g " ")) print }' "$expected")"

# The comparison (awk: macOS's /bin/bash 3.2 has no associative arrays).
fail=0
printf '%s\n' "$want" > "$logdir/want.tsv"
printf '%s\n' "$actual" > "$logdir/actual.txt"
awk -F'\t' -v results="$out/results.tsv" '
	FILENAME == ARGV[1] { if (NF) { e[$1] = $2; why[$1] = $3; all[$1] = 1 } next }
	{ split($0, f, " "); if (f[1] != "") { a[f[1]] = f[2]; all[f[1]] = 1 } }
	END {
		bad = 0
		n = 0; for (p in all) keys[++n] = p
		for (i = 2; i <= n; i++) { k = keys[i]; for (j = i - 1; j >= 1 && keys[j] > k; j--) keys[j + 1] = keys[j]; keys[j + 1] = k }
		printf "" > results
		for (i = 1; i <= n; i++) {
			p = keys[i]; x = e[p]; y = a[p]
			printf "%s\t%s\t%s\n", p, (y == "" ? "MISSING" : y), why[p] >> results
			if (x == "") { print "RATCHET: " p ": " y ", not in expected.tsv (drift)"; bad = 1; continue }
			if (y == "") { print "RATCHET: " p ": expected " x ", did not run (drift)"; bad = 1; continue }
			if (x == "FLAKY" || (x == "PASS" && y == "PASS")) continue
			if (x == "PASS") { print "RATCHET: " p ": expected PASS, got " y " (regression)"; bad = 1; continue }
			if (y == "PASS") { print "RATCHET: " p ": expected " x " (" why[p] "), now PASS: raise expected.tsv"; bad = 1; continue }
			if (x != y) print "RATCHET: " p ": expected " x ", got " y " (accepted: both fail)"
		}
		exit bad
	}' "$logdir/want.tsv" "$logdir/actual.txt" || fail=1
counts="$(awk -F'\t' '{ n[$2]++ } END { for (r in n) printf "%s=%d ", r, n[r] }' "$out/results.tsv")"
echo "suite_test: results: $counts(results.tsv in the test's outputs)"
[ "$status" -eq 0 ] || { echo "FAIL: the QEMU run failed (status $status)"; exit 1; }
[ "$fail" -eq 0 ] || { echo "FAIL: the ratchet (kexts/zfs/tests/expected.tsv) doesn't hold"; exit 1; }
echo "PASS: shard ${shard:-all}, ratchet holds"
