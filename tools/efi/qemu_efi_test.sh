#!/usr/bin/env bash
# SPDX-License-Identifier: BSD-2-Clause
# Boot an EFI application as \EFI\BOOT\BOOTAA64.EFI on QEMU virt with EDK2 (and,
# with --machine virt-secure, TF-A at EL3; with --machine sbsa-ref, the SBSA
# reference machine with TF-A and SbsaQemu), or a disk image (--disk), and
# check the serial log.
#   qemu_efi_test.sh [OPTIONS] EFI_FILE TIMEOUT_SECONDS EXPECTED_LINE...
# Options:
#   --esp PATH=FILE   also place FILE on the ESP at PATH (e.g. NeoDarwin/kernelcache=...)
#   --mem SIZE        guest RAM (default 512M; 1G for virt-secure)
#   --smp N           CPUs (default 1)
#   --cpu MODEL       QEMU CPU (default cortex-a76, Armv8.2: the SBSA kernel's
#                     baseline; neoverse-n2, the machine's own, on sbsa-ref)
#   --machine KIND    virt (default): QEMU virt without EL3, EDK2 from QEMU's
#                     share/qemu, one GIC security state (GICD_CTLR.DS=1).
#                     virt-secure: virt,secure=on with TrustZone firmware at
#                     EL3 (--firmware, //third_party/qemu_firmware:virt_secure_flash)
#                     and the GIC's two security states (DS=0), as on SBSA
#                     boards; RAM defaults to 1G, since TF-A loads BL33 at
#                     0x60000000 (docs/kernel/qemu-secure.md)
#                     sbsa-ref: QEMU's SBSA reference machine (RAM at 1 TiB,
#                     GIC with ITS at 0x40060000, PL011 at 0x60000000, AHCI,
#                     XHCI and a PCIe host), booted by TF-A PLAT=qemu_sbsa
#                     (--firmware, Secure flash0) and EDK2 SbsaQemu
#                     (--firmware-ns, Non-secure flash1): both from
#                     //third_party/qemu_firmware:sbsa_ref_flash. RAM
#                     defaults to 2G; the FAT ESP is an AHCI disk (the
#                     machine's default interface), --disk-device and
#                     --device go on the PCIe root bus (docs/kernel/qemu-sbsa-ref.md)
#   --machine-opt OPT append OPT to -M (repeatable), e.g. iommu=smmuv3: an
#                     SMMUv3 between PCIe and memory, which the IORT then
#                     names on the requester IDs' way to the ITS
#   --firmware FILE   virt: EDK2 code flash to use instead of QEMU's;
#                     virt-secure: the secure flash image (BL1 + FIP), required;
#                     sbsa-ref: SBSA_FLASH0 (BL1 + FIP), required
#   --firmware-ns FILE
#                     sbsa-ref: SBSA_FLASH1 (EDK2 and its variable store),
#                     required; the guest writes to a copy
#   --until-lines     pass as soon as every expected line has appeared, then stop
#                     QEMU (for a kernel, which never powers off); default is to
#                     require QEMU to exit by itself within the timeout
#   --send-after LINE TEXT
#                     once LINE appears on serial (after the previous step's
#                     match), type TEXT half a second later, as a person
#                     would; "\n" in TEXT is Enter, "\b" Backspace (DEL, the
#                     tty's erase character), "\e" Esc, and {up}, {down},
#                     {left}, {right} the arrow keys' ANSI sequences. Steps
#                     run in order. The kernel drops input typed before the
#                     console is open, so LINE should be a prompt
#   --sendkey-after LINE TEXT
#                     the same, but TEXT is typed on the guest's keyboard
#                     (a USB keyboard: --device usb-kbd) through QEMU's
#                     monitor, one sendkey per character, not on serial.
#                     In TEXT, "\n" is Enter, "\b" Backspace, "\t" Tab,
#                     "\e" Esc, and {NAME} a QEMU key name, e.g. {left},
#                     {up}, {ctrl-c}
#   --sendkey-on-screen TEXT KEYS
#                     type KEYS on the guest's keyboard once the screen's
#                     last line (the cursor dropped) ends with TEXT, read
#                     from a screendump every few seconds (needs
#                     --screendump and --screen-font): for a console on the
#                     framebuffer alone. Steps of both kinds run in order
#   --device DEV      add a QEMU device, e.g. ramfb (a GOP framebuffer under EDK2)
#   --drive ID=IMAGE  add a raw block backend named ID for a --device to use
#                     (drive=ID), e.g. --drive disk0=16M --device
#                     virtio-blk-pci,drive=disk0. IMAGE is a size (a blank
#                     sparse image of that many K, M or G bytes) or a file,
#                     which is copied first: the guest writes to the copy
#   --disk IMAGE      boot a raw disk image instead: it is the only boot
#                     drive (no FAT ESP is made from EFI_FILE and --esp; pass
#                     EFI_FILE as - or anything), attached as --disk-device;
#                     EDK2 boots its ESP's \EFI\BOOT\BOOTAA64.EFI. The
#                     image is copied first: the guest writes to the copy
#   --disk-in-place IMAGE
#                     the same, but the guest writes to IMAGE itself, so a
#                     second run sees what the first wrote
#   --disk-device DEV the disk's QEMU device (default
#                     virtio-blk-pci,disable-legacy=on: modern virtio, 1af4:1042)
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
#   --user-net DEV    a NIC on QEMU's user-mode network (slirp: the guest is
#                     10.0.2.15, the gateway 10.0.2.2, DNS 10.0.2.3), e.g.
#                     virtio-net-pci,mac=52:54:00:12:34:56: -netdev user and
#                     -device DEV,netdev=... It opens no host port by itself
#   --hostfwd PORT    with --user-net: forward a TCP port on the host's
#                     loopback to guest PORT. The host side is 127.0.0.1 and
#                     a free high port chosen for this run (never 0.0.0.0);
#                     host commands find it in $ND_HOSTFWD_PORT. It closes
#                     with QEMU
#   --host-setup CMD  run CMD (bash) on the host before QEMU starts, in a
#                     directory of the run's own ($ND_HOST_DIR, also HOME);
#                     the test fails if it fails. E.g. ssh-keygen a key
#   --host-cmd-after LINE CMD
#                     once LINE appears on serial (in order with the
#                     --send-after steps), run CMD (bash) on the host in the
#                     same directory, with $ND_HOSTFWD_PORT, for at most
#                     ND_HOST_CMD_TIMEOUT seconds (default 180), while serial
#                     keeps being read. Its output and exit status join the
#                     log as "host: ..." lines ("host: exit N", 124 if it
#                     timed out), which expected lines can name.
#                     CMD is the test's own text: nothing from the guest's
#                     output goes into it
#   In --send-after TEXT, {hostfile:NAME} is the first line of file NAME in
#   the host directory (made by --host-setup), e.g. a public key to install
# Environment:
#   ND_QEMU           qemu-system-aarch64 to use
#   ND_QEMU_DEBUG=DIR keep serial.log there and add QEMU's exception trace (-d int)
set -euo pipefail
esp_files=(); mem=""; smp=1; cpu=""; until_lines=0; sends=(); machine=virt; firmware=""; firmware_ns=""
devices=(); drives=(); screendump=""; screen_font=""; screen_lines=(); until_screen=0; absent=(); mopts=""
disk=""; disk_in_place=0; disk_device="virtio-blk-pci,disable-legacy=on"
user_net=""; hostfwd=""; host_setups=()
while [ $# -gt 0 ]; do
	case "$1" in
		--esp) esp_files+=("$2"); shift 2 ;;
		--mem) mem="$2"; shift 2 ;;
		--smp) smp="$2"; shift 2 ;;
		--cpu) cpu="$2"; shift 2 ;;
		--machine) machine="$2"; shift 2 ;;
		--machine-opt) mopts="$mopts,$2"; shift 2 ;;
		--firmware) firmware="$2"; shift 2 ;;
		--firmware-ns) firmware_ns="$2"; shift 2 ;;
		--until-lines) until_lines=1; shift ;;
		--send-after) sends+=(serial "$2" "$3"); shift 3 ;;
		--sendkey-after) sends+=(key "$2" "$3"); shift 3 ;;
		--sendkey-on-screen) sends+=(screen "$2" "$3"); shift 3 ;;
		--device) devices+=(-device "$2"); shift 2 ;;
		--drive) drives+=("$2"); shift 2 ;;
		--disk) disk="$2"; disk_in_place=0; shift 2 ;;
		--disk-in-place) disk="$2"; disk_in_place=1; shift 2 ;;
		--disk-device) disk_device="$2"; shift 2 ;;
		--screendump) screendump="$2"; shift 2 ;;
		--screen-font) screen_font="$2"; shift 2 ;;
		--screen-line) screen_lines+=("$2"); shift 2 ;;
		--until-screen) until_screen=1; shift ;;
		--absent) absent+=("$2"); shift 2 ;;
		--user-net) user_net="$2"; shift 2 ;;
		--hostfwd) hostfwd="$2"; shift 2 ;;
		--host-setup) host_setups+=("$2"); shift 2 ;;
		--host-cmd-after) sends+=(host "$2" "$3"); shift 3 ;;
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
	machine_args=(-M "virt,gic-version=3$mopts" -drive if=pflash,format=raw,readonly=on,file="$fw")
	: "${mem:=512M}"
	;;
