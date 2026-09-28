#!/usr/bin/env bash
# SPDX-License-Identifier: BSD-2-Clause
# Re-pin a lock file (see rules/pinned_files.bzl) against a local checkout:
# rewrites the commit line to the checkout's HEAD and every hash from the
# checkout's files. The set of paths stays as listed; edit it by hand.
#   lock.sh CHECKOUT LOCKFILE
set -euo pipefail
checkout="$1"; lock="$2"
commit="$(git -C "$checkout" rev-parse HEAD)"
if [ -n "$(git -C "$checkout" status --porcelain)" ]; then echo "$checkout has local changes; pin a clean tree" >&2; exit 1; fi
tmp="$(mktemp)"
grep '^#' "$lock" | sed -E "s|^# commit: .*|# commit: $commit|" > "$tmp"
grep -v '^#' "$lock" | awk '{print $2}' | (cd "$checkout" && xargs shasum -a 256) | sort -k2 >> "$tmp"
mv "$tmp" "$lock"
echo "$lock pinned to $commit"
