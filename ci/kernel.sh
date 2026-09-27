#!/usr/bin/env bash
# SPDX-License-Identifier: BSD-2-Clause
# Kernel job: full XNU build and the link-gap ratchet (about 10 minutes cold).
set -euo pipefail
cd "$(dirname "$0")/.."
bazel test --config=ci //kernel:vmapple_link_gap_ratchet //kernel:sbsa_kernel_test //kernel:sbsa_sysreg_audit