virt-secure)
	# BL1 runs from the secure flash that -bios fills; BL2 loads BL31 and
	# BL33 (EDK2) from the FIP after it. No virtualization=on: BL31 enters
	# EDK2 at Non-secure EL1, as on plain virt.
	[ -n "$firmware" ] && [ -f "$firmware" ] || { echo "--machine virt-secure needs --firmware FILE (the TF-A secure flash image)"; exit 1; }
	machine_args=(-M "virt,secure=on,gic-version=3$mopts" -bios "$firmware")
	: "${mem:=1G}"
	;;
sbsa-ref)
	# flash0 is Secure (TF-A's BL1 runs from it, the FIP follows); flash1
	# holds SbsaQemu, which BL31 enters in place at Non-secure EL2, and
	# its variables, so the guest gets a copy of it.
	[ -n "$firmware" ] && [ -f "$firmware" ] && [ -n "$firmware_ns" ] && [ -f "$firmware_ns" ] ||
		{ echo "--machine sbsa-ref needs --firmware SBSA_FLASH0 and --firmware-ns SBSA_FLASH1"; exit 1; }
	machine_args=(-M "sbsa-ref$mopts" -drive if=pflash,unit=0,format=raw,readonly=on,file="$firmware")
	: "${mem:=2G}"; : "${cpu:=neoverse-n2}"
	;;
