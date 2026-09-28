#!/usr/bin/env bash
# SPDX-License-Identifier: BSD-2-Clause
# Kernel job: full XNU build, the link-gap ratchet, the kernel collection and
# a QEMU boot to IOKit (about 12 minutes cold; needs QEMU).
set -euo pipefail
cd "$(dirname "$0")/.."
bazel test --config=ci //kernel:vmapple_link_gap_ratchet //kernel:sbsa_kernel_test //kernel:sbsa_isa_audit //kernel:sbsa_kc_check //kernel:sbsa_boot_test
