<!-- SPDX-License-Identifier: BSD-2-Clause -->
# pf, pfctl and the NTP client (P4-24)

Part of **P4-24**, the networking userland (`docs/kernel/network.md`, "What P4-24 still needs"): the packet filter with its control program, and a client that sets the clock from NTP servers. Both run under launchd as on macOS, both are off by default, and both are proven on QEMU by `//kernel:sbsa_pf_ntp_test`. The rest of P4-24 (network_cmds, IPv6) is documented in `docs/kernel/network.md`.

## Decisions

| Question | Decision | Why |
|---|---|---|
| pf in the kernel | **already built**: no kernel patch | The SBSA kernel uses `config/MASTER.arm64.MacOSX`, whose `BASE` includes `PF = [ pf pflog ]`, so `bsd/net/pf*.c` are compiled in. `pfinit()` runs at boot and makes `/dev/pf` and `/dev/pfm` (`root:wheel 0600`); pf stays stopped until someone enables it (`pf_status.running` 0, no ruleset). Nothing changes at boot |
| pfctl's source | **OpenBSD 4.3's `sbin/pfctl`**, pinned file by file at the openbsd/src commit of its last change before the 4.3 release (`fd47049c620b`, 2008-02-13; `base/pfctl/openbsd.lock`), built against **xnu's own `<net/pfvar.h>`** | Apple publishes no pfctl: no `network_cmds` tag has one (checked from network_cmds-77 to 741.100.2), and no other `apple-oss-distributions` project does (the `IPFirewall` and `FirewallTool` projects are the old ipfw). The reuse order then goes to the BSDs. xnu's pf is OpenBSD's of early 2008: its `pfvar.h` is OpenBSD revision 1.259 (2007-12-02), `pf.c` 1.567 (2008-02-20), `pf_ioctl.c` 1.175, `pf_table.c` 1.68. OpenBSD 4.3 (May 2008) is the release of those months, so its pfctl speaks the same ioctls with the same structures, less Apple's changes. FreeBSD's pre-netlink pfctl is further away: FreeBSD 7–9 carried OpenBSD 4.1's pf, and FreeBSD 10 and later rewrote the kernel side and its ioctl structures. Compiling against xnu's header (System.framework's PrivateHeaders) makes every structure the kernel's; the patches follow Apple's renamed members |
| ALTQ | **not built** | xnu has no ALTQ: its `pfioctl` answers the ALTQ requests `ENODEV`, and OpenBSD's pfctl then turns queueing off ("No ALTQ support in kernel / ALTQ related functions disabled"), which is what macOS's pfctl prints. `pfctl_altq.c`, `pfctl_qstats.c` and OpenBSD's `sys/altq` headers (some under the four-clause BSD licence) are left out; `base/pfctl/src/nd_pfctl_noaltq.c` stands in for their functions (a `queue` or `altq` rule is an error) |
| pfctl's Apple extensions | **enable references** (`-E`, `-X`, `-s References`) and **`scrub-anchor`/`dummynet-anchor`** | macOS's pf.conf and its tools use them; they are small and the kernel side is in xnu (patch 0002). Not done: dummynet rules themselves (pipes, `dnctl`) and NAT64 rules |
| `/etc/pf.conf` | **macOS's**: the `com.apple` anchor points (`scrub-anchor`, `nat-anchor`, `rdr-anchor`, `dummynet-anchor`, `anchor`) and `load anchor "com.apple" from "/etc/pf.anchors/com.apple"`, which is empty here | the ruleset other components hang their rules from; with only anchors, pf passes everything until rules are added. On macOS `/etc/pf.anchors/com.apple` names Internet Sharing, AirDrop and the application firewall, which NeoDarwin doesn't have |
| pf at boot | **`com.apple.pfctl`** (`/System/Library/LaunchDaemons/com.apple.pfctl.plist`), `Disabled` as on macOS, running `pfctl -E -f /etc/pf.conf` | macOS's mechanism: the job's `Disabled` key is the switch. `launchctl load -w` turns it on now; launchctl has no overrides database yet (`docs/base/session.md`), so for every boot set `Disabled` to false in the plist. `-E` takes a reference rather than a plain `-e`, so pf stays on while other components (which enable pf with their own references on macOS) come and go. Its output, the token included, goes to `/var/log/pfctl.log` |
| NTP client | **`sntp` from Apple's ntp-139** (ntp 4.2.8p10), the last ntp drop Apple published (macOS 10.15), pinned by tarball hash | Reuse order, case 2 (an earlier Apple drop): from macOS 10.14 Apple sets the clock with `timed`, which is closed, and stopped publishing ntp after 10.15. ntp-139's `ntpd` links Apple's closed `libCrashReporterClient`, `libSMC` and IOKit, and Seatbelt (`sandbox_init`); its `sntp` links only libntp (with libevent and libopts inside) and builds with two small patches. macOS's own `ntpd-wrapper` ran exactly this sntp (`sntp -S server`) at boot before starting ntpd. FreeBSD's ntpd stays the choice for a machine that must serve time or discipline its clock continuously |
| The job | **`com.neodarwin.sntp`**: `/usr/libexec/sntp-wrapper` (Embedded Swift, `base/sntp_wrapper`) at load and every 1024 seconds, `Disabled` | The wrapper does what ntpd-wrapper did before starting ntpd, without configd: wait (at most 60 s) for a resolver configuration (`/etc/resolv.conf`), then run `sntp -K /dev/null -S -s -M 128 HOST` for each `server` or `pool` line of `/etc/ntp.conf` until one succeeds: a step when the clock is more than 128 ms off (ntpd's step threshold), a slew otherwise. 1024 s is ntpd's longest default poll interval. Off by default, as FreeBSD's `ntpd_enable="NO"`, which also keeps the QEMU tests from reaching the internet's NTP servers |
| `/etc/ntp.conf` | `pool pool.ntp.org iburst`, as FreeBSD's names the pool | ntpd's syntax, so the same file serves ntpd later; the wrapper reads only the host of `server` and `pool` lines |

## Pieces

| Where | What |
|---|---|
| `//base:packet_filter` (`base/pfctl`) | `/sbin/pfctl` from `@openbsd_pfctl` (`openbsd.lock`: `sbin/pfctl` less the ALTQ files, `sys/net/pf_ruleset.c`, `etc/pf.os`), with FreeBSD's `md5c.c` for the parser's one MD5; `compat/nd_pfctl_compat.h` (OpenBSD's `SIMPLEQ` and `TAILQ_END`, the ruleset prototypes xnu keeps for the kernel, xnu's `pf_addr` member names), `compat/altq/` (the ALTQ constants the parser names), `src/nd_pfctl_noaltq.c`; `/etc/pf.conf`, `/etc/pf.anchors/com.apple`, `/etc/pf.os` (OpenBSD's passive OS fingerprints, which macOS ships too), `com.apple.pfctl.plist` |
| `//base:ntp_client` (`base/ntp`) | `/usr/bin/sntp` from `@apple_ntp` (ntp-139), replaying `ntp.xcodeproj`'s `ntp` (libntp) and `sntp` targets with the shipped `config.h`s; `/etc/ntp.conf` |
| `//base/sntp_wrapper` | `/usr/libexec/sntp-wrapper` and `com.neodarwin.sntp.plist`, in the session images through `images/BUILD.bazel` (`_NETWORK_FILES`) |
| `tests/qemu/sntp` | the test's SNTP responder (host side) |

### pfctl's patches

- **0001, xnu's structures and Darwin.** A rule's port match is the union `xport` (TCP/UDP ranges, GRE call IDs, ESP SPIs): `src.port[]`/`port_op` become `src.xport.range.port[]`/`.op` in the parser, the printer and the optimizer's field table (which lists the whole union). A state keeps a family per side of the translation (`af_lan`, `af_gwy`, for NAT64) and the external host as each side sees it (`ext_lan`, `ext_gwy`); its ports are `xport.port`, and its times are 64-bit. Darwin has no interface groups (`SIOCGIFGMEMB`), and bison wants `YYSTYPE_IS_DECLARED` next to the parser's own `YYSTYPE`.
- **0002, xnu's extensions.** `-E` enables pf with a reference (`DIOCSTARTREF`) and prints the token; `-X token` releases one (`DIOCSTOPREF`, and pf stops with the last); `-s References` lists the holders (`DIOCGETSTARTERS`); `-e` and `-d` behave as on macOS (`-d` stops pf and drops every reference). `scrub-anchor` and `dummynet-anchor` add anchors to the scrub and dummynet rulesets, which xnu's `pf_norm.c` and `pf.c` step into; `-s dummynet` shows the dummynet ruleset, and its transactions go with the filter rules (`-R`, `-F rules`). `pf_get_ruleset_number()` (OpenBSD's userland copy in `pf_ruleset.c`) knows `PF_DUMMYNET` and `PF_NAT64`, and rules print their names.

The compatibility header renames OpenBSD's `v4`/`v6` to xnu's `v4addr`/`v6addr`, maps `SIMPLEQ` to `STAILQ`, and defines `RT_TABLEID_MAX` as `INT_MAX`: on xnu a rule's `rtableid` is an interface scope (an interface index, `PF_RTABLEID_IS_VALID` in `pf.c`), not a routing table. pfctl is built with `-fcommon`, as OpenBSD's 2008 compiler defaulted to: `pfctl.c` and `pf_ruleset.c` both define `pf_anchors` and `pf_main_anchor` tentatively.

### sntp's patches

- **0001.** `libntp/systime.c` traces each step and slew with `os_trace_debug()`, deprecated since macOS 10.13 and not in NeoDarwin's `libsystem_trace` stand-in; `os_log_debug()` is the same debug-level message. `include/ntp_md5.h` maps MD5 to CommonCrypto on every Apple platform, and the base has no libcommonCrypto: with `NEODARWIN_NO_COMMONCRYPTO` it takes ntp's own fallback, libisc's MD5, already in libntp (sntp uses MD5 only for symmetric-key authentication).
- **0002.** sntp sends to port 123 only. A server named `host:port` or `[address]:port` now names another port (a bare IPv6 address is taken whole, as before). The QEMU test needs it ("Tests").

libntp is built as the Xcode target builds it, less `systime_s.c` (`systime.c` built for ntpdsim, the simulator, defining the same functions).

## Using it

```
pfctl -s info                       # Status: Disabled at boot
pfctl -f /etc/pf.conf               # load the ruleset (No ALTQ support in kernel...)
pfctl -E                            # enable, with a reference: Token : N
pfctl -s References                 # who holds pf enabled
pfctl -X N                          # release it (pf stops with the last reference)
pfctl -d                            # stop pf outright
launchctl load -w /System/Library/LaunchDaemons/com.apple.pfctl.plist     # the boot job, now

echo 'server time.example.org' > /etc/ntp.conf
launchctl load -w /System/Library/LaunchDaemons/com.neodarwin.sntp.plist  # set the clock now and every 1024 s
sntp time.example.org               # query only
```

What the test's guest prints (`pfctl -s References`, then sntp's report in `/var/log/sntp.log`, made while the clock still read 2000):

```
PID      Process Name         TOKEN                TIMESTAMP
37       pfctl                12633458505157041302 1790890391
2000-01-01 00:00:00.407186 (+0000) +844205604.551704 +/- 562803736.368062 10.0.2.2 s1 no-leap
```

## Tests

`//kernel:sbsa_pf_ntp_test` boots `//images:pam_session_root` on QEMU `virt` with virtio-net on the user network, and after the DHCP lease:

1. **pf off at boot**: `ls -l /dev/pf`, `pfctl -s info` says `Status: Disabled`.
2. Remote Login is turned on and the run's key installed (as `//kernel:sbsa_net_test`).
3. **The boot job**: `launchctl load -w .../com.apple.pfctl.plist`; `/var/log/pfctl.log` has `No ALTQ support in kernel`, `pf enabled` and `Token : `; `pfctl -s info` says `Status: Enabled for 0 days ...`; `-s References` lists pfctl's token; `-s rules`, `-s nat` and `-s dummynet` show the five `com.apple/*` anchors.
4. **ssh under the default ruleset**: the host runs `uname -a` over ssh (`host: Darwin localhost ...`).
5. **A blocking rule**: `/etc/pf.conf` plus a table, `table <nd_test> persist { 192.0.2.1 }`, `block drop out quick inet proto icmp from any to 10.0.2.2` and `pass in quick on en0 inet proto tcp from any to any port 22 keep state` is loaded; `pfctl -s rules` shows the rules as the kernel holds them (`... port = ssh flags S/SA keep state`), and `ping 10.0.2.2` gets no reply (`ping-blocked-42`). The host logs in over ssh again, and `pfctl -s states` shows the state the pass rule made (`ALL tcp 10.0.2.15:22 <- 10.0.2.2:...`, as macOS prints a floating state); `pfctl -t nd_test -T add 192.0.2.2` (`1/1 addresses added.`) and `-T show` list both addresses, and `-s Tables` the table. Then `pfctl -d` (`pf disabled`, `Status: Disabled`), and the ping passes (`ping-passed-42`).
6. **NTP**: the guest's clock is set to 2000-01-01 (`year-2000`); `/etc/ntp.conf` names the host's responder; `com.neodarwin.sntp` is loaded and steps the clock (sntp's report `... 10.0.2.2 s1 no-leap` in `/var/log/sntp.log`); the host reads the guest's `date +%s` over ssh and checks it against its own (`host: clock-ok`, within 10 s), and the responder's log shows the request it answered.

**The SNTP responder.** slirp has no NTP server, and binding the host's port 123 needs privileges. `tests/qemu/sntp/sntp_responder.pl` is a small SNTP server (RFC 4330: a stratum 1 answer with the host's clock, the request's transmit time as the originate time). The test's `--host-setup` starts it in the background on **127.0.0.1 only**, on a free UDP port the host's kernel picks, and it writes the port to the run's host directory; the guest reaches it through slirp as 10.0.2.2 at the same port, which is why sntp learned `host:port`. It stops after 900 s or as soon as the run's directory is removed (the harness removes it when the run ends), whichever is first. It opens no other port, and nothing listens on any address but the loopback.

The rest of the suite is unchanged: pf is off and both jobs are Disabled in every image, so no other test sees pf or reaches an NTP server.

## Findings

| Finding | Fix |
|---|---|
| PF was already in the SBSA kernel (`MASTER.arm64.MacOSX`'s `BASE`), with `/dev/pf` | no kernel patch (none of 0038–0045 used) |
| xnu's `pf_ruleset.c` no longer builds for userland: its `#ifndef KERNEL` half has bit-rotted (bounds-safety annotations, `pf_lock` assertions, `strbufcmp`) | OpenBSD 4.3's `sys/net/pf_ruleset.c` (1.1, the revision xnu's derives from), with xnu's ruleset numbers for dummynet and NAT64 (patch 0002) |
| xnu's `pfvar.h` declares the ruleset functions for the kernel only (`KERNEL_PRIVATE`); without a prototype, `pf_find_or_create_ruleset()`'s pointer would be truncated to `int` (the toolchain's `-Wno-error=int-conversion` lets that through) | prototypes in `nd_pfctl_compat.h`; the build was checked with `-Wint-conversion` and `-Wimplicit-function-declaration` clean |
| macOS's pf.conf uses `scrub-anchor` and `dummynet-anchor`, which OpenBSD 4.3's grammar lacks (`syntax error` at the first line) | patch 0002 |
| `TAILQ_END` (OpenBSD) is missing from Darwin's `<sys/queue.h>`: the optimizer compared a pointer with an implicitly declared function's `int` | compat header |
| OpenBSD's ALTQ headers carry the four-clause BSD licence (Sun's CBQ, LBL's class queueing) | ALTQ isn't built; xnu has none |
| ntp-139's libntp target builds both `systime.c` and `systime_s.c` (ntpdsim's), which define the same symbols | `systime_s.c` left out |
| ntp's `ntp_md5.h` wants CommonCrypto on every Apple platform; `os_trace_debug` isn't in the trace stand-in | sntp patch 0001 |
| OpenBSD 4.3's `print_rule()` prints any action past `PF_NORDR` as `action(N)`; dummynet anchors printed `action(11)` | the bound is xnu's last action (patch 0002) |
| `pfctl -nv` prints an anchor rule with the anchor's last path component (`anchor "/*"` for `com.apple/*`): OpenBSD 4.3's own verbose print. The rule loaded and the kernel's copy (`pfctl -s rules`) have the whole path | left as upstream |
| A blocked outbound ping gets no `sendto` error: pf drops the packet after `ip_output` accepted it, so ping reports timeouts and `100.0% packet loss` | expected; the test reads ping's exit status |
| sntp can't name a port | sntp patch 0002 (`host:port`) |

## Limits

- pfctl is OpenBSD 4.3's: no `match` rules, `divert-to` or `rdr-to`/`nat-to` syntax (OpenBSD 4.7 and later). OpenBSD 4.3's pf.conf(5) is the reference, plus `scrub-anchor` and `dummynet-anchor`; the man pages aren't installed yet.
- No ALTQ, no dummynet pipes (`dnctl`), no NAT64 rules from pfctl; no `pflog` reader (`tcpdump -i pflog0` needs libpcap, not built).
- `launchctl load -w` doesn't persist yet (no overrides database), so a job turned on that way is off again after a reboot; edit the plist's `Disabled` key to keep it on.
- The NTP client steps or slews once per run; it doesn't discipline the clock's frequency as ntpd does, and it serves no time. No NTS, no authentication by default (`-a`/`-k` work with a key file).
