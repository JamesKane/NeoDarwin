#!/usr/bin/env bash
# SPDX-License-Identifier: BSD-2-Clause
# trustcache against codesign(1), the reference for cdhashes, and its module
# format: see BUILD.bazel.
set -euo pipefail
tool="$1"; ent="$2"
work="${TEST_TMPDIR:-$(mktemp -d)}/tc"; rm -rf "$work"; mkdir -p "$work/root/bin" "$work/root/lib"
fails=0
check() { if eval "$2"; then echo "ok   $1"; else echo "FAIL $1"; fails=$((fails + 1)); fi; }
reference() { codesign -dvvv "$1" 2>&1 | sed -n 's/^CDHash=//p'; }
ours() { "$tool" cdhash "$1" | awk '{print $1}'; }

# 1. A linker-signed binary: the tool itself.
cp "$tool" "$work/root/bin/linker-signed"
check "linker-signed cdhash matches codesign" '[ "$(ours "$work/root/bin/linker-signed")" = "$(reference "$work/root/bin/linker-signed")" ]'

# 2. The same re-signed ad hoc with entitlements (XML and DER slots).
cp "$tool" "$work/root/bin/entitled"; chmod u+w "$work/root/bin/entitled"
codesign -s - -f -i neodarwin.test.entitled --entitlements "$ent" "$work/root/bin/entitled" 2>/dev/null
check "entitled cdhash matches codesign" '[ "$(ours "$work/root/bin/entitled")" = "$(reference "$work/root/bin/entitled")" ]'
check "entitlements reported" '"$tool" cdhash "$work/root/bin/entitled" | grep -q " entitlements$"'
check "different signatures, different cdhashes" '[ "$(ours "$work/root/bin/entitled")" != "$(ours "$work/root/bin/linker-signed")" ]'

# 3. A module over the root, with a non-Mach-O file and a symbolic link.
echo "not a binary" > "$work/root/lib/readme"
ln -s ../bin/entitled "$work/root/lib/link"
"$tool" create "$work/tc" --manifest "$work/manifest" "$work/root" > "$work/out"
check "two entries" 'grep -q ": 2 entries, UUID " "$work/out"'
check "module is 24 + 2 x 22 bytes" '[ "$(wc -c < "$work/tc" | tr -d " ")" = 68 ]'
check "dump reads it back sorted" '"$tool" dump "$work/tc" | tail -n +2 | awk "{print \$1}" | sort -c'
check "dump lists both cdhashes" '[ "$("$tool" dump "$work/tc" | tail -n +2 | awk "{print \$1}" | sort | tr "\n" " ")" = "$( (ours "$work/root/bin/entitled"; ours "$work/root/bin/linker-signed") | sort | tr "\n" " ")" ]'
types() { "$tool" dump "$work/tc" | tail -n +2 | cut -d" " -f2,3 | sort -u; }
check "hash type 2 (SHA-256), flags 0" '[ "$(types)" = "2 0" ]'
check "manifest names the files, not the link" '[ "$(awk "{print \$2}" "$work/manifest" | sort | tr "\n" " ")" = "bin/entitled bin/linker-signed " ]'
cp "$work/tc" "$work/tc.first"
"$tool" create "$work/tc" "$work/root" > /dev/null
check "same inputs, same bytes (UUID included)" 'cmp -s "$work/tc" "$work/tc.first"'

# 4. A tampered binary is refused, unless excluded.
cp "$tool" "$work/root/bin/tampered"; chmod u+w "$work/root/bin/tampered"
printf '\x00' | dd of="$work/root/bin/tampered" bs=1 seek=20000 conv=notrunc 2>/dev/null
check "tampered binary refused" '! "$tool" create "$work/tc2" "$work/root" 2> "$work/err"'
check "refusal names the file and page" 'grep -q "bin/tampered: code signature does not match the file: page" "$work/err"'
check "excluded, it is left out" '"$tool" create "$work/tc2" --exclude bin/tampered "$work/root" | grep -q ": 2 entries, UUID .*; 1 Mach-O file(s) excluded"'
check "a directory excludes what is under it" '"$tool" create "$work/tc3" --exclude bin "$work/root" | grep -q ": 0 entries, UUID .*; 3 Mach-O file(s) excluded"'

# 5. An unsigned Mach-O is refused.
cp "$tool" "$work/root/lib/unsigned"; chmod u+w "$work/root/lib/unsigned"
codesign --remove-signature "$work/root/lib/unsigned"
check "unsigned binary refused" '! "$tool" create "$work/tc4" --exclude bin/tampered "$work/root" 2> "$work/err" && grep -q "lib/unsigned: no code signature" "$work/err"'

# 6. A corrupt module is refused by dump.
head -c 30 "$work/tc.first" > "$work/short"
check "truncated module refused" '! "$tool" dump "$work/short" 2>/dev/null'

echo "$fails failure(s)"
[ "$fails" = 0 ]
