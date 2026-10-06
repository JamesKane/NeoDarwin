#!/usr/bin/env bash
# SPDX-License-Identifier: BSD-2-Clause
# The audit must pass a justified file and fail each kind of violation.
set -uo pipefail
tool="$1"; dir="$2"
fail=0
"$tool" "$dir/good.c" >/dev/null || { echo "good.c should pass"; fail=1; }
"$tool" "$dir/missing.c" >/dev/null && { echo "missing.c should fail"; fail=1; }
"$tool" "$dir/bad_ground.c" >/dev/null && { echo "bad_ground.c should fail"; fail=1; }
# --tree: a tree with one unjustified file fails, naming exactly that file;
# docs/ and testdata/ directories are skipped.
out="$("$tool" --tree "$dir/tree")" && { echo "--tree with an unjustified file should fail"; fail=1; }
grep -q "^FAIL  missing .*src/unjustified.c$" <<< "$out" || { echo "--tree should name src/unjustified.c:"; echo "$out"; fail=1; }
grep -q "^2 file(s), 1 failure(s)$" <<< "$out" || { echo "--tree should audit 2 files (docs/, testdata/ skipped):"; echo "$out"; fail=1; }
[[ $fail -eq 0 ]] && echo "lang_audit selftest: ok"
exit $fail
