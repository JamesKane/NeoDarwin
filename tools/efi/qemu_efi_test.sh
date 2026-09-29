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
#   --send-after LINE TEXT
#                     once LINE appears on serial (after the previous step's
#                     match), type TEXT half a second later, as a person
#                     would; "\n" in TEXT is Enter. Steps run in order. The
#                     kernel drops input typed before the console is open, so
#                     LINE should be a prompt
# Environment:
#   ND_QEMU           qemu-system-aarch64 to use
#   ND_QEMU_DEBUG=DIR keep serial.log there and add QEMU's exception trace (-d int)
set -euo pipefail
esp_files=(); mem=512M; smp=1; cpu=cortex-a76; until_lines=0; sends=()
while [ $# -gt 0 ]; do
	case "$1" in
		--esp) esp_files+=("$2"); shift 2 ;;
		--mem) mem="$2"; shift 2 ;;
		--smp) smp="$2"; shift 2 ;;
		--cpu) cpu="$2"; shift 2 ;;
		--until-lines) until_lines=1; shift ;;
		--send-after) sends+=("$2" "$3"); shift 3 ;;
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
# The serial port is a pair of named pipes (QEMU's pipe chardev): QEMU writes
# the guest's output to ser.out and reads its input from ser.in.
steps="$work/sends"; : > "$steps"
i=0; while [ $i -lt ${#sends[@]} ]; do printf '%s\t%s\n' "${sends[$i]}" "${sends[$((i + 1))]}" >> "$steps"; i=$((i + 2)); done
mkfifo "$work/ser.in" "$work/ser.out"
status=0
# Watchdog in perl: QEMU ignores SIGALRM. It copies serial output to the log,
# types each --send-after step once its line has appeared, and in
# --until-lines mode stops QEMU once every expected line is there. QEMU
# stalls if its output isn't drained, so the loop always reads it.
perl -e '
	use Fcntl; use Time::HiRes qw(time);
	my ($t, $until, $log, $pat, $steps, $ser, @cmd) = @ARGV;
	open(my $pf, "<", $pat) or die; my @want = grep { length } map { chomp; $_ } <$pf>;
	open(my $sf, "<", $steps) or die; my @send = map { chomp; [split /\t/, $_, 2] } <$sf>;
	sysopen(my $out, "$ser.out", O_RDWR | O_NONBLOCK) or die "ser.out: $!";
	sysopen(my $in, "$ser.in", O_RDWR) or die "ser.in: $!";
	open(my $lf, ">>", $log) or die; $lf->autoflush(1);
	my ($text, $pos, $due) = ("", 0, undef);
	sub drain { my $buf; while (sysread($out, $buf, 65536)) { print $lf $buf; $buf =~ s/\r//g; $text .= $buf } }
	my $pid = fork(); if (!$pid) { exec @cmd or die "exec: $!" }
	my $deadline = time + $t;
	while (1) {
		drain();
		if (waitpid($pid, 1) > 0) { drain(); exit($? >> 8) }
		if (@send && !defined $due && (my $at = index($text, $send[0][0], $pos)) >= 0) {
			$pos = $at + length($send[0][0]); $due = time + 0.5;
		}
		if (defined $due && time >= $due) {
			(my $keys = $send[0][1]) =~ s/\\n/\r/g;
			syswrite($in, $keys); shift @send; undef $due;
		}
		if ($until && !@send && !grep { index($text, $_) < 0 } @want) { kill 9, $pid; waitpid($pid, 0); exit 0 }
		if (time >= $deadline) { kill 9, $pid; waitpid($pid, 0); exit 124 }
		select(undef, undef, undef, 0.2);
	}
' "$timeout" "$until_lines" "$log" "$patterns" "$steps" "$work/ser" "$qemu" -M virt,gic-version=3 -cpu "$cpu" -smp "$smp" -m "$mem" \
	-nographic -no-reboot \
	-drive if=pflash,format=raw,readonly=on,file="$fw" \
	-drive format=raw,file=fat:rw:"$work/esp" -chardev pipe,id=ser,path="$work/ser" -serial chardev:ser -monitor none \
	${debug[@]+"${debug[@]}"} || status=$?
# The log as a terminal shows it: escape sequences dropped, backspaces
# applied (line editors such as zsh's back up and redraw), lines split.
clean="$(perl -0777 -pe 's/\e\[[0-9;?]*[A-Za-z]//g; 1 while s/[^\x08\n]\x08//; s/\x08//g; tr/\r/\n/' < "$log" | grep -av '^\s*$' || true)"
echo "$clean" | tail -n 60
[ "$status" -eq 124 ] && { echo "FAIL: timed out after ${timeout}s"; }
[ "$status" -ne 0 ] && [ "$status" -ne 124 ] && echo "FAIL: QEMU exited with status $status"
for want in "$@"; do
	echo "$clean" | grep -aqF -- "$want" || { echo "FAIL: missing serial line: $want"; exit 1; }
done
[ "$status" -eq 0 ] || exit 1
echo "PASS: $# expected line(s) on serial"
