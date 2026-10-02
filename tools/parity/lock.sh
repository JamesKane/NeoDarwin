#!/usr/bin/env bash
# SPDX-License-Identifier: BSD-2-Clause
# Re-pin tools/parity/freebsd.lock (rules/pinned_files.bzl's format) from a
# FreeBSD checkout at a release tag: the files parity's walk reads (the
# Makefiles of bin, sbin, usr.bin and usr.sbin and their subdirectories,
# Makefile.inc and Makefile.<arch>, share/mk's option lists) with their
# hashes. A sparse checkout of bin, sbin, usr.bin, usr.sbin and share/mk is
# enough. Run through Bazel, which builds parity:
#   bazel run //tools/parity:lock -- CHECKOUT RELEASE
# e.g. RELEASE "15.1-RELEASE (tag release/15.1.0)".
set -euo pipefail
parity="$(cd "$(dirname "$1")" && pwd)/$(basename "$1")"; checkout="$2"; release="$3"
lock="${BUILD_WORKSPACE_DIRECTORY:?run with bazel run}/tools/parity/freebsd.lock"
commit="$(git -C "$checkout" rev-parse HEAD)"
if [ -n "$(git -C "$checkout" status --porcelain)" ]; then echo "$checkout has local changes; pin a clean tree" >&2; exit 1; fi
tmp="$(mktemp)"; trap 'rm -f "$tmp"' EXIT
{
	echo "# FreeBSD's program Makefiles for the parity inventory (tools/parity), fetched unmodified by //rules:pinned_files.bzl."
	echo "# Repository: https://github.com/freebsd/freebsd-src"
	echo "# release: $release"
	echo "# commit: $commit"
	echo "# Format: shasum -a 256 (hash, two spaces, path). Regenerate with bazel run //tools/parity:lock (tools/parity/lock.sh)."
	"$parity" programs "$checkout" --lock /dev/null --files | (cd "$checkout" && xargs shasum -a 256) | sort -k2
} > "$tmp"
mv "$tmp" "$lock"; trap - EXIT
echo "$lock pinned to $commit ($(grep -vc '^#' "$lock") files)"
