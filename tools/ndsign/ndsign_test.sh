#!/usr/bin/env bash
# SPDX-License-Identifier: BSD-2-Clause
# ndsign and nd_package (P2-01): see BUILD.bazel. The test key chain is
# testdata/test_keys (TEST-ONLY).
set -euo pipefail
ndsign="$PWD/$1"; tamper="$PWD/$2"; trustcache="$PWD/$3"; pkg="$PWD/$4"; again="$PWD/$5"; keys="$(dirname "$PWD/$6")"
work="${TEST_TMPDIR:-$(mktemp -d)}/ndsign"; rm -rf "$work"; mkdir -p "$work"; cd "$work"
fails=0
check() { if eval "$2"; then echo "ok   $1"; else echo "FAIL $1"; fails=$((fails + 1)); fi; }
root=(--root "$keys/root.pub")
verify() { "$ndsign" verify "${root[@]}" "$@" 2>&1; }
refused() { local why="$1"; shift; local out; if out="$("$@" 2>&1)"; then echo "  accepted: $out"; return 1; fi; grep -q -- "$why" <<< "$out" || { echo "  $out"; return 1; }; }
tmp_chain() { cat "$keys/channel.cert"; echo; cat "$1"; }
far=4102444800  # the test certificates' expiry, 2100-01-01

# 1. The package nd_package built verifies offline, and reproduces.
out="$(verify "$pkg")"
check "package verifies against the test root" 'grep -q "ok: pciconf 1.0 (aarch64), 2 file(s)" <<< "$out"'
check "signed by the test chain" 'grep -q "signed by release key test-release seq 1, channel test seq 1" <<< "$out"'
check "its trust cache lists pciconf, granted" 'grep -q "trust cache: 1 entries, granted" <<< "$out"'
check "two builds, the same bytes" 'cmp -s "$pkg" "$again"'
"$ndsign" unpack "${root[@]}" "$pkg" unpacked > /dev/null
check "unpacked: the binary, its manual page, the module, the grant" '[ -x unpacked/files/usr/sbin/pciconf ] && [ -f unpacked/files/usr/share/man/man8/pciconf.8 ] && [ -f unpacked/trustcache ] && [ -f unpacked/trustcache.grant ]'
"$ndsign" pack --name pciconf --version 1.0 --license BSD-2-Clause --provides cmd:pciconf --trust-cache unpacked/trustcache \
	--chain "$keys/channel.cert" --chain "$keys/release.cert" --key "$keys/release.key" -o repacked.ndpkg unpacked/files > /dev/null
check "repacked from its contents: the same bytes" 'cmp -s "$pkg" repacked.ndpkg'
check "the grant verifies for the module" '"$ndsign" verify-tc "${root[@]}" unpacked/trustcache unpacked/trustcache.grant | grep -q "grant ok: pciconf 1.0"'

# 2. Wrong keys and bad chains.
check "another root: refused" 'refused "untrusted root" "$ndsign" verify --root "$keys/other-root.pub" "$pkg"'
"$ndsign" keygen intruder > /dev/null
"$ndsign" certify --issuer "$keys/other-root.key" --kind channel --name test --seq 1 --expires $far --scope "*" --usage manifest --usage trust-cache --key "$keys/channel.pub" -o forged-channel.cert
"$ndsign" certify --issuer "$keys/channel.key" --kind release --name test-release --seq 1 --expires $far --scope "*" --usage manifest --usage trust-cache --key "$keys/release.pub" -o release-again.cert
mkdir -p tree/bin; cp "$trustcache" tree/bin/tool
"$trustcache" create tc tree > /dev/null
pack() { "$ndsign" pack --name demo --version 2 --license BSD-2-Clause --trust-cache tc "$@" tree > /dev/null; }
pack --chain forged-channel.cert --chain "$keys/release.cert" --key "$keys/release.key" -o forged.ndpkg
check "a channel certificate from another root: refused" 'refused "untrusted root" "$ndsign" verify "${root[@]}" forged.ndpkg'
"$ndsign" certify --issuer intruder.key --kind release --name test-release --seq 1 --expires $far --scope "*" --usage manifest --usage trust-cache --key intruder.pub -o intruder.cert
pack --chain "$keys/channel.cert" --chain intruder.cert --key intruder.key -o intruder.ndpkg
check "a release certificate the channel didn't issue: refused" 'refused "wrong issuer" "$ndsign" verify "${root[@]}" intruder.ndpkg'
check "signing with a key the chain doesn't certify: refused" 'refused "the key is not the release certificate" pack --chain "$keys/channel.cert" --chain "$keys/release.cert" --key intruder.key -o x.ndpkg'
"$ndsign" certify --issuer "$keys/channel.key" --kind release --name manifests-only --seq 1 --expires $far --scope "*" --usage manifest --key "$keys/release.pub" -o manifests-only.cert
"$ndsign" sign-tc --chain "$keys/channel.cert" --chain manifests-only.cert --key "$keys/release.key" --package demo --version 2 tc -o tc.grant.manifest-only
check "a release key not authorised for trust caches: grant refused" 'refused "not authorised" "$ndsign" verify-tc "${root[@]}" tc tc.grant.manifest-only'
"$ndsign" certify --issuer "$keys/channel.key" --kind release --name scoped --seq 1 --expires $far --scope "net-*" --usage manifest --usage trust-cache --key "$keys/release.pub" -o scoped.cert
pack --chain "$keys/channel.cert" --chain scoped.cert --key "$keys/release.key" -o scoped.ndpkg
check "a package outside the release key's scope: refused" 'refused "out of scope" "$ndsign" verify "${root[@]}" scoped.ndpkg'

