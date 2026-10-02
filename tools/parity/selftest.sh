#!/usr/bin/env bash
# SPDX-License-Identifier: BSD-2-Clause
# parity over testdata/: the walk of a small FreeBSD-shaped tree (SUBDIR
# conditions, Makefile.<arch>, a collecting directory, Makefile.inc chains,
# LINKS, SCRIPTS, a helper subdirectory), then each check passing on the good
# inventory and failing on a broken copy, and accept refusing a regression.
#   selftest.sh PARITY
set -euo pipefail
tool="$1"
d="$(dirname "$0")/testdata"
w="$(mktemp -d)"; trap 'rm -rf "$w"' EXIT
fail() { echo "FAIL: $*"; exit 1; }

"$tool" programs "$d/tree" --lock "$d/test.lock" > "$w/programs.tsv"
grep -q '^# .* at Test-RELEASE, freebsd-src 0123456789ab, from' "$w/programs.tsv" || fail "no pin line"
diff <(grep -v '^#' "$w/programs.tsv" | tail -n +2) "$d/programs.want" || fail "program set"
"$tool" programs "$d/tree" --lock "$d/test.lock" --files > "$w/files"
grep -qx 'sbin/group/Makefile.inc' "$w/files" && grep -qx 'share/mk/src.opts.mk' "$w/files" || fail "--files"

args=(--programs "$w/programs.tsv" --contents "$d/contents.txt" --backlog "$d/backlog.yaml")
check() {  # check WANT(pass|fail) INVENTORY BASELINE CHECK... -- PATTERN
	local want="$1" inv="$2" base="$3"; shift 3
	local checks=(); while [ "$1" != "--" ]; do checks+=("$1"); shift; done; shift
	local rc=0
	"$tool" check "${checks[@]}" "${args[@]}" --inventory "$inv" --baseline "$base" > "$w/out" || rc=$?
	if [ "$want" = pass ]; then [ "$rc" = 0 ] || { cat "$w/out"; fail "${checks[*]} should pass"; }
	else [ "$rc" = 1 ] || { cat "$w/out"; fail "${checks[*]} should fail"; }; fi
	[ -z "${1:-}" ] || grep -qF -- "$1" "$w/out" || { cat "$w/out"; fail "${checks[*]}: no '$1'"; }
}
inv="$d/inventory.tsv"; base="$d/baseline.tsv"
check pass "$inv" "$base" all --

# (a) a missing row, an extra row.
grep -v '^usr.sbin/daemon' "$inv" > "$w/missing.tsv"
check fail "$w/missing.tsv" "$base" coverage -- "usr.sbin/daemon: no row"
{ cat "$inv"; printf 'usr.sbin/zzz\ttodo\t\t\t\tno\t\n'; } > "$w/extra.tsv"
check fail "$w/extra.tsv" "$base" coverage -- "usr.sbin/zzz: not a program directory"
# (b) an unknown status, n/a without a reason, base and unbuilt without a roadmap item.
sed 's|^bin/ls\tapple|bin/ls\tmaybe|' "$inv" > "$w/status.tsv"
check fail "$w/status.tsv" "$base" statuses -- "bin/ls: status 'maybe'"
sed 's|^bin/bar\tn/a\t\t\t\tno\tnot built by default|bin/bar\tn/a\t\t\t\tno\t|' "$inv" > "$w/reason.tsv"
check fail "$w/reason.tsv" "$base" statuses -- "bin/bar: n/a needs the reason"
sed 's|^sbin/foo\tfreebsd\tFreeBSD sbin/foo\t\tP4-21|sbin/foo\tfreebsd\tFreeBSD sbin/foo\t\tP9-99|' "$inv" > "$w/item.tsv"
check fail "$w/item.tsv" "$base" statuses -- "roadmap item P9-99 is not in"
# (c) a row loses its status; a row stops being built; progress without accept.
sed 's|^sbin/foo\tfreebsd|sbin/foo\ttodo|' "$inv" > "$w/lost.tsv"
check fail "$w/lost.tsv" "$base" ratchet -- "sbin/foo: lost its status (freebsd in the baseline, now todo)"
sed 's|^bin/cat\(.*\)\tyes\t$|bin/cat\1\tno\t|' "$inv" > "$w/unbuilt.tsv"
check fail "$w/unbuilt.tsv" "$base" ratchet -- "bin/cat: no longer built"
sed 's|^usr.sbin/daemon\ttodo|usr.sbin/daemon\tfreebsd|' "$inv" > "$w/ahead.tsv"
check fail "$w/ahead.tsv" "$base" ratchet -- "the baseline is behind"
# (d) built claimed without the file, and the file present but not claimed.
sed 's|^bin/ls\(.*\)\tno\t$|bin/ls\1\tyes\t|' "$inv" > "$w/claim.tsv"
check fail "$w/claim.tsv" "$base" built -- "bin/ls: built says yes, but the image lacks ls"
{ cat "$d/contents.txt"; echo usr/sbin/daemon; } > "$w/contents.txt"
"$tool" check built --programs "$w/programs.tsv" --contents "$w/contents.txt" --inventory "$inv" > "$w/out" && fail "unclaimed build passed"
grep -qF "usr.sbin/daemon: built says no, but the image has daemon" "$w/out" || fail "unclaimed build"

# update: built from the image; accept: refuses a regression without --regress.
"$tool" update --programs "$w/programs.tsv" --contents "$w/contents.txt" --inventory "$inv" --out "$w/updated.tsv"
grep -q $'^usr.sbin/daemon\ttodo\t\t\t\tyes\t$' "$w/updated.tsv" || fail "update"
if "$tool" accept --inventory "$w/lost.tsv" --baseline "$base" --out "$w/b.tsv" > "$w/out" 2>&1; then fail "accept took a regression"; fi
"$tool" accept --inventory "$w/lost.tsv" --baseline "$base" --out "$w/b.tsv" --regress
grep -q $'^sbin/foo\ttodo\tno$' "$w/b.tsv" || fail "accept --regress"

# The report counts.
"$tool" report --programs "$w/programs.tsv" --inventory "$inv" --out "$w/coverage.md"
grep -qF 'Coverage (a status other than `todo`): **8/9 (88.9%)**' "$w/coverage.md" || { cat "$w/coverage.md"; fail "report"; }
echo "PASS: walk, checks (a)-(d), update, accept and report"
