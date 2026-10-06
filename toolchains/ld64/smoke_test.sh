#!/usr/bin/env bash
# SPDX-License-Identifier: BSD-2-Clause
# The from-source ld64 runs: -v names the pinned version, a static arm64
# program links with an LC_UNIXTHREAD entry (what ld64.lld can't do), and a
# .tbd input fails with the no-libtapi error.
#   smoke_test.sh LD
set -euo pipefail
LD="$1"
T="${TEST_TMPDIR:-$(mktemp -d)}"
v="$("$LD" -v 2>&1)"
grep -q '^@(#)PROGRAM:ld  PROJECT:ld64-957\.1$' <<< "$v" || { echo "unexpected -v: $v" >&2; exit 1; }
printf 'int x = 3;\nint start(void) { return x; }\n' > "$T/t.c"
xcrun clang -arch arm64 -mcpu=cortex-a76 -mmacosx-version-min=26.0 -c "$T/t.c" -o "$T/t.o"
"$LD" -arch arm64 -static -e _start -platform_version macos 26.0 26.0 "$T/t.o" -o "$T/t"
lc="$(otool -l "$T/t")"
grep -q 'cmd LC_UNIXTHREAD' <<< "$lc" || { echo "no LC_UNIXTHREAD" >&2; exit 1; }
if out="$("$LD" -arch arm64 -platform_version macos 26.0 26.0 "$T/t.o" "$(xcrun --show-sdk-path)/usr/lib/libSystem.tbd" -o "$T/u" 2>&1)"; then
	echo "a .tbd input linked" >&2; exit 1
fi
grep -q 'built without libtapi' <<< "$out" || { echo "unexpected .tbd error: $out" >&2; exit 1; }
echo "ld64 smoke test: ok ($(head -1 <<< "$v"))"
