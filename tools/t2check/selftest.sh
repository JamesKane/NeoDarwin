#!/usr/bin/env bash
# SPDX-License-Identifier: BSD-2-Clause
# The T2 gate must pass an allocation-free module and fail each violation
# with its diagnostic:  selftest.sh T2CHECK EMBEDDED_TOOLCHAIN TESTDATA_DIR
set -uo pipefail
check="$1"; tc="$2"; dir="$3"
fail=0
expect_fail() { # fixture, expected diagnostic substring
	local out
	if out="$("$check" "$tc" Fixture "$dir/$1" 2>&1)"; then
		echo "$1: should fail the T2 gate"; fail=1
	elif ! grep -qF -- "$2" <<< "$out"; then
		echo "$1: failed without the expected diagnostic '$2':"; echo "$out"; fail=1
	else
		echo "$1: fails as expected ($2)"
	fi
}
"$check" "$tc" Fixture "$dir/good.swift" || { echo "good.swift should pass"; fail=1; }
expect_fail annotated_alloc.swift "can cause locking"
expect_fail stray_alloc.swift "in -no-allocations mode"
expect_fail unannotated.swift "T2 entry point without @_noLocks"
expect_fail class_on_path.swift "can cause metadata allocation or locks"
[[ $fail -eq 0 ]] && echo "t2check selftest: ok"
exit $fail
