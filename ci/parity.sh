#!/usr/bin/env bash
# SPDX-License-Identifier: BSD-2-Clause
# Parity job (P4-20, docs/architecture/freebsd-parity.md §2): the inventory's
# checks, including the status ratchet, then the coverage report, copied to
# $PARITY_OUT (default ci-out) for the workflow to publish as an artifact and
# a job summary (P0-05).
set -euo pipefail
cd "$(dirname "$0")/.."
bazel test --config=ci //tools/parity:all
bazel build --config=ci //tools/parity:coverage
out="${PARITY_OUT:-ci-out}"
mkdir -p "$out"
cp -f "$(bazel cquery --config=ci --output=files //tools/parity:coverage 2>/dev/null)" "$out/parity-coverage.md"
if [ -n "${GITHUB_STEP_SUMMARY:-}" ]; then cat "$out/parity-coverage.md" >> "$GITHUB_STEP_SUMMARY"; fi
echo "coverage report: $out/parity-coverage.md"
