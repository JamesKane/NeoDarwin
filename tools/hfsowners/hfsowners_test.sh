#!/usr/bin/env bash
# SPDX-License-Identifier: BSD-2-Clause
# hfsowners over a small volume from mkhfs.sh: everything root:wheel, the
# setuid mode hdiutil drops restored, one path given to another owner.
#   hfsowners_test.sh MKHFS HFSOWNERS
set -euo pipefail
mkhfs="$1"; tool="$2"
w="$(mktemp -d)"; trap 'rm -rf "$w"' EXIT
mkdir -p "$w/tree/usr/bin"; printf 'x' > "$w/tree/usr/bin/su"
"$mkhfs" "$w/v.hfs" Test --dir Users/test --tree "$w/tree" --mode usr/bin/su 4755 \
	--owners "$tool" --owner Users/test 501:20
"$tool" list "$w/v.hfs" > "$w/list"
cat "$w/list"
want() { grep -qxF "$1" "$w/list" || { echo "FAIL: no line: $1"; exit 1; }; }
want "0:0 104755 /usr/bin/su"
want "501:20 40755 /Users/test"
want "0:0 40755 /usr"
! grep -v '^0:0 \|^501:20 40755 /Users/test$' "$w/list" || { echo "FAIL: a path not root:wheel"; exit 1; }
echo "PASS: root:wheel, setuid kept, one owner override"
