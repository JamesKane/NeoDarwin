#!/usr/bin/env bash
# SPDX-License-Identifier: BSD-2-Clause
# P0-01: the fragile upstreams resolve from their mirrors under
# github.com/JamesKane (docs/repository.md §1). With an empty repository cache
# and every fragile origin rewritten to an unreachable host by Bazel's
# downloader config, each repository must still fetch, which it can only do
# from its mirror, and pass its sha256.
set -euo pipefail
cd "$(dirname "$0")/.."
tmp="$(mktemp -d)"; trap 'rm -rf "$tmp"' EXIT
cat > "$tmp/downloader.cfg" <<'CFG'
rewrite (www\.leonerd\.org\.uk/.*) blocked.invalid/$1
rewrite (www\.inf\.puc-rio\.br/.*) blocked.invalid/$1
rewrite (github\.com/martanne/.*) blocked.invalid/$1
rewrite (github\.com/ksh93/.*) blocked.invalid/$1
rewrite (github\.com/openzfsonosx/.*) blocked.invalid/$1
rewrite (raw\.githubusercontent\.com/hrs-allbsd/.*) blocked.invalid/$1
CFG
repos=(leonerd_libtermkey puc_rio_lpeg martanne_vis ksh93 openzfs wide_dhcpv6)
for r in "${repos[@]}"; do
	bazel fetch --repository_cache="$tmp/cache" --downloader_config="$tmp/downloader.cfg" --force --repo="@$r" > "$tmp/$r.log" 2>&1 ||
		{ cat "$tmp/$r.log"; echo "mirror_check: FAIL: @$r didn't fetch from its mirror"; exit 1; }
	echo "mirror_check: @$r fetched from its mirror"
done
echo "mirror_check: PASS (${#repos[@]} fragile upstreams)"
