#!/usr/bin/env bash
# SPDX-License-Identifier: BSD-2-Clause
# Test wrapper: run parity (the first argument) with the rest.
set -euo pipefail
tool="$1"; shift
exec "$tool" "$@"
