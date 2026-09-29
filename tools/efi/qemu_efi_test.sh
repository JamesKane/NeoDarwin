#!/usr/bin/env bash
# SPDX-License-Identifier: BSD-2-Clause
# Boot an EFI application as \EFI\BOOT\BOOTAA64.EFI on QEMU virt with EDK2 (and,
# with --machine virt-secure, TF-A at EL3) and check the serial log.
#   qemu_efi_test.sh [OPTIONS] EFI_FILE TIMEOUT_SECONDS EXPECTED_LINE...
# Options:
#   --esp PATH=FILE   also place FILE on the ESP at PATH (e.g. NeoDarwin/kernelcache=...)
#   --mem SIZE        guest RAM (default 512M; 1G for virt-secure)
#   --smp N           CPUs (default 1)
#   --cpu MODEL       QEMU CPU (default cortex-a76, Armv8.2: the SBSA kernel's baseline)
#   --machine KIND    virt (default): QEMU virt without EL3, EDK2 from QEMU's
#                     share/qemu, one GIC security state (GICD_CTLR.DS=1).
#                     virt-secure: virt,secure=on with TrustZone firmware at
#                     EL3 (--firmware, //third_party/qemu_firmware:virt_secure_flash)
#                     and the GIC's two security states (DS=0), as on SBSA
#                     boards; RAM defaults to 1G, since TF-A loads BL33 at
#                     0x60000000 (docs/kernel/qemu-secure.md)
#   --firmware FILE   virt: EDK2 code flash to use instead of QEMU's;
#                     virt-secure: the secure flash image (BL1 + FIP), required
#   --until-lines     pass as soon as every expected line has appeared, then stop
#                     QEMU (for a kernel, which never powers off); default is to
#                     require QEMU to exit by itself within the timeout
#   --send-after LINE TEXT
#                     once LINE appears on serial (after the previous step's
#                     match), type TEXT half a second later, as a person
#                     would; "\n" in TEXT is Enter. Steps run in order. The
#                     kernel drops input typed before the console is open, so
#                     LINE should be a prompt
#   --device DEV      add a QEMU device, e.g. ramfb (a GOP framebuffer under EDK2)
#   --drive ID=IMAGE  add a raw block backend named ID for a --device to use
#                     (drive=ID), e.g. --drive disk0=16M --device
#                     virtio-blk-pci,drive=disk0. IMAGE is a size (a blank
#                     sparse image of that many K, M or G bytes) or a file,
#                     which is copied first: the guest writes to the copy
#   --screendump NAME once the run has passed (or timed out), save the display
#                     as a PPM through QEMU's monitor, as NAME in
#                     $TEST_UNDECLARED_OUTPUTS_DIR (or ND_QEMU_DEBUG's directory)
#   --screen-font FILE
#                     xnu's osfmk/console/iso_font.c: read the screendump's
#                     text back, cell by 8x16 cell, and print it
#   --screen-line TEXT
#                     require TEXT on one line of the screen (needs the two
#                     options above); repeatable
#   --until-screen    with --until-lines, also wait for every --screen-line,
#                     taking a screendump every few seconds, before stopping
#                     QEMU: for a kernel whose console is the framebuffer alone,
#                     which prints nothing on serial
#   --absent TEXT     fail if TEXT appears on serial; repeatable
# Environment:
#   ND_QEMU           qemu-system-aarch64 to use
#   ND_QEMU_DEBUG=DIR keep serial.log there and add QEMU's exception trace (-d int)
set -euo pipefail
esp_files=(); mem=""; smp=1; cpu=cortex-a76; until_lines=0; sends=(); machine=virt; firmware=""
devices=(); drives=(); screendump=""; screen_font=""; screen_lines=(); until_screen=0; absent=()
while [ $# -gt 0 ]; do
	case "$1" in
		--esp) esp_files+=("$2"); shift 2 ;;
		--mem) mem="$2"; shift 2 ;;
		--smp) smp="$2"; shift 2 ;;
		--cpu) cpu="$2"; shift 2 ;;
		--machine) machine="$2"; shift 2 ;;
		--firmware) firmware="$2"; shift 2 ;;
		--until-lines) until_lines=1; shift ;;
		--send-after) sends+=("$2" "$3"); shift 3 ;;
		--device) devices+=(-device "$2"); shift 2 ;;
		--drive) drives+=("$2"); shift 2 ;;
		--screendump) screendump="$2"; shift 2 ;;
		--screen-font) screen_font="$2"; shift 2 ;;
		--screen-line) screen_lines+=("$2"); shift 2 ;;
		--until-screen) until_screen=1; shift ;;
		--absent) absent+=("$2"); shift 2 ;;
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
case "$machine" in
virt)
	fw="$firmware"
	if [ -z "$fw" ]; then
		for d in "$(dirname "$qemu")/../share/qemu" /opt/homebrew/share/qemu /usr/local/share/qemu /usr/share/qemu; do
			[ -f "$d/edk2-aarch64-code.fd" ] && { fw="$d/edk2-aarch64-code.fd"; break; }
		done
	fi
	[ -n "$fw" ] || { echo "EDK2 firmware edk2-aarch64-code.fd not found next to QEMU"; exit 1; }
	machine_args=(-M virt,gic-version=3 -drive if=pflash,format=raw,readonly=on,file="$fw")
	: "${mem:=512M}"
	;;
