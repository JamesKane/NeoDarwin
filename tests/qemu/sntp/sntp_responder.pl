#!/usr/bin/env perl
# SPDX-License-Identifier: BSD-2-Clause
# sntp_responder.pl DIR SECONDS: an SNTP server (RFC 4330) for a QEMU test
# (docs/base/pf-ntp.md, "Tests"), answering client requests with the host's
# clock as a stratum 1 server. It listens on the host's loopback only
# (127.0.0.1, UDP, a free port the kernel picks), which a guest on QEMU's
# user network reaches as 10.0.2.2 at the same port; it writes the port to
# DIR/ntp_port and logs each answer to standard output. It stops after
# SECONDS, or as soon as DIR is gone (the test harness removes its run
# directory when the run ends), so it never outlives the test.
use strict;
use warnings;
use IO::Select;
use IO::Socket::INET;
use Socket qw(unpack_sockaddr_in inet_ntoa);
use Time::HiRes qw(time);

$| = 1;
my ($dir, $seconds) = @ARGV;
die "usage: sntp_responder.pl DIR SECONDS\n" unless defined $seconds && -d $dir;
my $sock = IO::Socket::INET->new(LocalAddr => "127.0.0.1", LocalPort => 0, Proto => "udp")
	or die "sntp-responder: socket: $!\n";
my $port = $sock->sockport;
open(my $pf, ">", "$dir/ntp_port.tmp") or die "sntp-responder: $dir/ntp_port: $!\n";
print $pf "$port\n";
close $pf;
rename("$dir/ntp_port.tmp", "$dir/ntp_port") or die "sntp-responder: $dir/ntp_port: $!\n";
print "sntp-responder: listening on 127.0.0.1:$port\n";

# NTP's timestamp: seconds since 1900 and a 32-bit binary fraction.
sub ntp_time {
	my ($t) = @_;
	my $s = int($t);
	return pack("NN", $s + 2208988800, int(($t - $s) * 4294967296));
}

my $sel = IO::Select->new($sock);
my $end = time + $seconds;
while (time < $end && -d $dir) {
	next unless $sel->can_read(1);
	my $from = $sock->recv(my $req, 1024);
	next unless defined $from;
	my $received = time;
	next if length($req) < 48;
	my $first = unpack("C", $req);
	my ($version, $mode) = (($first >> 3) & 7, $first & 7);
	next unless $mode == 3; # a client's request
	my $poll = unpack("x2 C", $req);
	my $reply = pack("C C C c", ($version << 3) | 4, 1, $poll, -20) # no leap warning, server, stratum 1, ~1 us
		. pack("N N", 0, 0x10)                                   # root delay 0, root dispersion ~0.2 ms
		. "LOCL"                                                 # reference: the host's own clock
		. ntp_time($received)                                    # reference time
		. substr($req, 40, 8)                                    # originate: the request's transmit time
		. ntp_time($received)                                    # receive
		. ntp_time(time);                                        # transmit
	$sock->send($reply, 0, $from) or next;
	my ($peer_port, $peer) = unpack_sockaddr_in($from);
	printf "sntp-responder: answered %s:%d (NTPv%d) at %d\n", inet_ntoa($peer), $peer_port, $version, int($received);
}
print "sntp-responder: stopped\n";