# 3. Expiry.
check "valid until the certificates expire" '"$ndsign" verify "${root[@]}" --now $((far - 1)) "$pkg" > /dev/null'
check "expired chain: refused" 'refused "expired" "$ndsign" verify "${root[@]}" --now $far "$pkg"'
"$ndsign" certify --issuer "$keys/channel.key" --kind release --name old --seq 1 --expires 1000000000 --scope "*" --usage manifest --usage trust-cache --key "$keys/release.pub" -o expired.cert
pack --chain "$keys/channel.cert" --chain expired.cert --key "$keys/release.key" -o expired.ndpkg
check "an expired release certificate: refused" 'refused "expired" "$ndsign" verify "${root[@]}" expired.ndpkg'

# 4. Tampering.
"$tamper" "$pkg" files/usr/sbin/pciconf flip t-payload.ndpkg
check "a tampered binary: refused" 'refused "usr/sbin/pciconf: does not match the manifest" verify t-payload.ndpkg'
"$tamper" "$pkg" files/usr/share/man/man8/pciconf.8 flip t-page.ndpkg
check "a tampered manual page: refused" 'refused "pciconf.8: does not match the manifest" verify t-page.ndpkg'
sed 's/version = "1.0"/version = "1.1"/' unpacked/manifest.toml > manifest.edited
"$tamper" "$pkg" manifest.toml manifest.edited t-manifest.ndpkg
check "an edited manifest: refused" 'refused "manifest.toml does not match its signature" verify t-manifest.ndpkg'
"$tamper" "$pkg" trustcache tc t-tc.ndpkg
check "another trust cache: refused" 'refused "does not match the manifest" verify t-tc.ndpkg'
check "a grant for another module: refused" 'refused "wrong kind or grant" "$ndsign" verify-tc "${root[@]}" tc unpacked/trustcache.grant'
sed -E "s/^module-sha256 = \".*\"$/module-sha256 = \"$(shasum -a 256 tc | cut -c1-64)\"/" unpacked/trustcache.grant > grant.edited
check "a grant re-pointed at another module: bad signature" '! cmp -s grant.edited unpacked/trustcache.grant && refused "bad signature" "$ndsign" verify-tc "${root[@]}" tc grant.edited'

# 5. Key rotation: seq and previous against a key ring.
"$ndsign" certify --issuer "$keys/root.key" --kind channel --name test --seq 2 --previous "$keys/channel.cert" --expires $far --scope "*" --usage manifest --usage trust-cache --key "$keys/channel.pub" -o channel-2.cert
check "seq 2 names seq 1 as previous" 'grep -q "previous = \"$(shasum -a 256 "$keys/channel.cert" | cut -c1-64)\"" channel-2.cert'
check "seq 3 needs the seq 2 certificate" 'refused "seq must follow" "$ndsign" certify --issuer "$keys/root.key" --kind channel --name test --seq 3 --previous "$keys/channel.cert" --expires $far --scope "*" --usage manifest --key "$keys/channel.pub" -o x.cert'
mkdir -p ring; cp channel-2.cert ring/
check "a chain superseded by a known certificate: refused" 'refused "seq 1 is superseded by seq 2" "$ndsign" verify "${root[@]}" --keyring ring "$pkg"'
pack --chain channel-2.cert --chain "$keys/release.cert" --key "$keys/release.key" -o rotated.ndpkg
rm ring/*; cp "$keys/channel.cert" ring/
check "the rotated chain verifies against a ring that knows seq 1" '"$ndsign" verify "${root[@]}" --keyring ring rotated.ndpkg | grep -q "channel test seq 2"'
"$ndsign" certify --issuer "$keys/root.key" --kind channel --name test --seq 1 --expires $((far - 10)) --scope "*" --usage manifest --usage trust-cache --key "$keys/channel.pub" -o channel-fork.cert
"$ndsign" certify --issuer "$keys/root.key" --kind channel --name test --seq 2 --previous channel-fork.cert --expires $far --scope "*" --usage manifest --usage trust-cache --key "$keys/channel.pub" -o channel-2-fork.cert
pack --chain channel-2-fork.cert --chain "$keys/release.cert" --key "$keys/release.key" -o fork.ndpkg
check "a seq 2 that doesn't follow the known seq 1: refused" 'refused "does not follow the known seq 1" "$ndsign" verify "${root[@]}" --keyring ring fork.ndpkg'
cp channel-fork.cert ring/fork.cert
check "two different seq 1 certificates: refused" 'refused "conflicts with a known one" "$ndsign" verify "${root[@]}" --keyring ring "$pkg"'

# 6. The archive's form.
check "the archive is a zstd frame" '[ "$(head -c 4 "$pkg" | od -An -tx1 | tr -d " ")" = 28b52ffd ]'
check "a truncated archive: refused" 'head -c 100 "$pkg" > short.ndpkg && refused "zstd" verify short.ndpkg'

echo "$fails failure(s)"
[ "$fails" -eq 0 ]
