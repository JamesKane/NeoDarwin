#!/usr/bin/env bash
# SPDX-License-Identifier: BSD-2-Clause
set -euo pipefail
out="$("$1")"
echo "$out"
[[ "$out" == "neodarwin smoke: 42" ]]
