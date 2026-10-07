#!/usr/bin/env bash
# SPDX-License-Identifier: BSD-2-Clause
# Runs a hello-world binary and checks its output.
#   hello_test.sh BINARY EXPECTED
set -euo pipefail
out="$("$1")"
echo "$out"
[ "$out" = "$2" ] || { echo "hello_test: expected '$2'" >&2; exit 1; }