virt-secure)
	# BL1 runs from the secure flash that -bios fills; BL2 loads BL31 and
	# BL33 (EDK2) from the FIP after it. No virtualization=on: BL31 enters
	# EDK2 at Non-secure EL1, as on plain virt.
	[ -n "$firmware" ] && [ -f "$firmware" ] || { echo "--machine virt-secure needs --firmware FILE (the TF-A secure flash image)"; exit 1; }
	machine_args=(-M virt,secure=on,gic-version=3 -bios "$firmware")
	: "${mem:=1G}"
	;;
*) echo "unknown --machine $machine (virt, virt-secure)"; exit 1 ;;
esac
work="$(mktemp -d)"; trap 'rm -rf "$work"' EXIT
# The screen's text: each 8x16 cell's foreground bits (anything but the
# commonest colour) looked up in the font video_console.c draws with,
# bit 0 leftmost (vc_render_char). Unknown cells, such as the cursor,
# read as "?". With a third argument, a file of lines, it prints nothing
# and exits 3 unless each of them is on the screen.
cat > "$work/screen.pl" <<'SCREEN_PL'
my ($ppm, $font, $need) = @ARGV; my $screen = "";
open(my $ff, "<", $font) or die "$font: $!"; my $src = do { local $/; <$ff> };
$src =~ /iso_font\[[^\]]*\]\s*=\s*\{(.*?)\};/s or die "no iso_font in $font";
my @b = map { hex } ($1 =~ /0x([0-9a-fA-F]{2})/g); @b == 4096 or die "iso_font has " . @b . " bytes";
my %glyph; for my $c (reverse 33 .. 126) { $glyph{join(",", @b[$c * 16 .. $c * 16 + 15])} = chr($c) }
open(my $pf, "<:raw", $ppm) or die "$ppm: $!"; my $d = do { local $/; <$pf> };
$d =~ s/\AP6\s+(?:#[^\n]*\n\s*)*(\d+)\s+(\d+)\s+(\d+)\s//s or die "$ppm is not a binary PPM";
my ($w, $h) = ($1, $2);
my %n; $n{substr($d, $_ * 3, 3)}++ for 0 .. $w * $h - 1;
my ($bg) = sort { $n{$b} <=> $n{$a} } keys %n;
for my $r (0 .. int($h / 16) - 1) {
	my $line = "";
	for my $c (0 .. int($w / 8) - 1) {
		my @rows;
		for my $y (0 .. 15) {
			my $o = (($r * 16 + $y) * $w + $c * 8) * 3; my $v = 0;
			for my $x (0 .. 7) { $v |= 1 << $x if substr($d, $o + $x * 3, 3) ne $bg }
			push @rows, $v;
		}
		my $k = join(",", @rows);
		$line .= $k eq "0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0" ? " " : ($glyph{$k} // "?");
	}
	$line =~ s/\s+$//; $screen .= "$line\n";
}
if (defined $need) {
	open(my $nf, "<", $need) or die "$need: $!";
	for my $want (grep { length } map { chomp; $_ } <$nf>) { exit 3 if index($screen, $want) < 0 }
} else {
	print $screen;
}
SCREEN_PL
mkdir -p "$work/esp/EFI/BOOT"; cp "$efi" "$work/esp/EFI/BOOT/BOOTAA64.EFI"
for spec in ${esp_files[@]+"${esp_files[@]}"}; do
	dest="$work/esp/${spec%%=*}"; mkdir -p "$(dirname "$dest")"; cp "${spec#*=}" "$dest"
done
for spec in ${drives[@]+"${drives[@]}"}; do
	id="${spec%%=*}"; image="${spec#*=}"; file="$work/drive-$id.img"
	case "$image" in
	*[0-9][KMG]) perl -e 'my %u = (K => 1 << 10, M => 1 << 20, G => 1 << 30); $ARGV[0] =~ /^(\d+)([KMG])$/ or die;
		open(my $f, ">", $ARGV[1]) or die; truncate($f, $1 * $u{$2}) or die' "$image" "$file" ;;
	*) cp "$image" "$file"; chmod u+w "$file" ;;
	esac
	devices+=(-drive "if=none,id=$id,format=raw,file=$file")
