#!/bin/sh
# SPDX-License-Identifier: BSD-2-Clause
#
# Content hash of a macOS SDK tree (P0-02, toolchains/README.md): the SHA-256
# of a manifest with one line per regular file or symlink, sorted by path in
# the C locale:
#   f <sha256 of contents> <path>     regular file
#   l <link target> <path>            symlink (not followed)
# Directories, modes and timestamps aren't hashed. Uses only base-system tools
# (find, xargs, shasum, readlink, sort), so it works with no developer tools.
#   sdk_hash.sh SDK_DIR [MANIFEST_OUT]
set -eu
sdk=$1
out=${2:-}
case "$out" in ""|/*) ;; *) out=$PWD/$out ;; esac
export LC_ALL=C
tmp=$(mktemp "${TMPDIR:-/tmp}/nd_sdk_manifest.XXXXXX")
trap 'rm -f "$tmp"' EXIT
cd "$sdk"
{
    find . -type f -print0 | xargs -0 /usr/bin/shasum -a 256 | sed 's|^\([0-9a-f]*\)  \./|f \1 |'
    find . -type l -print | sed 's|^\./||' | while IFS= read -r p; do
        printf 'l %s %s\n' "$(readlink "$p")" "$p"
    done
} | sort -t ' ' -k3 > "$tmp"
if [ -n "$out" ]; then cp "$tmp" "$out"; fi
/usr/bin/shasum -a 256 "$tmp" | cut -d' ' -f1
