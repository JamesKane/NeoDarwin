#!/usr/bin/env bash
# SPDX-License-Identifier: BSD-2-Clause
# Boot an EFI application as \EFI\BOOT\BOOTAA64.EFI on QEMU virt with EDK2 and
# check the serial log.
#   qemu_efi_test.sh [OPTIONS] EFI_FILE TIMEOUT_SECONDS EXPECTED_LINE...
# Options:
#   --esp PATH=FILE   also place FILE on the ESP at PATH (e.g. NeoDarwin/kernelcache=...)
#   --mem SIZE        guest RAM (default 512M)
#   --smp N           CPUs (default 1)
#   --cpu MODEL       QEMU CPU (default cortex-a76; the SBSA kernel needs Armv8.4, e.g. neoverse-v1)
#   --until-lines     pass as soon as every expected line has appeared, then stop
#                     QEMU (for a kernel, which never powers off); default is to
#                     require QEMU to exit by itself within the timeout
# Environment:
#   ND_QEMU           qemu-system-aarch64 to use
#   ND_QEMU_DEBUG=DIR keep serial.log there and add QEMU's exception trace (-d int)
set -euo pipefail
esp_files=(); mem=512M; smp=1; cpu=cortex-a76; until_lines=0
while [ $# -gt 0 ]; do
	case "$1" in
		--esp) esp_files+=("$2"); shift 2 ;;
		--mem) mem="$2"; shift 2 ;;
		--smp) smp="$2"; shift 2 ;;
		--cpu) cpu="$2"; shift 2 ;;
		--until-lines) until_lines=1; shift ;;
		*) break ;;
	esac
done
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
for spec in ${esp_files[@]+"${esp_files[@]}"}; do
	dest="$work/esp/${spec%%=*}"; mkdir -p "$(dirname "$dest")"; cp "${spec#*=}" "$dest"
done
logdir="$work"; debug=()
if [ -n "${ND_QEMU_DEBUG:-}" ]; then
	logdir="$ND_QEMU_DEBUG"; mkdir -p "$logdir"; debug=(-d int,guest_errors,unimp -D "$logdir/qemu.log")
fi
log="$logdir/serial.log"; : > "$log"
patterns="$work/expect"; printf '%s\n' "$@" > "$patterns"
status=0
# Watchdog in perl: QEMU ignores SIGALRM. In --until-lines mode it also polls
# the serial log and stops QEMU once every expected line is there.
perl -e '
	my ($t, $until, $log, $pat, @cmd) = @ARGV;
	open(my $pf, "<", $pat) or die; my @want = grep { length } map { chomp; $_ } <$pf>;
	my $pid = fork(); if (!$pid) { exec @cmd or die "exec: $!" }
	my $deadline = time + $t;
	while (1) {
		if (waitpid($pid, 1) > 0) { exit($? >> 8) }
		if ($until && open(my $lf, "<", $log)) {
			local $/; my $text = <$lf>; $text =~ s/\r//g;
			if (!grep { index($text, $_) < 0 } @want) { kill 9, $pid; waitpid($pid, 0); exit 0 }
		}
		if (time >= $deadline) { kill 9, $pid; waitpid($pid, 0); exit 124 }
		select(undef, undef, undef, 0.2);
	}
' "$timeout" "$until_lines" "$log" "$patterns" "$qemu" -M virt,gic-version=3 -cpu "$cpu" -smp "$smp" -m "$mem" \
	-nographic -no-reboot \
	-drive if=pflash,format=raw,readonly=on,file="$fw" \
	-drive format=raw,file=fat:rw:"$work/esp" -serial "file:$log" -monitor none ${debug[@]+"${debug[@]}"} || status=$?
clean="$(tr -d '\033' < "$log" | sed -E 's/\[[0-9;?]*[A-Za-z]//g' | tr '\r' '\n' | grep -av '^\s*$' || true)"
echo "$clean" | tail -n 60
[ "$status" -eq 124 ] && { echo "FAIL: timed out after ${timeout}s"; }
[ "$status" -ne 0 ] && [ "$status" -ne 124 ] && echo "FAIL: QEMU exited with status $status"
for want in "$@"; do
	echo "$clean" | grep -aqF -- "$want" || { echo "FAIL: missing serial line: $want"; exit 1; }
done
[ "$status" -eq 0 ] || exit 1
echo "PASS: $# expected line(s) on serial"
