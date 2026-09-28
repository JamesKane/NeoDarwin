#!/usr/bin/env bash
# SPDX-License-Identifier: BSD-2-Clause
# Boot an EFI application as \EFI\BOOT\BOOTAA64.EFI on QEMU virt with EDK2 and
# check the serial log.
#   qemu_efi_test.sh EFI_FILE TIMEOUT_SECONDS EXPECTED_LINE...
# Passes when QEMU exits by itself (the application powers off) within the
# timeout and every expected line appears on the serial port.
set -euo pipefail
efi="$1"; timeout="$2"; shift 2
# Bazel runs tests with a minimal PATH, so also look where package managers install QEMU.
qemu="${ND_QEMU:-}"
if [ -z "$qemu" ]; then
	for q in "$(command -v qemu-system-aarch64 || true)" /opt/homebrew/bin/qemu-system-aarch64 /usr/local/bin/qemu-system-aarch64 /usr/bin/qemu-system-aarch64; do
		[ -n "$q" ] && [ -x "$q" ] && { qemu="$q"; break; }
	done
fi
[ -n "$qemu" ] || { echo "qemu-system-aarch64 not found (brew install qemu, or set ND_QEMU)"; exit 1; }
fw=""
for d in "$(dirname "$qemu")/../share/qemu" /opt/homebrew/share/qemu /usr/local/share/qemu /usr/share/qemu; do
	[ -f "$d/edk2-aarch64-code.fd" ] && { fw="$d/edk2-aarch64-code.fd"; break; }
done
[ -n "$fw" ] || { echo "EDK2 firmware edk2-aarch64-code.fd not found next to QEMU"; exit 1; }
work="$(mktemp -d)"; trap 'rm -rf "$work"' EXIT
mkdir -p "$work/esp/EFI/BOOT"; cp "$efi" "$work/esp/EFI/BOOT/BOOTAA64.EFI"
log="$work/serial.log"
status=0
perl -e '
	my ($t, @cmd) = @ARGV;
	my $pid = fork(); if (!$pid) { exec @cmd or die "exec: $!" }
	local $SIG{ALRM} = sub { kill 9, $pid; waitpid($pid, 0); exit 124 };
	alarm $t; waitpid($pid, 0); exit($? >> 8);
' "$timeout" "$qemu" -M virt -cpu cortex-a76 -m 512 -nographic -no-reboot \
	-drive if=pflash,format=raw,readonly=on,file="$fw" \
	-drive format=raw,file=fat:rw:"$work/esp" -serial "file:$log" -monitor none || status=$?
clean="$(tr -d '\033' < "$log" | sed -E 's/\[[0-9;?]*[A-Za-z]//g' | tr '\r' '\n')"
echo "$clean" | grep -a "neoboot" || true
[ "$status" -eq 124 ] && { echo "FAIL: QEMU did not exit within ${timeout}s"; exit 1; }
[ "$status" -eq 0 ] || { echo "FAIL: QEMU exited with status $status"; exit 1; }
for want in "$@"; do
	echo "$clean" | grep -aqF -- "$want" || { echo "FAIL: missing serial line: $want"; exit 1; }
done
echo "PASS: $# expected line(s) on serial; QEMU powered off cleanly"
