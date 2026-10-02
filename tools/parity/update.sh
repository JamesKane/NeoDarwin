#!/usr/bin/env bash
# SPDX-License-Identifier: BSD-2-Clause
# bazel run //tools/parity:update and :accept: rewrite inventory.tsv or
# baseline.tsv in the workspace.
#   update.sh update PARITY PROGRAMS CONTENTS
#   update.sh accept PARITY [--regress]
set -euo pipefail
dir="${BUILD_WORKSPACE_DIRECTORY:?run with bazel run}/tools/parity"
mode="$1"; parity="$2"; shift 2
case "$mode" in
update)
	"$parity" update --programs "$1" --contents "$2" --inventory "$dir/inventory.tsv" --out "$dir/inventory.tsv"
	echo "updated $dir/inventory.tsv" ;;
accept)
	"$parity" accept --inventory "$dir/inventory.tsv" --baseline "$dir/baseline.tsv" --out "$dir/baseline.tsv" "$@"
	echo "recorded $dir/baseline.tsv" ;;
esac