*) echo "unknown --machine $machine (virt, virt-secure, sbsa-ref)"; exit 1 ;;
esac
: "${cpu:=cortex-a76}"
work="$(mktemp -d)"; trap 'rm -rf "$work"' EXIT
if [ "$machine" = sbsa-ref ]; then
	cp "$firmware_ns" "$work/flash1.fd"; chmod u+w "$work/flash1.fd"
	machine_args+=(-drive if=pflash,unit=1,format=raw,file="$work/flash1.fd")
fi
# The screen's text: each 8x16 cell's foreground bits (anything but the
# commonest colour) looked up in the font video_console.c draws with,
# bit 0 leftmost (vc_render_char). Unknown cells, such as the cursor,
# read as "?". With a third argument, a file of lines, it prints nothing
# and exits 3 unless each of them is on the screen.
cat > "$work/screen.pl" <<'SCREEN_PL'
my ($ppm, $font, $need, $mode) = @ARGV; my $screen = "";
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
if (defined $mode && $mode eq "last") {
	# Exit 0 if the last non-blank line, less the cursor ("?") and
	# spaces, ends with the text in the file $need.
	open(my $nf, "<", $need) or die "$need: $!"; my $want = <$nf>; chomp $want; $want =~ s/\s+$//;
	my ($last) = grep { /\S/ } reverse split /\n/, $screen; $last //= ""; $last =~ s/[\s?]+$//;
	exit(length($last) >= length($want) && substr($last, -length($want)) eq $want ? 0 : 3);
} elsif (defined $need) {
	open(my $nf, "<", $need) or die "$need: $!";
	for my $want (grep { length } map { chomp; $_ } <$nf>) { exit 3 if index($screen, $want) < 0 }
} else {
	print $screen;
}
SCREEN_PL
# The boot drive: a FAT ESP from EFI_FILE and --esp (QEMU's vvfat), or
# with --disk the given image as the only one.
if [ -n "$disk" ]; then
	boot_file="$disk"
	if [ "$disk_in_place" -eq 0 ]; then
		boot_file="$work/disk.img"; cp "$disk" "$boot_file"; chmod u+w "$boot_file"
	fi
	boot_drive=(-drive "if=none,id=bootdisk,format=raw,file=$boot_file" -device "$disk_device,drive=bootdisk,bootindex=0")
