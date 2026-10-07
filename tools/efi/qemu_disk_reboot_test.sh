#!/usr/bin/env bash
# SPDX-License-Identifier: BSD-2-Clause
# Boot the same disk image two or more times, checking each run's serial
# log, so each boot sees what the ones before wrote (docs/kernel/storage.md;
# boot environments, docs/architecture/filesystems.md §8).
#   qemu_disk_reboot_test.sh IMAGE FIRST_RUN_ARGS... --reboot SECOND_RUN_ARGS... [--reboot MORE_RUN_ARGS...]...
# Each run's arguments are qemu_efi_test.sh's (options, EFI_FILE, TIMEOUT,
# EXPECTED_LINE...), without --disk: every run boots one copy of IMAGE,
# through --disk-in-place, made in the test's temporary directory.
set -euo pipefail
# The harness beside this script; in a Bazel test, in the runfiles (the
# test's executable is a copy named after the test).
harness="$(dirname "$0")/qemu_efi_test.sh"
[ -n "${TEST_SRCDIR:-}" ] && harness="$TEST_SRCDIR/_main/tools/efi/qemu_efi_test.sh"
image="$1"; shift
work="$(mktemp -d "${TEST_TMPDIR:-/tmp}/disk.XXXXXX")"; trap 'rm -rf "$work"' EXIT
cp "$image" "$work/disk.img"; chmod u+w "$work/disk.img"
names=(first second third fourth fifth sixth seventh eighth)
boot() {
	echo "=== ${names[$1]:-next} boot"
	bash "$harness" --disk-in-place "$work/disk.img" "${@:2}"
}
n=0; run=()
for arg in "$@"; do
	if [ "$arg" = "--reboot" ]; then
		boot "$n" ${run[@]+"${run[@]}"}; n=$((n + 1)); run=()
	else
		run+=("$arg")
	fi
done
[ "$n" -ge 1 ] || { echo "qemu_disk_reboot_test.sh: no --reboot"; exit 2; }
boot "$n" ${run[@]+"${run[@]}"}
