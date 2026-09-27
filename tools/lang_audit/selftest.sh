#!/usr/bin/env bash
# SPDX-License-Identifier: BSD-2-Clause
# The audit must pass a justified file and fail each kind of violation.
set -uo pipefail
tool="$1"; dir="$2"
fail=0
"$tool" "$dir/good.c" >/dev/null || { echo "good.c should pass"; fail=1; }
"$tool" "$dir/missing.c" >/dev/null && { echo "missing.c should fail"; fail=1; }
"$tool" "$dir/bad_ground.c" >/dev/null && { echo "bad_ground.c should fail"; fail=1; }
[[ $fail -eq 0 ]] && echo "lang_audit selftest: ok"
exit $fail
