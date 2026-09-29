#!/usr/bin/env bash
# SPDX-License-Identifier: BSD-2-Clause
# Boot the same disk image twice, checking each run's serial log, so the
# second boot sees what the first wrote (docs/kernel/storage.md).
#   qemu_disk_reboot_test.sh IMAGE FIRST_RUN_ARGS... --reboot SECOND_RUN_ARGS...
# Each run's arguments are qemu_efi_test.sh's (options, EFI_FILE, TIMEOUT,
# EXPECTED_LINE...), without --disk: both runs boot one copy of IMAGE,
# through --disk-in-place, made in the test's temporary directory.
set -euo pipefail
# The harness beside this script; in a Bazel test, in the runfiles (the
# test's executable is a copy named after the test).
harness="$(dirname "$0")/qemu_efi_test.sh"
[ -n "${TEST_SRCDIR:-}" ] && harness="$TEST_SRCDIR/_main/tools/efi/qemu_efi_test.sh"
image="$1"; shift
first=(); while [ $# -gt 0 ] && [ "$1" != "--reboot" ]; do first+=("$1"); shift; done
[ "${1:-}" = "--reboot" ] || { echo "qemu_disk_reboot_test.sh: no --reboot"; exit 2; }
shift
work="$(mktemp -d "${TEST_TMPDIR:-/tmp}/disk.XXXXXX")"; trap 'rm -rf "$work"' EXIT
cp "$image" "$work/disk.img"; chmod u+w "$work/disk.img"
echo "=== first boot"
bash "$harness" --disk-in-place "$work/disk.img" "${first[@]}"
echo "=== second boot"
bash "$harness" --disk-in-place "$work/disk.img" "$@"