else
	mkdir -p "$work/esp/EFI/BOOT"; cp "$efi" "$work/esp/EFI/BOOT/BOOTAA64.EFI"
	for spec in ${esp_files[@]+"${esp_files[@]}"}; do
		dest="$work/esp/${spec%%=*}"; mkdir -p "$(dirname "$dest")"; cp "${spec#*=}" "$dest"
	done
	boot_drive=(-drive format=raw,file=fat:rw:"$work/esp")
fi
for spec in ${drives[@]+"${drives[@]}"}; do
	id="${spec%%=*}"; image="${spec#*=}"; file="$work/drive-$id.img"
	case "$image" in
	*[0-9][KMG]) perl -e 'my %u = (K => 1 << 10, M => 1 << 20, G => 1 << 30); $ARGV[0] =~ /^(\d+)([KMG])$/ or die;
		open(my $f, ">", $ARGV[1]) or die; truncate($f, $1 * $u{$2}) or die' "$image" "$file" ;;
	*) cp "$image" "$file"; chmod u+w "$file" ;;
	esac
	devices+=(-drive "if=none,id=$id,format=raw,file=$file")
done
# The host side: a directory for --host-setup and --host-cmd-after, and the
# user-mode network with its one loopback forward.
host_dir="$work/host"; mkdir -p "$host_dir"
export ND_HOST_DIR="$host_dir"
if [ -n "$hostfwd" ] && [ -z "$user_net" ]; then echo "--hostfwd needs --user-net"; exit 1; fi
case "$hostfwd" in ""|[0-9]|[0-9][0-9]|[0-9][0-9][0-9]|[0-9][0-9][0-9][0-9]|[0-9][0-9][0-9][0-9][0-9]) ;;
	*) echo "--hostfwd takes a guest TCP port number"; exit 1 ;; esac
if [ -n "$user_net" ]; then
	netdev="user,id=ndnet0"
	if [ -n "$hostfwd" ]; then
		# A free port on 127.0.0.1 (the kernel's choice), for this run only.
		port="$(perl -MIO::Socket::INET -e '
			my $s = IO::Socket::INET->new(LocalAddr => "127.0.0.1", LocalPort => 0, Proto => "tcp", Listen => 1, ReuseAddr => 1)
				or die "no free port: $!"; print $s->sockport')"
		export ND_HOSTFWD_PORT="$port"
		netdev="$netdev,hostfwd=tcp:127.0.0.1:$port-:$hostfwd"
	fi
	devices+=(-netdev "$netdev" -device "$user_net,netdev=ndnet0")
