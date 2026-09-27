#!/usr/bin/env bash
# SPDX-License-Identifier: BSD-2-Clause
# PR gate: build and test everything, then audit the whole tree for
# unjustified C-family files (language policy T4).
set -euo pipefail
cd "$(dirname "$0")/.."
bazel test --config=ci //...
bazel run --config=ci //tools/lang_audit -- --tree "$PWD"