done
logdir="$work"; debug=()
if [ -n "${ND_QEMU_DEBUG:-}" ]; then
	logdir="$ND_QEMU_DEBUG"; mkdir -p "$logdir"; debug=(-d int,guest_errors,unimp -D "$logdir/qemu.log")
fi
log="$logdir/serial.log"; : > "$log"
patterns="$work/expect"; printf '%s\n' "$@" > "$patterns"
screen_need="$work/screen_need"; printf '%s\n' ${screen_lines[@]+"${screen_lines[@]}"} > "$screen_need"
if [ "$until_screen" -eq 1 ] && { [ -z "$screendump" ] || [ -z "$screen_font" ]; }; then
	echo "--until-screen needs --screendump and --screen-font"; exit 1
fi
# The serial port is a pair of named pipes (QEMU's pipe chardev): QEMU writes
# the guest's output to ser.out and reads its input from ser.in.
steps="$work/sends"; : > "$steps"
i=0; while [ $i -lt ${#sends[@]} ]; do printf '%s\t%s\n' "${sends[$i]}" "${sends[$((i + 1))]}" >> "$steps"; i=$((i + 2)); done
mkfifo "$work/ser.in" "$work/ser.out"
# With a screendump, the monitor is a Unix socket the watchdog talks to.
monitor=(-monitor none); dump=""
if [ -n "$screendump" ]; then
	dump="${TEST_UNDECLARED_OUTPUTS_DIR:-$logdir}/$screendump"; mkdir -p "$(dirname "$dump")"; rm -f "$dump"
	# A short path: Unix socket names are limited to about 100 bytes, and
	# Bazel's TMPDIR is long.
	mon_dir="$(mktemp -d /tmp/ndmon.XXXXXX)"; trap 'rm -rf "$work" "$mon_dir"' EXIT
	monitor=(-monitor unix:"$mon_dir/mon",server=on,wait=off)
fi
status=0
# Watchdog in perl: QEMU ignores SIGALRM. It copies serial output to the log,
# types each --send-after step once its line has appeared, and in
# --until-lines mode stops QEMU once every expected line is there (and,
# with --until-screen, every screen line). QEMU stalls if its output isn't
# drained, so the loop always reads it.
perl -e '
	use Fcntl; use Time::HiRes qw(time); use IO::Socket::UNIX;
	my ($t, $until, $log, $pat, $steps, $ser, $mon, $dump, $until_screen, $screen_pl, $font, $need, @cmd) = @ARGV;
	open(my $pf, "<", $pat) or die; my @want = grep { length } map { chomp; $_ } <$pf>;
	open(my $sf, "<", $steps) or die; my @send = map { chomp; [split /\t/, $_, 2] } <$sf>;
	sysopen(my $out, "$ser.out", O_RDWR | O_NONBLOCK) or die "ser.out: $!";
	sysopen(my $in, "$ser.in", O_RDWR) or die "ser.in: $!";
	open(my $lf, ">>", $log) or die; $lf->autoflush(1);
	my ($text, $pos, $due) = ("", 0, undef);
	sub drain { my $buf; while (sysread($out, $buf, 65536)) { print $lf $buf; $buf =~ s/\r//g; $text .= $buf } }
	# The display as a PPM, a second after the last expected line (the
	# console draws as it prints), once QEMU has finished writing it.
	sub screendump {
		return unless length $dump;
		my $until = time + 1; while (time < $until) { drain(); select(undef, undef, undef, 0.1) }
		my $m = IO::Socket::UNIX->new(Type => SOCK_STREAM(), Peer => $mon) or return;
		print $m "screendump $dump\n";
		my ($last, $deadline) = (-1, time + 15);
		while (time < $deadline) {
			drain(); select(undef, undef, undef, 0.3);
			my $size = -s $dump; last if defined $size && $size > 0 && $size == $last; $last = $size // -1;
		}
		close $m;
	}
	# --until-screen: a fresh screendump every few seconds, read back
	# until it shows every screen line.
	my $next_look = 0;
	sub screen_ready {
		return 1 unless $until_screen;
		return 0 if time < $next_look;
		unlink $dump; screendump(); $next_look = time + 3;
		return system("perl", $screen_pl, $dump, $font, $need) == 0;
	}
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
		if ($until && !@send && !grep({ index($text, $_) < 0 } @want) && screen_ready()) {
			screendump() unless $until_screen; kill 9, $pid; waitpid($pid, 0); exit 0
		}
		if (time >= $deadline) { screendump(); kill 9, $pid; waitpid($pid, 0); exit 124 }
		select(undef, undef, undef, 0.2);
	}
' "$timeout" "$until_lines" "$log" "$patterns" "$steps" "$work/ser" "${mon_dir:-}/mon" "$dump" \
	"$until_screen" "$work/screen.pl" "$screen_font" "$screen_need" \
	"$qemu" "${machine_args[@]}" -cpu "$cpu" -smp "$smp" -m "$mem" \
	-nographic -no-reboot ${devices[@]+"${devices[@]}"} \
	-drive format=raw,file=fat:rw:"$work/esp" -chardev pipe,id=ser,path="$work/ser" -serial chardev:ser "${monitor[@]}" \
	${debug[@]+"${debug[@]}"} || status=$?
# The log as a terminal shows it: escape sequences dropped, backspaces
# applied (line editors such as zsh's back up and redraw), lines split.
clean="$(perl -0777 -pe 's/\e\[[0-9;?]*[A-Za-z]//g; 1 while s/[^\x08\n]\x08//; s/\x08//g; tr/\r/\n/' < "$log" | grep -av '^\s*$' || true)"
echo "$clean" | tail -n 60
# The screen's text too, even when the run failed.
screen=""
if [ -n "$screen_font" ] && [ -s "$dump" ]; then
	screen="$(perl "$work/screen.pl" "$dump" "$screen_font")" || { echo "FAIL: cannot read the screendump's text"; exit 1; }
	echo "--- screen ---"; echo "$screen" | grep -av '^$' | tail -n 30; echo "--------------"
fi
[ "$status" -eq 124 ] && { echo "FAIL: timed out after ${timeout}s"; }
[ "$status" -ne 0 ] && [ "$status" -ne 124 ] && echo "FAIL: QEMU exited with status $status"
for want in "$@"; do
	echo "$clean" | grep -aqF -- "$want" || { echo "FAIL: missing serial line: $want"; exit 1; }
done
for unwanted in ${absent[@]+"${absent[@]}"}; do
	if echo "$clean" | grep -aqF -- "$unwanted"; then echo "FAIL: unexpected serial line: $unwanted"; exit 1; fi
done
[ "$status" -eq 0 ] || exit 1
if [ -n "$screendump" ]; then
	[ -s "$dump" ] || { echo "FAIL: no screendump (is there a display device?)"; exit 1; }
	echo "screendump: $dump"
fi
if [ -n "$screen_font" ]; then
	for want in ${screen_lines[@]+"${screen_lines[@]}"}; do
		echo "$screen" | grep -aqF -- "$want" || { echo "FAIL: missing screen line: $want"; exit 1; }
	done
fi
echo "PASS: $# expected line(s) on serial${screen_lines[@]+, ${#screen_lines[@]} on screen}"
