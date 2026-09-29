#!/usr/bin/env bash
# SPDX-License-Identifier: BSD-2-Clause
# Fail if a base library's exported symbols differ from its recorded list.
#   exports_test.sh INSTALL_TREE DYLIB_PATH EXPECTED
# The list is the ratchet: an export appears or disappears only with a change
# to the list in the same commit. Regenerate with
#   nm -gUj <tree>/<dylib> | LC_ALL=C sort > base/<project>/exports.txt
set -euo pipefail
tree="$1"; dylib="$2"; expected="$3"
actual="$(mktemp)"; trap 'rm -f "$actual"' EXIT
nm -gUj "$tree/$dylib" | LC_ALL=C sort > "$actual"
if ! diff -u "$expected" "$actual"; then
	echo "FAIL: $dylib's exports differ from $expected"; exit 1
fi
echo "PASS: $dylib exports $(wc -l < "$actual" | tr -d ' ') symbols, as recorded"
