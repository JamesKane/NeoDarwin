#!/usr/bin/env bash
# SPDX-License-Identifier: BSD-2-Clause
# Fail if libSystem's reexported libraries (LC_REEXPORT_DYLIB) differ from the
# recorded list. nm -gU (exports_test.sh) lists only libSystem's own symbols;
# everything else it provides comes through these reexports.
#   reexports_test.sh INSTALL_TREE DYLIB_PATH EXPECTED
# Regenerate with
#   otool -l <tree>/usr/lib/libSystem.B.dylib | awk '/cmd LC_REEXPORT_DYLIB/ { getline; getline; print $2 }' |
#     LC_ALL=C sort > base/libsystem/reexports.txt
set -euo pipefail
tree="$1"; dylib="$2"; expected="$3"
actual="$(mktemp)"; trap 'rm -f "$actual"' EXIT
otool -l "$tree/$dylib" | awk '/cmd LC_REEXPORT_DYLIB/ { getline; getline; print $2 }' | LC_ALL=C sort > "$actual"
if ! diff -u "$expected" "$actual"; then
	echo "FAIL: $dylib's reexports differ from $expected"; exit 1
fi
echo "PASS: $dylib reexports $(wc -l < "$actual" | tr -d ' ') libraries, as recorded"