fi
for cmd in ${host_setups[@]+"${host_setups[@]}"}; do
	(cd "$host_dir" && HOME="$host_dir" bash -c "$cmd") || { echo "FAIL: --host-setup failed: $cmd"; exit 1; }
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
has_keys=0; has_screen_keys=0
i=0; while [ $i -lt ${#sends[@]} ]; do
	printf '%s\t%s\t%s\n' "${sends[$i]}" "${sends[$((i + 1))]}" "${sends[$((i + 2))]}" >> "$steps"
	[ "${sends[$i]}" = serial ] || has_keys=1
	[ "${sends[$i]}" = screen ] && has_screen_keys=1
	i=$((i + 3))
done
if [ "$has_screen_keys" -eq 1 ] && { [ -z "$screendump" ] || [ -z "$screen_font" ]; }; then
	echo "--sendkey-on-screen needs --screendump and --screen-font"; exit 1
fi
mkfifo "$work/ser.in" "$work/ser.out"
# With a screendump or keys to type, the monitor is a Unix socket the
# watchdog talks to.
monitor=(-monitor none); dump=""
if [ -n "$screendump" ]; then
	dump="${TEST_UNDECLARED_OUTPUTS_DIR:-$logdir}/$screendump"; mkdir -p "$(dirname "$dump")"; rm -f "$dump"
fi
if [ -n "$screendump" ] || [ "$has_keys" -eq 1 ]; then
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
	open(my $sf, "<", $steps) or die; my @send = map { chomp; [split /\t/, $_, 3] } <$sf>;
	sysopen(my $out, "$ser.out", O_RDWR | O_NONBLOCK) or die "ser.out: $!";
	sysopen(my $in, "$ser.in", O_RDWR) or die "ser.in: $!";
	open(my $lf, ">>", $log) or die; $lf->autoflush(1);
	my ($text, $pos, $due) = ("", 0, undef);
	sub drain { my $buf; while (sysread($out, $buf, 65536)) { print $lf $buf; $buf =~ s/\r//g; $text .= $buf } }
	# --host-cmd-after: the command runs in a child of its own while serial
	# is drained; its output, then its status, join the log and the text
	# as "host: " lines.
	my ($host_pid, $host_out, $host_deadline, $host_n) = (undef, undef, 0, 0);
	my $host_limit = $ENV{ND_HOST_CMD_TIMEOUT} || 180;
	sub host_start {
		my ($cmd) = @_; $host_n++;
		$host_out = "$ENV{ND_HOST_DIR}/.host-cmd-$host_n.out";
		$host_pid = fork();
		if (!$host_pid) {
			chdir $ENV{ND_HOST_DIR} or die; $ENV{HOME} = $ENV{ND_HOST_DIR};
			open(STDOUT, ">", $host_out) or die; open(STDERR, ">&", \*STDOUT) or die; open(STDIN, "<", "/dev/null");
			setpgrp(0, 0);
			exec("bash", "-c", $cmd) or die "exec bash: $!";
		}
		$host_deadline = time + $host_limit;
		my $note = "host\$ $cmd\n"; print $lf $note; $text .= $note;
	}
	sub host_poll {
		return 1 unless defined $host_pid;
		my $done = waitpid($host_pid, 1) > 0; my $status = $?;
		if (!$done && time >= $host_deadline) {
			kill 9, -$host_pid; waitpid($host_pid, 0); $status = -1; $done = 1;
		}
		return 0 unless $done;
		open(my $hf, "<", $host_out); my @lines = <$hf>; close $hf;
		my $report = join("", map { s/\r//g; chomp; "host: $_\n" } @lines);
		$report .= $status < 0 ? "host: timed out after ${host_limit}s\nhost: exit 124\n" : "host: exit " . ($status >> 8) . "\n";
		print $lf $report; $text .= $report; undef $host_pid;
		return 1;
	}
	# {hostfile:NAME}: the first line of a file in the host directory.
	sub hostfiles {
		my ($t) = @_;
		$t =~ s{\{hostfile:([A-Za-z0-9._-]+)\}}{
			open(my $f, "<", "$ENV{ND_HOST_DIR}/$1") or die "{hostfile:$1}: $!"; my $l = <$f> // ""; chomp $l; $l
		}ge;
		return $t;
	}
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
	# Keys through the monitor: one sendkey per key, held 20 ms, 100 ms
	# apart so that a key is up again before it is pressed twice.
	my %named = (" " => "spc", "-" => "minus", "=" => "equal", "[" => "bracket_left", "]" => "bracket_right",
		"\\" => "backslash", ";" => "semicolon", "\x27" => "apostrophe", "`" => "grave_accent", "," => "comma",
		"." => "dot", "/" => "slash");
	my %shifted = ("!" => "1", "@" => "2", "#" => "3", "\$" => "4", "%" => "5", "^" => "6", "&" => "7", "*" => "8",
		"(" => "9", ")" => "0", "_" => "minus", "+" => "equal", "{" => "bracket_left", "}" => "bracket_right",
		"|" => "backslash", ":" => "semicolon", "\"" => "apostrophe", "~" => "grave_accent", "<" => "comma",
		">" => "dot", "?" => "slash");
	my %escapes = ("n" => "ret", "b" => "backspace", "t" => "tab", "e" => "esc");
	sub keynames {
		my ($text) = @_; my @k;
		while (length $text) {
			if ($text =~ s/^\\(.)//s) { push @k, $escapes{$1} // die "unknown escape \\$1" }
			elsif ($text =~ s/^\{([a-z0-9_-]+)\}//) { push @k, $1 }
			else {
				my $c = substr($text, 0, 1, "");
				if ($c =~ /^[a-z0-9]$/) { push @k, $c }
				elsif ($c =~ /^[A-Z]$/) { push @k, "shift-" . lc $c }
				elsif (exists $named{$c}) { push @k, $named{$c} }
				elsif (exists $shifted{$c}) { my $b = $shifted{$c}; push @k, "shift-" . ($named{$b} // $b) }
				else { die "no key for " . sprintf("0x%02x", ord $c) }
			}
		}
		return @k;
	}
	sub typekeys {
		my @k = keynames($_[0]);
		my $m = IO::Socket::UNIX->new(Type => SOCK_STREAM(), Peer => $mon) or die "monitor: $!";
		$m->blocking(0);
		for my $k (@k) {
			print $m "sendkey $k 20\n";
			my $until = time + 0.1; while (time < $until) { drain(); my $junk; sysread($m, $junk, 4096); select(undef, undef, undef, 0.02) }
		}
		my $until = time + 0.3; while (time < $until) { drain(); my $junk; sysread($m, $junk, 4096); select(undef, undef, undef, 0.05) }
		close $m;
	}
	# A --sendkey-on-screen step: the last line of the screen, every few seconds.
	my $next_key_look = 0;
	sub screen_prompt {
		my ($want) = @_;
		return 0 if time < $next_key_look;
		$next_key_look = time + 3;
		open(my $wf, ">", "$need.last") or die; print $wf "$want\n"; close $wf;
		unlink $dump; screendump();
		return system("perl", $screen_pl, $dump, $font, "$need.last", "last") == 0;
	}
	my $pid = fork(); if (!$pid) { exec @cmd or die "exec: $!" }
	my $deadline = time + $t;
	while (1) {
		drain();
		host_poll();
		if (waitpid($pid, 1) > 0) { my $st = $?; drain(); kill 9, -$host_pid if defined $host_pid; exit($st >> 8) }
		if (@send && !defined $due && $send[0][0] ne "screen" && (my $at = index($text, $send[0][1], $pos)) >= 0) {
			$pos = $at + length($send[0][1]); $due = time + 0.5;
		}
		if (@send && !defined $due && $send[0][0] eq "screen" && screen_prompt($send[0][1])) {
			$pos = length($text); $due = time + 0.5;
		}
		if (defined $due && time >= $due && host_poll()) {
			if ($send[0][0] eq "host") {
				host_start($send[0][2]);
			} elsif ($send[0][0] eq "serial") {
				(my $keys = hostfiles($send[0][2])) =~ s/\\n/\r/g;
				my %arrow = (up => "A", down => "B", right => "C", left => "D");
				$keys =~ s/\\b/\x7f/g; $keys =~ s/\\e/\e/g; $keys =~ s/\{(up|down|left|right)\}/\e[$arrow{$1}/g;
				syswrite($in, $keys);
			} else {
				typekeys($send[0][2]);
				$next_key_look = time + 3;
			}
			shift @send; undef $due;
		}
		if ($until && !@send && host_poll() && !grep({ index($text, $_) < 0 } @want) && screen_ready()) {
			screendump() unless $until_screen; kill 9, $pid; waitpid($pid, 0); exit 0
		}
		if (time >= $deadline) { kill 9, -$host_pid if defined $host_pid; screendump(); kill 9, $pid; waitpid($pid, 0); exit 124 }
		select(undef, undef, undef, 0.2);
	}
' "$timeout" "$until_lines" "$log" "$patterns" "$steps" "$work/ser" "${mon_dir:-}/mon" "$dump" \
	"$until_screen" "$work/screen.pl" "$screen_font" "$screen_need" \
	"$qemu" "${machine_args[@]}" -cpu "$cpu" -smp "$smp" -m "$mem" \
	-nographic -no-reboot ${devices[@]+"${devices[@]}"} \
	"${boot_drive[@]}" -chardev pipe,id=ser,path="$work/ser" -serial chardev:ser "${monitor[@]}" \
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
