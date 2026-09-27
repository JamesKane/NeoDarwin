#!/usr/bin/env bash
# SPDX-License-Identifier: BSD-2-Clause
# Test wrapper: run the lang_audit binary (first argument) over the rest.
set -euo pipefail
tool="$1"; shift
exec "$tool" "$@"
