#!/usr/bin/env bash
# SPDX-License-Identifier: BSD-2-Clause
# CoreFoundation's host check (docs/base/corefoundation.md): cftest.c built
# against the framework's headers and linked against a copy of its binary
# whose install name is next to the program, so macOS's own CoreFoundation
# (in the shared cache) stays out of it; the copy runs under the host's
# libSystem, whose install names are the base's.
#   host_test.sh CF_TREE CFTEST_C
set -euo pipefail
tree="$1"; src="$2"
t="$(mktemp -d)"; trap 'rm -rf "$t"' EXIT
cp "$tree/System/Library/Frameworks/CoreFoundation.framework/Versions/A/CoreFoundation" "$t/CoreFoundation"
chmod u+w "$t/CoreFoundation"
xcrun install_name_tool -id @executable_path/CoreFoundation "$t/CoreFoundation" 2>/dev/null
codesign -f -s - "$t/CoreFoundation" 2>/dev/null
xcrun clang -arch arm64 -Werror -F "$tree/usr/local/frameworks" "$src" "$t/CoreFoundation" -o "$t/cftest"
out="$("$t/cftest")"
echo "$out"
[ "$out" = "cftest: ok" ]
otool -L "$t/cftest" | grep -q '@executable_path/CoreFoundation'
