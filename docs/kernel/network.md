<!-- SPDX-License-Identifier: BSD-2-Clause -->
# Bring-up Ethernet: IONetworkingFamily, virtio-net, DHCP, a resolver and the TC956x (P1-19)

**P4-24, libpcap and tcpdump** ("libpcap and tcpdump", below): Apple's `libpcap.A.dylib` (all 200 of macOS 26's exports), `libipsec.A.dylib` and `/usr/sbin/tcpdump`; traceroute's TCP probes work, and root captures on en0 (`/dev/bpf*` are root's, 0600, as on macOS).

**P4-24, IPv6 DNS** ("IPv6 DNS: RDNSS and DHCPv6", below): name servers from router advertisements (RDNSS, DNSSL; FreeBSD's `rtsold`, which replaces 726's `rtsol`) and DHCPv6 (`dhcp6c`, FreeBSD's `net/dhcp6` port, stateless on the O flag, stateful on M), merged with the DHCP lease's by FreeBSD's `resolvconf(8)` into `/var/run/resolv.conf`; mDNSResponder asks IPv6 servers too. On slirp en0 gets fec0::3 by DHCPv6.

**P4-24, the rest of network_cmds and IPv6** ("P4-24: the rest of network_cmds and IPv6", below): `arp`, `ndp`, `ping6`, `traceroute` and `traceroute6` (setuid), `rtsol`, `rarpd`, `spray` and `kdumpd` from network_cmds-726, installed as macOS installs them; en* interfaces **autoconfigure IPv6** at boot, netconfigd making IPConfiguration's interface requests and running `rtsol`. On slirp en0 gets fec0::/64 addresses and an IPv6 default route, and `ping6` reaches the gateway.

**P4-24, rtadvd** ("rtadvd", below): FreeBSD's `usr.sbin/rtadvd` as `/usr/sbin/rtadvd` with `/etc/rtadvd.conf` (network_cmds-726 has none), adapted to xnu's routing socket, ND interface information and option structures. `//kernel:sbsa_rtadvd_test` boots two guests on a private segment (the harness's new `--link-net`/`--peer-net`): the router advertises 2001:db8:1::/64 with RDNSS and DNSSL, the host autoconfigures through netconfigd, and each pings the other.

**P1-19, checkpoint 3** ("Checkpoint 3: the Radxa Dragon Q8B's TC956x", below): a driver for the board's **Toshiba TC956x** PCIe Ethernet (PCI 1179:0220, two functions, one MAC each), `NeoDarwinTC956x`, with its hardware logic ported from FreeBSD's `tcx` (written for and run on this board) into a C header that also builds on the host. QEMU has no TC956x, so it is proven by a **register model** of the chip (`//kernel/neodarwin/network:tc956x_sim_test`, FreeBSD's bring-up order with an SError for any access to a block in reset, the SerDes/PCS/PHY link machine at every speed, the rings, interrupts and offloads) and **29 planted bugs** it must catch (`tc956x_mutation_tests`). The kernel builds with it and every QEMU test is unchanged. The board exit (a DHCP lease on the TC956x and ssh from the Mac) waits for board time: P1-19 stays *doing*.

**P1-19, checkpoint 2** ("Checkpoint 2: DHCP and name resolution", below): en0 takes a **DHCP lease at boot** (FreeBSD's `dhclient`, run by NeoDarwin's `netconfigd` launchd job), `/etc/resolv.conf` names the server the lease gives, and **names resolve** through Libinfo as on macOS: `/etc/hosts`, then **mDNSResponder** (the published POSIX daemon) for unicast DNS and `.local`; **libresolv-93** is built and OpenSSH links it again. On QEMU `virt` (MSI-X and INTx) and `sbsa-ref` the guest gets 10.0.2.15 from slirp, resolves a name and the host logs in over ssh.

**Checkpoint 1.** The SBSA kernel has an Ethernet stack: Apple's open **IONetworkingFamily**, the family every macOS Ethernet driver publishes through, with a NeoDarwin **virtio-net** driver under it. Its interface attaches to the BSD stack as **en0**, root configures it by hand (`ifconfig en0 inet 10.0.2.15/24 up`, `route add default 10.0.2.2`), and on QEMU's user-mode network the gateway answers `ping`, and **the host logs in over ssh** with a key and copies a file back, on `virt` (MSI-X and INTx) and on `sbsa-ref`.

It is a **bring-up aid**, kept small and easy to replace, as P1-18's console keyboard is (`usb-console.md`). P3-08's network dexts (virtio-net, e1000/igb, Realtek `re`, and `netd`; `docs/architecture/drivers.md`) replace the in-kernel drivers, and this checkpoint's patch 0036 goes with them. Whether IONetworkingFamily stays under the dexts (on macOS, DriverKit's Ethernet drivers reach the stack through the closed NetworkingDriverKit and Skywalk instead) is P3-08's decision. Checkpoint 2 adds DHCP and a resolver, checkpoint 3 the Radxa Dragon Q8B's Toshiba TC956x, a port of FreeBSD's `tcx` (below, "What checkpoints 2 and 3 need").

## Pieces

| Where | What |
|---|---|
| `@apple_ionetworkingfamily` (`MODULE.bazel`) | IONetworkingFamily **186**, from the macOS 26.0 release set (distribution-macOS `macos-260`), pinned by SHA-256 (`kernel/upstream.lock`). APSL 2.0 (`THIRD_PARTY_NOTICES.md`). All twelve sources, and the headers overlaid at `iokit/ndnet/include/IOKit/network`, where kexts find them in the SDK |
| `kernel/neodarwin/network/NeoDarwinVirtioNet.cpp` | the virtio-net driver, an `IOEthernetController` |
| `kernel/neodarwin/network/nd_virtio_net.h` | the network device's features, configuration and packet header |
| `kernel/neodarwin/network/NeoDarwinInterfaceNamer.cpp` | names interfaces (en0, en1, ...) until there is a user-space namer |
| `kernel/neodarwin/virtio/NeoDarwinVirtioPCI.h`, `nd_virtio.h` | the virtio 1.x modern PCI transport and split virtqueue, shared with virtio-blk (moved out of `kernel/neodarwin/storage`, below) |
| `kernel/neodarwin/storage/NeoDarwinStorageDMA.h` | the DMA policy (`dma-coherent`, `dma-address-bits`) PCI drivers share |
| patches 0034–0036 | the virtio transport's search path (0034); IONetworkingFamily's build list, search paths, the kext's view of the kernel and IONetworkStack's personality (0035); the driver's and the namer's (0036) |
| `kernel/neodarwin/network/NeoDarwinTC956x.cpp`, `nd_tc956x.h`, `test/tc956x_sim_test.c` | checkpoint 3: the TC956x driver, its FreeBSD-derived hardware core and the core's register model (below) |
| patch 0037 | the TC956x driver's build-list line, search paths and personality |
| `tools/efi/qemu_efi_test.sh --user-net`, `--hostfwd`, `--host-setup`, `--host-cmd-after` | a NIC on QEMU's user-mode network, one forward from the host's loopback, and commands the test runs on the host |

Everything is compiled into the kernel, as IOPCIFamily and IOStorageFamily are, because `kcgen` links no kexts until M5. The overlay puts NeoDarwin's files at `iokit/ndnet`, IONetworkingFamily at `iokit/ndnet/IONetworkingFamily`, the virtio transport at `iokit/ndvirtio`.

## The stack

```
IOPCIDevice 1af4:1041                     (IOPCIFamily, pci.md)
  NeoDarwinVirtioNet                      IOEthernetController
    IOEthernetInterface   en0             ifnet_allocate_extended, ifnet_attach (BSD: ether, inet, inet6)
IOResources
  IONetworkStack                          names interfaces, attaches them to BSD
    NeoDarwinInterfaceNamer               asks it to, for every new interface
```

## What of IONetworkingFamily builds

IONetworkingFamily is fully open: every source of the kext builds for arm64 in the kernel. On macOS, DriverKit's NetworkingDriverKit (`IOUserNetworkEthernet`) and Skywalk's native drivers live elsewhere and are closed; nothing here needs them.

| Source | On NeoDarwin |
|---|---|
| `IONetworkController`, `IOEthernetController` | built: the controller drivers subclass |
| `IONetworkInterface`, `IOEthernetInterface` | built: the BSD side. `IONetworkInterface::attachToDataLinkLayer` makes the ifnet with `ifnet_allocate_extended` and `ifnet_attach`; input is `ifnet_input_extended`, in batches |
| `IONetworkStack` | built, with its Info.plist personality (on IOResources once IOBSD is published) |
| `IOOutputQueue` (`IOBasicOutputQueue`, `IOGatedOutputQueue`), `IOPacketQueue`, `IOMbufMemoryCursor` | built |
| `IONetworkData`, `IONetworkMedium`, `IONetworkUserClient` | built |
| `IOKernelDebugger` | built (IONetworkController refers to it); its `IOKDP` personality is left out: no driver attaches a debugger client, so there is no KDP over Ethernet |

| Finding | Fix |
|---|---|
| The family is kext code, compiled by its Xcode project with `__PRIVATE_SPI__` against Kernel.framework. In xnu's iokit, with `XNU_KERNEL_PRIVATE`, `<sys/mbuf.h>` and `<net/dlil.h>` expose BSD's internals: inline functions that take `mbuf_t` as `struct mbuf *` (true only under `BSD_BUILD`), templates in `extern "C"`, Skywalk's headers | patch 0035's `ND_KEXT_VIEW`: `-D__PRIVATE_SPI__ -UXNU_KERNEL_PRIVATE -UIOKITSTATS`, for the family and everything that includes its headers (the driver and the namer), so its classes are laid out alike everywhere. `IOKITSTATS` only adds friends and a private header; the classes' layouts don't depend on it |
| `<net/dlil.h>` includes `<net/classq/classq.h>`, which Kernel.framework's PrivateHeaders carry and xnu's `EXPORT_HDRS` don't | `-idirafter $(SRCROOT)/bsd`: found in the source tree, after every other directory |
| On macOS an interface waits for configd's InterfaceNamer to name it; only a network root is named in the kernel (`IOKitBSDInit.cpp`, `IORegisterNetworkInterface`) | `NeoDarwinInterfaceNamer` ("Naming") |

## Naming

`IONetworkStack` takes every published `IONetworkInterface` as its client and leaves it unnamed until someone registers it with a unit, through the stack's properties (`IONetworkStackUserCommand`, `IOInterfaceUnit`, and the interface's registry entry ID or path). configd's InterfaceNamer does that on macOS, persistently (`NetworkInterfaces.plist`, by location); user space needs the `com.apple.networking.ionetworkstack.user-client` entitlement. NeoDarwin has no configd, so `NeoDarwinInterfaceNamer` (on `IONetworkStack`, a match category of its own) watches interface publications and, from a thread call, registers each new one with the lowest free unit of its prefix (`kIONetworkStackRegisterInterfaceWithLowestUnit`, unit 0): what `IORegisterNetworkInterface` does for a network root. It runs in the kernel task, so the entitlement check doesn't apply. It waits until the stack has taken the interface (the stack's own publication handler may run after the namer's), and leaves alone an interface that already has a BSD name (the stack publishes it again after the attach). The stack then sets `BSD Name`, attaches the interface to BSD and publishes it again.

```
NeoDarwinInterfaceNamer: en0: IOEthernetInterface of NeoDarwinVirtioNet
```

Units go in the order interfaces appear, and are not persistent: with two NICs, which is en0 depends on which driver starts first. A user-space namer with a persistent table (configd's, or NeoDarwin's own in P3-08's `netd`) replaces it; `nd_ifnamer=0` turns it off, and interfaces then stay unnamed.

## The virtio transport, shared

P1-10's virtio-blk driver had the modern PCI transport and the split ring inside it. Both drivers now use `kernel/neodarwin/virtio` (patch 0034):

- `nd_virtio.h`: the capabilities, common configuration, status, the device-independent feature bits and the split ring's layouts. The block device's layouts moved to `storage/nd_virtio_blk.h`, the network device's are `network/nd_virtio_net.h`.
- `NeoDarwinVirtioPCI.h`, header-only C++: `NDVirtioPCI` (find and map the vendor capabilities, reset, ACKNOWLEDGE and DRIVER, negotiate features with VERSION_1 required and ACCESS_PLATFORM/ORDER_PLATFORM taken when offered, the configuration generation, queue set-up, MSI-X vectors with their read-back, the ISR for INTx, the log's interrupt description) and `NDVirtqueue` (one split ring below `dma-address-bits`, descriptors and the available ring on the first pages, the used ring on its own page; `push`, `publish` with its barriers and doorbell, `nextUsed`).

NeoDarwinVirtioBlock's behaviour and log line are unchanged; the storage tests are the proof. One difference: a queue's size is now rounded down to a power of two (the ring indices wrap at 2^16), which every device QEMU or a board presents already is.

## The virtio-net driver

`NeoDarwinVirtioNet` matches `IOPCIMatch` `0x10411af4 0x10001af4`: modern virtio-net, and transitional (QEMU's `virtio-net-pci` on a root bus), whose modern capabilities it uses, as virtio-blk does. On QEMU `virt` the machine's own default NIC is a transitional virtio-net-pci on QEMU's user network at 00:01.0, so every `virt` boot without `--user-net` now drives it too (its interface is named en0 and left down). The boot-arg `nd_virtio_net=0` keeps the driver off.

**Initialisation** (§3.1.1): reset, ACKNOWLEDGE, DRIVER, features, FEATURES_OK, the configuration, the receive and transmit queues, interrupts, queue enable, DRIVER_OK; then the receive ring is filled, the medium published and the interface attached.

| Feature | Taken | Use |
|---|---|---|
| `VERSION_1` (32) | required | modern device; the packet header is always 12 bytes |
| `ACCESS_PLATFORM` (33), `ORDER_PLATFORM` (36) | if offered | as for virtio-blk |
| `MAC` (5) | if offered | the station address; without it a locally administered one is made up (`02:4e:44:...`) and the log says `(random)` |
| `STATUS` (16) | if offered | link state; a configuration interrupt re-reads it. Without it the link is always up |
| `SPEED_DUPLEX` (63) | if offered | the medium's speed, when the device knows it (QEMU's doesn't by default) |
| `CSUM`, `GUEST_CSUM`, TSO/UFO, `MRG_RXBUF`, `CTRL_VQ`, `CTRL_RX`, `MQ`, event index, indirect, packed | no | no offloads: the stack computes checksums; each receive buffer holds a whole frame; one queue pair |

QEMU 11.1's virtio-net offers far more; the driver takes VERSION_1, STATUS and MAC: `features 0x100010020` (QEMU offers neither ACCESS_PLATFORM nor, without a `speed` property, SPEED_DUPLEX).

**Receive.** Up to 128 buffers (256-entry ring, two descriptors each): the 12-byte `virtio_net_hdr` in driver memory, each in its own 64-byte line, then the frame, written straight into an mbuf cluster from `allocatePacket(2048)`, 2 bytes in (`ETHER_ALIGN`: the IP header after the 14-byte Ethernet header is 4-byte aligned, so the stack never copies it to realign). Without `MRG_RXBUF` or the guest offloads, a buffer must hold a whole frame (§5.1.6.3.1, at least 1526 bytes); these hold 2046. Completions are handed to the interface in one batch per interrupt (`inputPacket` with `kInputOptionQueuePacket`, then `flushInputQueue`, which is one `ifnet_input_extended`), and each slot gets a fresh mbuf. If none can be had, or the interface isn't enabled, the frame is dropped and its mbuf goes back on the ring: the ring never runs dry.

**Transmit.** `IOGatedOutputQueue` (256 packets) calls `outputPacket` on the driver's work loop. Fixed chains of 8 descriptors (32 slots on a 256-entry ring): the header (all zeroes: no offload, no GSO), then up to 7 segments of the mbuf chain from an `IOMbufNaturalMemoryCursor`, which splits at page boundaries and coalesces a longer chain. A full ring returns `kIOReturnOutputStall`; completions free slots and restart the queue (`service(kServiceAsync)`). Every interrupt reaps the transmit ring, so mbufs are freed promptly.

**DMA** (`NeoDarwinStorageDMA.h`). The rings, headers and bounce buffers are physically contiguous and below `dma-address-bits`. mbuf data is cleaned to the point of coherency before the device reads it, and cleaned and invalidated before a receive buffer is posted and again before the CPU reads the frame, when the device isn't `dma-coherent` (coherent: `DMB` only). mbufs are addressed directly, so they must be within the device's reach: a transmit packet with a segment above `dma-address-bits` (or outside the mbuf map, where `mbuf_data_to_physical` answers 0) is copied into its slot's 2 KiB bounce buffer; with an address limit under 64 bits each receive slot also has a bounce buffer, used when its cluster is out of reach, and the frame is copied into the mbuf. QEMU's devices reach all memory, so neither path runs there; they are the TC956x's (36-bit) business.

**Interrupts.** MSI-X first (`configureInterrupts(kIOInterruptTypePCIMessagedX, 1, 3)`), before anything resolves the device's interrupts: vector 0 configuration changes, 1 the receive queue, 2 the transmit queue. With two vectors both queues share vector 1; with one, everything is on vector 0. Without MSIs (`nd_pci_msi=0`, no ITS), INTx, level and possibly shared, through an `IOFilterInterruptEventSource` whose filter reads the ISR status (which deasserts the line) and claims the interrupt only if a bit was set. Each queue logs its first completion by its interrupt once, the tests' proof that its vector works.

**Filters.** Without `CTRL_RX` the device can't be told to filter; QEMU's model starts promiscuous and with every multicast. `setPromiscuousMode`, `setMulticastMode` and `setMulticastList` succeed without doing anything: the stack drops what isn't for it.

**Counters.** The driver's own, as the registry property `Statistics` (rx/tx packets, bytes, dropped, bounced and interrupts, tx stalls), current whenever the registry is read (`ioreg`, once it is in the image). IONetworkingFamily's: input errors (dropped frames) and output packets and errors in its `IONetworkStats`. `netstat -I en0` shows output packets, but its input packets stay 0 (below, "Findings").

**The log** (`virt`, MSI-X):

```
NeoDarwinPCIHostBridge: 0000:00:01.0 1af4:1041 class 020000 bar1 mem 0x10041000+0x1000 bar4 mem pf 0x8000004000+0x4000 INTA gsiv 36 msi-x 4
NeoDarwinPCIMSI: 0000:00:01.0: MSI-X 3 of 4 vectors -> LPIs 8192-8194, DeviceID 0x8 on ITS 0
NeoDarwinVirtioNet: 00:01.0: virtio-net 1af4:1041: MAC 52:54:00:12:34:56; features 0x100010020; link up; rx queue 256 (128 buffers of 2046 bytes), tx queue 256 (32 slots of 7 segments); MSI-X 3 vectors (LPIs 8192-8194); DMA coherent, 64 address bits
ifnet_attach: All kernel threads created for interface en0 have been scheduled at least once. Proceeding.
NeoDarwinInterfaceNamer: en0: IOEthernetInterface of NeoDarwinVirtioNet
    (root: ifconfig en0 inet 10.0.2.15/24 up)
NeoDarwinVirtioNet: 00:01.0: en0: link up
skip attaching fsw to en0 using legacy TX model
NeoDarwinVirtioNet: 00:01.0: tx queue: first completion by its interrupt (MSI-X vector 2, LPI 8194)
NeoDarwinVirtioNet: 00:01.0: rx queue: first completion by its interrupt (MSI-X vector 1, LPI 8193)
```

## Configuration

Checkpoint 1 configured en0 by hand. QEMU's user-mode network (slirp) is 10.0.2.0/24: the guest is 10.0.2.15, the gateway (and the host, as seen from the guest) 10.0.2.2, the DNS proxy 10.0.2.3.

```
ifconfig en0 inet 10.0.2.15/24 up
route -n add default 10.0.2.2
```

`ifconfig en0 up` enables the interface (`IONetworkController::enable`), which starts the output queue and reports the link (`en0: link up`). Since checkpoint 2, netconfigd does this at boot and dhclient takes the lease (below); a static configuration is a line in `/etc/netconfigd.conf`.

## Tests

| Target | Machine | Asserts |
|---|---|---|
| `//kernel:sbsa_net_test` | `virt`, `virtio-net-pci,disable-legacy=on` (1af4:1041) on the user network, MSI-X | the driver's line (MAC 52:54:00:12:34:56, three MSI-X vectors), en0 named; **the lease** (checkpoint 2): netconfigd starts `dhclient -d en0`, `DHCPACK from 10.0.2.2`, `bound to 10.0.2.15`; `ifconfig en0` (ether, inet 10.0.2.15/24, `status: active`), the default route via 10.0.2.2, `nameserver 10.0.2.3` in `/etc/resolv.conf`, `ping -c 2 10.0.2.2` (2 of 2); **names**: `localhost` (`/etc/hosts`), `nd-test.local` (mDNS, a record `dns-sd -P` registers) and, when the host can resolve it, `example.com` through slirp's DNS proxy; the first receive and transmit completions on vectors 1 and 2; **ssh from the host**: `uname -a` says `Darwin`, a 1.5 MB file copies back whole; `netstat -I en0`; sshd logs the publickey login from 10.0.2.2 |
| `//kernel:sbsa_net_intx_test` | `virt`, `nd_pci_msi=0`, transitional `virtio-net-pci` (1af4:1000) | the same on INTx |
| `//kernel:sbsa_ref_net_test` | `sbsa-ref` (TF-A, SbsaQemu, four CPUs), `virtio-net-pci,disable-legacy=on` on its PCIe root bus | the same, with MSI-X through the ITS via the SMMUv3 in bypass |

Checkpoint 1's tests configured en0 by hand (`ifconfig`, `route`); since checkpoint 2 the same three tests take the lease at boot and resolve names ("Checkpoint 2", "Tests").

Until patch 0038, `sbsa_ref_net_test` panicked about one run in five just after the lease (`Ticket spinlock[...] timeout` on `pset0.sched_lock`, after "failed to halt" for the other CPUs). Neither the driver nor the ITS was involved: XNU's lock and debugger timeouts are 24 MHz tick counts, 41.7 times too short at sbsa-ref's 1 GHz counter, and the network's work at boot (slirp and virtio-net under QEMU's iothread lock, the extra processes) held a vCPU past 3 ms (`arm64-sbsa-bringup.md` §2.1.9).

**ssh from the host.** The harness runs the host's own `ssh`, and keeps the run sealed:

- `--host-setup` makes an ed25519 key with `ssh-keygen` in a directory of the run's own (also `HOME` for host commands), deleted with the run.
- In the guest, root waits for the lease (`/var/run/resolv.conf` written), then runs `launchctl load -w /System/Library/LaunchDaemons/ssh.plist` (Remote Login, socket-activated) and writes the public key to `/Users/test/.ssh/authorized_keys`: the harness types it, from `{hostfile:id_ed25519.pub}`.
- `--user-net ...` with `--hostfwd 22` adds `-netdev user,hostfwd=tcp:127.0.0.1:PORT-:22`. PORT is a free port the host's kernel picks for this run; the forward listens on the loopback only and closes with QEMU. No other host port is opened.
- `--host-cmd-after 'key-42' 'ssh ... uname -a'` runs `ssh -F /dev/null -i id_ed25519 -o IdentitiesOnly=yes -o IdentityAgent=none -o BatchMode=yes -o StrictHostKeyChecking=no -o UserKnownHostsFile=/dev/null -p $ND_HOSTFWD_PORT test@127.0.0.1`: no user configuration, agent or known hosts, never a prompt. The first connection waits while `sshd-keygen-wrapper` makes the guest's host keys.
- Its output joins the log as `host: ...` lines (`host: Darwin localhost 25.0.0 ...`, `host: exit 0`), which the test expects like serial lines. Commands are the test's own text; nothing the guest prints goes into them. Each has a time limit (180 s), and is killed with QEMU.
- The throughput check copies `/usr/lib/libcrypto.3.dylib` (4.7 MB, OpenSSL's libcrypto since 2026-10-01; LibreSSL's 1.5 MB `libcrypto.46.dylib` before) over ssh and compares its size with the guest's `wc -c`. It takes about two seconds on every machine (`host: copied 4701360 of 4701360 bytes in 2 s`), TCG included; a sanity check, not a benchmark.

**ICMP to the gateway.** QEMU 11.1's slirp answers echo requests to 10.0.2.2 (TTL 255, a millisecond or so), without privileges on the host, so `ping` is the first check.

**The machines' own NICs.** QEMU adds a default NIC only when no `-netdev` or `-nic` is given. `--user-net` therefore replaces `virt`'s default virtio-net (the test's device takes 00:01.0) and `sbsa-ref`'s built-in e1000e (absent; bochs-display stays at 00:01.0, the test's device is 00:02.0). Every other `virt` test still has the default NIC, a transitional virtio-net on QEMU's user network, which the driver now drives (named en0, never brought up); `sbsa-ref`'s e1000e has no driver.

## Findings

| Finding | Fix |
|---|---|
| IONetworkingFamily doesn't compile in xnu's iokit as-is: under `XNU_KERNEL_PRIVATE`, BSD's internal headers don't build as C++ there | the kext's view of the kernel (patch 0035, "What of IONetworkingFamily builds") |
| Interfaces wait for a user-space namer that doesn't exist | `NeoDarwinInterfaceNamer` ("Naming") |
| `netstat -I en0` reports 0 input packets while traffic flows. The SBSA kernel's Skywalk default (`IF_ATTACH_NX_DEFAULT` for a non-macOS target: `SKYWALK_NETWORKING_ENABLED`) attaches a netif *compat* nexus to every Ethernet interface, which sets `IFCAP_SKYWALK`; the DLIL input thread then skips its stats sync ("the stats are already incremented there", `dlil_input.c`), but IONetworkingFamily's input goes through `ifnet_input_extended` and that thread, so the counts are dropped. Output counts are unaffected. Confirmed: with the boot-arg `if_attach_nx=2` (flowswitch only, no compat netif) `netstat -I en0` counts input (4 packets after a 3-echo ping) | not NeoDarwin's code; the driver's `Statistics` has the real receive counts. **Decided in checkpoint 2: the default stays** ("Checkpoint 2", "Decisions") |
| The harness's first host-command hook only looked at its child when the next step was due, so a step waiting for `host: exit` never fired | `host_poll` on every pass of the watchdog loop; a timed-out command reports `host: exit 124` |
| Both `sbsa-ref`'s e1000e and `virt`'s default NIC vanish with `-netdev` | expected (QEMU's default NIC rule); the tests' PCI addresses are not asserted |

## Limits

- One queue pair, no multiqueue, no offloads (checksums, TSO), no `MRG_RXBUF`, no jumbo frames (MTU 1500), no control queue (no MAC filtering, VLAN filtering or MAC address change: `setHardwareAddress` is IONetworkingFamily's default, unsupported).
- No hot unplug handling beyond `stop` resetting the device; no `NEEDS_RESET` recovery (logged).
- No power management, no Wake-on-LAN.
- No KDP over Ethernet.
- Interface units are not persistent (the namer).

## Checkpoint 2: DHCP and name resolution

### Decisions

| Question | Decision | Why |
|---|---|---|
| DHCP client | **FreeBSD's `dhclient`** (`sbin/dhclient`, pinned file by file at the commit libm and libxo use: `base/dhclient/freebsd.lock`), with its `dhclient-script` | The reuse order (`docs/repository.md` §3.1): Apple's client, `IPConfiguration` in bootp-527, is a configd plugin and needs configd and SystemConfiguration, which NeoDarwin doesn't build; FreeBSD's comes before new code. It is the OpenBSD-derived ISC client, in production for twenty years, with privilege separation, lease files, renewal and rebinding, classless static routes, and an exit-hook script for local policy. A first-party Swift client would be new protocol code to get right and keep right. Its needs are small: BPF (xnu has `/dev/bpf*`), a routing socket, `sh`, `ifconfig` and `route`, all in the base. The port is compat headers for Capsicum and Casper (`base/dhclient/compat`) and two patches |
| What brings interfaces up | **`netconfigd`**, a small first-party Embedded Swift launchd job (`com.neodarwin.netconfigd`, KeepAlive), with `/etc/netconfigd.conf` (FreeBSD `rc.conf` style: `ifconfig_<if>="DHCP"`, a static `ifconfig` line, or `NONE`; `ifconfig_DEFAULT`; `defaultrouter`) | dhclient serves one interface per process and exits when it loses the interface; something has to find the interfaces, start a client for each and restart it. On macOS configd's IPConfiguration does this. A launchd job, loaded by `launchctl bootstrap` with the other LaunchDaemons, keeps launchctl's bootstrap to what launchctl-842's does (lo0 only), handles en* interfaces present at boot and those that appear later (a routing-socket message or a 5-second scan), and needs no boot-arg |
| Resolver | **(a) mDNSResponder's daemon**, from the published `mDNSPosix` sources of mDNSResponder-2881.0.25, installed as `/usr/sbin/mDNSResponder` with `dns-sd`; plus **libresolv-93** (`/usr/lib/libresolv.9.dylib`) | macOS fidelity: on macOS every `getaddrinfo`, `gethostbyname` and Libinfo `res_query` goes to mDNSResponder through `libsystem_dnssd` (Libinfo's mdns module), and the client library was already built. The published POSIX daemon has the same core (`mDNSCore`), the same client server (`uds_daemon.c`) and wire protocol, does unicast DNS from `/etc/resolv.conf` and `.local` by multicast, and built with one patch. (b), a libresolv module patched into Libinfo, would be a NeoDarwin-only resolver path to maintain and would leave `libsystem_dnssd`, `dns-sd` and `.local` dead. libresolv is the macOS 26.0 library for the programs that call it directly (`res_9_query`, `dns_*`), OpenSSH's SSHFP lookups among them |
| `/etc/resolv.conf` | macOS's: a link to `/var/run/resolv.conf` (the image's), written by `dhclient-script` and reread by mDNSResponder on `SIGHUP`; since P4-24's IPv6 DNS, written by `resolvconf(8)`, which merges the DHCP lease's servers with IPv6's ("IPv6 DNS: RDNSS and DHCPv6") | `/var/run` is emptied at boot, so a stale lease's servers never survive a reboot |
| OpenSSH patch 0003 (SSHFP off) | **dropped**: OpenSSH links libresolv (`-lresolv`) as Apple's build does | libresolv exists now |
| `if_attach_nx` default | **kept** (the kernel's Skywalk default, a compat netif on every Ethernet interface); `netstat -I` undercounts input packets on IONetworkingFamily interfaces, documented ("Findings") | The input-counter loss is cosmetic: the driver's `Statistics` and `netstat -I`'s output counts are right, and DHCP, BPF and the stack don't depend on it. The default is xnu's own for an arm64 non-macOS kernel (macOS's own default also attaches compat netifs), and changing it from neoboot would make NeoDarwin's stack differ from the one Apple tests, for one counter. P3-08 decides again, with its dexts, which on macOS reach the stack through Skywalk |
| The interface namer | **stays in the kernel** (`NeoDarwinInterfaceNamer`) | Naming has to happen before anything in user space can see the interface; a user-space namer with a persistent table is a configd- or `netd`-sized job (P3-08). netconfigd only configures interfaces that already have names |

### Pieces

| Where | What |
|---|---|
| `//base:dhcp_client` (`base/dhclient`) | `/sbin/dhclient`, `/sbin/dhclient-script`, `/etc/dhclient.conf` from `@freebsd_dhclient` (`base/dhclient/freebsd.lock`). `compat/`: Capsicum and Casper as no-ops (`sys/capsicum.h`, `capsicum_helpers.h`, `libcasper.h`, `casper/cap_syslog.h`), `sys/endian.h`, and `nd_dhclient_compat.h` (nitems, timespec arithmetic, `setproctitle`, `daemonfd`, `reallocarray`). Patches 0001 (xnu's BPF and routing socket, Darwin's pipes and `setuid`) and 0002 (`dhclient-script`) |
| `//base/netconfigd` | `/usr/libexec/netconfigd` (Embedded Swift), `com.neodarwin.netconfigd.plist`, `/etc/netconfigd.conf` (in the images: `images/BUILD.bazel`, `_NETWORK_FILES`) |
| `//base:mdns_responder` (`base/mdnsresponder/daemon.sh`) | `/usr/sbin/mDNSResponder` (mDNSPosix's `mdnsd`: `PosixDaemon.c`, `mDNSPosix.c`, `mDNSCore`, `uds_daemon.c`, DSO), `/usr/bin/dns-sd`, `com.apple.mDNSResponder.plist` (KeepAlive, `-foreground`, log `/var/log/mDNSResponder.log`). `daemon-patches/0001` |
| `//base:libresolv_dylib` (`base/libresolv`) | `/usr/lib/libresolv.9.dylib` (155 exports, the same as the macOS 26 SDK's `libresolv.9.tbd`), build-only headers in `usr/local/libresolv/include`. Patch 0001 |
| `@apple_configd_dnsinfo` (`base/libresolv/configd.lock`) | configd-1385.0.7's `dnsinfo.h`, which libresolv's `dns.c` builds against |
| `base/standins/libsystem_configuration` | `dns_configuration_copy()` (no configuration: `NULL`, as with no configd), `dns_configuration_free()`, `dns_configuration_notify_key()` |
| `images/BUILD.bazel` | `/etc/resolv.conf` -> `/var/run/resolv.conf`; netconfigd in the session images |

### The boot flow

```
launchd (PID 1)
  launchctl bootstrap -S System            lo0 (127.0.0.1, ::1); empties /var/run
    com.apple.mDNSResponder                /usr/sbin/mDNSResponder -foreground
        reads /etc/resolv.conf (none yet), listens on /var/run/mDNSResponder, drops to nobody
    com.neodarwin.netconfigd               /usr/libexec/netconfigd
        en0: ifconfig en0 up; dhclient -d en0
          dhclient [priv]                  BPF: DHCPDISCOVER, OFFER, REQUEST, ACK
            dhclient-script BOUND          ifconfig en0 inet 10.0.2.15 netmask 255.255.255.0 ...
                                           route add default 10.0.2.2
                                           resolvconf -a en0: nameserver 10.0.2.3
                                             /var/run/resolv.conf (merged), kill -HUP mDNSResponder
    com.apple.getty, ...
```

The lease log (`/var/log/netconfigd.log`, netconfigd's and dhclient's standard error):

```
netconfigd: en0: DHCP (dhclient -d en0, pid 12)
no such user: _dhcp, falling back to "nobody"
DHCPDISCOVER on en0 to 255.255.255.255 port 67 interval 6
DHCPOFFER from 10.0.2.2
DHCPREQUEST on en0 to 255.255.255.255 port 67
DHCPACK from 10.0.2.2
bound to 10.0.2.15 -- renewal in 43200 seconds.
```

### DHCP

dhclient runs in the foreground (`-d`) under netconfigd, which runs `ifconfig <if> up` first (dhclient waits for the link, and the driver reports it only once the interface is enabled) and restarts the client 10 seconds after it exits. dhclient splits into a privileged process (BPF writes, the script) and an unprivileged one (`nobody`, chrooted to `/var/empty`; FreeBSD's `_dhcp` user isn't in macOS's user database, hence the one-line warning). Leases persist in `/var/db/dhclient.leases.en0`; renewal and rebinding are dhclient's own. The script is FreeBSD's, adapted by patch 0002:
- Darwin's `ifconfig` has no `-n`; `arp` isn't built yet (P4-24), so the ARP flush is skipped.
- The name servers and search domain go to `resolvconf -a <interface>`, as on FreeBSD (`resolvconf_enable` YES, FreeBSD's default), and an expired lease takes them away (`resolvconf -d`). NeoDarwin's `resolvconf(8)` is FreeBSD's openresolv, which writes the merged `/var/run/resolv.conf` and sends `SIGHUP` to mDNSResponder ("IPv6 DNS: RDNSS and DHCPv6"). Until then patch 0002 wrote `/var/run/resolv.conf` itself (a `resolvconf_enable=NEODARWIN` mode).
- Local policy goes in `/etc/dhclient-enter-hooks` and `/etc/dhclient-exit-hooks`, as on FreeBSD; options in `/etc/dhclient.conf`.

A static interface: `ifconfig_en0="inet 192.0.2.10/24"` and `defaultrouter="192.0.2.1"` in `/etc/netconfigd.conf`; name servers then go in a plain `/etc/resolv.conf` replacing the link.

### Name resolution

Libinfo's search module asks, in order, its cache, the file module (`/etc/hosts` and the other `/etc` files) and the mdns module (`docs/base/libsystem.md`). The mdns module sends each query to mDNSResponder over `/var/run/mDNSResponder` (`DNSServiceQueryRecord`, with the failover attribute that `libsystem_dnssd` patch 0002 sends and the daemon ignores). mDNSResponder answers:
- unicast names from the servers in `/etc/resolv.conf` (`mDNS_AddDNSServer`; `GetServerForQuestion ... 10.0.2.3:53` in its log);
- `.local` names by multicast DNS on every interface, including records registered in the daemon itself (`dns-sd -R`, `-P`);
- `localhost` negatively (RFC 6761: never sent to a server; the file module has answered it already).

`dns-sd` is the published client (`Clients/dns-sd.c`), as `/usr/bin/dns-sd` on macOS. libresolv answers the programs that call it directly, from `/etc/resolv.conf` and `/etc/resolver/*`: its `dns.c` asks configd first (`dns_configuration_copy`), and the stand-in answers that there is no configuration, as Apple's does when configd isn't running.

What the published daemon lacks next to macOS's (closed since 1310): DNS over TLS and HTTPS (`POSIX_HAS_TLS` needs mbedtls), per-interface and scoped resolvers from configd (`/etc/resolver/*` is libresolv's only), DNSSEC validation, the XPC and Network.framework interfaces (`DNSServiceAttr*` setters over XPC, `nw_resolver`), and launchd socket activation (it makes its own socket; `Bonjour` keys in jobs' `Sockets` still aren't registered by launchctl).

### Tests

The three tests of checkpoint 1 (`//kernel:sbsa_net_test`, `sbsa_net_intx_test`, `sbsa_ref_net_test`) no longer configure en0: root waits for `/var/run/resolv.conf`, prints the lease log, and checks `ifconfig en0` (inet 10.0.2.15/24), `netstat -rn` (default via 10.0.2.2, `UGScg`), `nameserver 10.0.2.3`, `ping` to the gateway, and three lookups through Libinfo:

| Name | Path | Hermetic |
|---|---|---|
| `localhost` | `/etc/hosts` (the file module): `PING localhost (127.0.0.1)` | yes |
| `nd-test.local` | mDNSResponder: `dns-sd -P nd-test _nd-test._tcp local 7 nd-test.local 10.0.2.15` registers an address record; `ping` resolves it through `libsystem_dnssd`: `PING nd-test.local (10.0.2.15)` | yes: multicast DNS inside the guest |
| `example.com` | mDNSResponder, unicast, through slirp's DNS proxy (10.0.2.3), which forwards to the host's resolver | no: the guest's answer is checked only if the host's own `host -W 5 example.com` succeeds (`host: dns: example.com resolved`); without a network the test says `host: dns: skipped: ...` and passes. A guest failure while the host resolves fails the test (`--absent 'host: dns: FAILED'`) |

slirp has no local DNS records (only the proxy), so a hermetic unicast check would need a DNS server on the host, which the tests' rule (no host listeners but the loopback ssh forward) excludes. Unicast DNS is proven whenever the test host has a network; the `.local` check proves the Libinfo, `libsystem_dnssd` and daemon path every time. ssh from the host then works as in checkpoint 1, to the leased address.

Every other image with launchd now starts mDNSResponder and netconfigd too, so `virt` boots without `--user-net` take a lease on the machine's default NIC (also on slirp); `sbsa-ref` boots have no en* interface and netconfigd idles.

### Findings

| Finding | Fix |
|---|---|
| dhclient's DHCPDISCOVERs never left: xnu's BPF sends a frame written without "header complete" through the interface's `PF_INET` protocol, which isn't attached until the interface has an IPv4 address (FreeBSD's BPF doesn't care), so every write before the lease failed. The privileged process's error went to `/dev/null` | dhclient patch 0001: `BIOCSHDRCMPLT` on the write descriptor, as macOS's IPConfiguration sets (the frames carry their own Ethernet header); in the foreground the privileged process keeps standard error |
| After the OFFER: `buf_read: Bad file descriptor`, `short write`. dhclient's two processes talk both ways over a `pipe()`; FreeBSD's pipes are bidirectional, Darwin's aren't | patch 0001: a socket pair on Darwin |
| `can't drop privileges: Operation not permitted`: Darwin's `setuid()` without privilege takes only the real or saved ID, so FreeBSD's `seteuid()` then `setuid()` fails | patch 0001: `setuid()` as root, which sets all three |
| xnu's BPF has no `BIOCSETWF`, `BIOCLOCK`, `BIOCSETVLANPCP`; the routing socket no `RTM_IFANNOUNCE` or `RTM_IEEE80211`; there is no Capsicum, Casper or netlink | patch 0001 guards; `compat/`; `-DWITHOUT_NETLINK`. The unprivileged process's descriptors aren't limited (no Capsicum, no write filter): its sandbox is the chroot and `nobody` |
| `dhclient-script`'s `ifconfig -n` and `resolvconf(8)` are FreeBSD's | patch 0002; since "IPv6 DNS: RDNSS and DHCPv6" `resolvconf(8)` is built (FreeBSD's openresolv) and the script uses it unchanged |
| mDNSResponder's POSIX daemon rereads `resolv.conf` (start, `SIGHUP`) without the core's lock and logs "Lock failure ... caller: mDNS_AddDNSServer" | `daemon-patches/0001` |
| The mDNS core advertises the host name's address records only while a service with an automatic target is registered (`AdvertiseInterfaceIfNeeded`, `AutoTargetServices`), so `localhost.local` doesn't resolve on an idle system (the host name is `localhost`, too) | expected; the test registers its own record with `dns-sd -P` |
| `dns-sd -P` logs `getaddrinfo] dlopen("...libnetwork.dylib") failed`: Libinfo tries Network.framework's closed `libnetwork` for some lookups | expected; Libinfo falls back to its own path |
| libresolv-93 includes Apple-internal headers: `notify_private.h` (Libnotify), `dnsinfo.h` (configd), `md5.h` (CommonCrypto's MD5 under `MD5Init` names), and tests the dyld version set `dyld_2024_SU_E_os_versions`, which no published AvailabilityVersions (155, 157.2) defines | the first two from their projects (`@apple_libnotify`, the pinned configd header); HMAC-MD5 on FreeBSD's `md5c.c`, linked privately (no export); libresolv patch 0001 defines the set where it is missing |
| Bazel's downloader couldn't resolve github.com in this sandboxed session (curl could) | fetched with `--distdir` from files downloaded and checked against the pins; a normal checkout fetches as usual |

### Limits

- IPv4 only: no DHCPv6 and no router solicitation handling beyond the kernel's own (IPv6 link-local only); the IPv6-only option (RFC 8925) is never honoured (no netlink). Since P4-24, IPv6 autoconfigures ("P4-24: the rest of network_cmds and IPv6"), with RDNSS and DHCPv6 ("IPv6 DNS: RDNSS and DHCPv6").
- Interfaces are found by name (`en*`); one default route, the first lease's (dhclient-script's `is_default_interface`).
- netconfigd doesn't stop a client when its interface is removed (dhclient exits when the interface goes down) and doesn't reread its configuration (restart the job).
- No `arp`, `ndp` or `traceroute` (built since P4-24), no `nslookup`/`dig` (macOS's are BIND's, not in the release set).
- mDNSResponder's limits above; no Bonjour registration of launchd jobs' sockets.

## Checkpoint 3: the Radxa Dragon Q8B's TC956x

**Status.** Built into the SBSA kernel (patch 0037) and tested on the host against a register model; **not yet run on the board**, which wasn't available. QEMU has no TC956x model, and telling a QEMU kernel there is one would point the driver at whatever lies behind a fake BAR, so no QEMU boot covers it; the existing boots prove the rest of the path (IONetworkingFamily, the namer, netconfigd, dhclient, mDNSResponder, sshd). The exit, a DHCP lease on the TC956x and ssh from the Mac, is "On the board" below.

### The hardware

From FreeBSD's port (`../freebsd-src`, branch `radxa-dragon-q8b`; its hardware notes), which runs on this board at 2.5G with DHCP, checksum offload, TSO and interrupt moderation:

| What | On the Q8B |
|---|---|
| Where | PCIe segment 4: a TC9563 switch (UEFI powers and configures it; only its downstream port 3 is used), and behind it the TC956x endpoint 1179:0220 (subsystem 1179:0001) at bus 3, device 0, **functions 0 and 1**: one MAC each. The segment's host bridge is `_CCA` 1 (coherent) and reaches 36 address bits through the SMMUv3, assumed in bypass (`storage.md`) |
| BARs, per function | BAR0 16 KiB, the bridge configuration with the **TAMAP** (AXI-to-PCIe translation, four entries at 0x800); BAR2 512 KiB, the embedded Cortex-M3's SRAM (unused: no firmware is loaded); BAR4 2 MiB, the chip's whole SFR space |
| Chip-wide (function 0) | the clocks and resets (NCLKCTRL/NRSTCTRL 0x1004/0x1008), the TAMAP, the MSI generators' clock and reset |
| Per MAC | a Synopsys XGMAC 3.01 (SNPSVER 0x30) at 0x40000 + 0x8000·n; behind it an XPCS (a 1 KiB window at +0x3a00) and the SerDes (PMA, +0x4000); EMACnCTL (0x1070 + 4n) selects the SerDes rate (SP_SEL 4 = 2500BASE-X, 5 = SGMII 1G, 6 = 100M, 7 = 10M; it resets to an invalid 8) and says INIT_DONE; an MSI generator at 0xf000 + 0x100·n |
| PHY | a Qualcomm QCA8081 at MDIO address 0x1c (clause 22, PHY-specific status at register 0x11), its SerDes at 0x1d (clause 45, MMD 1); 10M to 2.5G. The SerDes runs 2500BASE-X at 2.5G and SGMII with in-band autonegotiation below |
| DMA | the MAC sees host address x at 0x10_0000_0000 + x (TAMAP entry 0, a 64 GiB window): every descriptor and buffer address carries that offset, and host memory must be below 36 bits |
| Interrupts | one MSI per function (32 advertised); the chip's MSI generator sends one MSI and holds off until MASK_CLR is written |
| What UEFI leaves | both MACs clocked and out of reset, the PMA and XPCS initialised at 2500BASE-X (INIT_DONE), the TAMAP programmed, a station address in the MAC (88:12:4e:00:02:01 on function 1, the address the LAN's DHCP server knows). The PHY's SerDes FIFO is released only on a port that had a link when UEFI ran |

The one hazard that shapes the code: **a block whose clock is off or whose reset is asserted doesn't answer, and on Qualcomm PCIe a failed read is an SError**. Every access follows FreeBSD's order, which ran on the board.

### Where the code is

| File | What |
|---|---|
| `kernel/neodarwin/network/nd_tc956x.h` | the hardware core, ported from FreeBSD's `if_tcx.c` and `if_tcxreg.h` (`PROVENANCE.md`): registers, chip set-up, MDIO, the PHY, the SerDes and PCS, the link state machine, init and stop, interrupts, descriptor formats and ring handling, the multicast hash. `static inline` C over five accessor macros (`ND_TC956X_READ`/`WRITE` for BAR4, `BRIDGE_WRITE` for BAR0, `DELAY`, `LOG`), as `nd_geni_uart.h` is |
| `kernel/neodarwin/network/NeoDarwinTC956x.cpp` | the IOKit driver, an `IOEthernetController`: matching, the BARs, DMA memory and mbufs, the MSI, the timers, and what IONetworkingFamily is told |
| `kernel/neodarwin/network/test/tc956x_sim_test.c` | the register model and the tests (below); `test/mutations.txt` and `test/mutate.sh` plant the bugs |
| patch 0037 | the build-list line, the search paths (network, PCI, storage's DMA policy; the kext view of 0035) and the personality, `IOPCIMatch` `0x02201179` |
| `THIRD_PARTY_NOTICES.md` | FreeBSD's BSD-2-Clause notice for the ported code |

### The driver

`NeoDarwinTC956x` matches both functions. `start` follows FreeBSD's `tcx_attach_pre` and `tcx_attach_post`:

1. The boot-args; function > 1 is refused. The function must be `dma-coherent` (below), and `dma-address-bits` is capped at 36.
2. Memory and bus mastering on; **BAR0 and BAR4 of this function** mapped, each checked to be at least as long as the highest offset the core uses (0x1000 and 0x50000).
3. **Function 0: chip set-up** (`nd_tc956x_chip_init`): TAMAP entry 0 (`SRC_LO` 0x47, `SRC_HI` 0x10, no translation; entries 1–3 cleared), then the MSI generators: clock on, then reset released. It records that the chip is set up. **Function 1 waits** for that, up to 10 s (FreeBSD's function 1 fails with ENXIO if it attaches first); after that it goes on if the MSI generators run (firmware's set-up, which FreeBSD also accepts), or gives up with `function 0 has not set up the chip`. While waiting it only reads function 0's clock and reset registers, as FreeBSD's does.
4. **The station address**, read from the MAC before anything resets it, and only if the MAC is clocked and out of reset; without one, a locally administered address (`02:4e:44:…`; the TC956x's own lives in an I2C EEPROM on `i2c12`, which NeoDarwin can't read yet).
5. **Cold init** (FreeBSD's default, `hw.tcx.cold_init=1`; `nd_tc956x_cold=0` keeps a MAC firmware left running): MAC, PMA and XPCS reset asserted; the MAC's clocks on; MAC reset released; the PHY polled over MDIO for its speed (1G if no link); EMACnCTL's speed selector set **before** the PMA's reset is released, with its reference clock configured while held (`nd_tc956x_pma_init`; INIT_DONE awaited for up to 1 s); XPCS reset released; PCS soft reset, type select, then 2500BASE-X or MAC-side SGMII with automatic speed switching (`nd_tc956x_xpcs_config`).
6. The PHY's ID (a warning if it isn't a QCA8081) and its advertisement fixed: both PAUSE bits, no half duplex (the XGMAC has none); changing it renegotiates once.
7. The rings, the MSI, the timers, the media (autoselect and 2500/1000/100/10 full duplex), a first link check, the log line, then `attachInterface`: IONetworkStack and the namer make it en*.

**Link.** A 500 ms timer (iflib's admin timer on FreeBSD) reads the PHY's status register 0x11 (`nd_tc956x_phy_poll`) and applies it (`nd_tc956x_link_apply`): on a new speed the PMA is restarted at the matching rate, and the PCS reconfigured when the link moves between 2.5G (2500BASE-X) and the rest (SGMII); the PHY's **SerDes FIFO is released on every link up and held on link down** (FreeBSD `189b99643d`: without it a port UEFI didn't bring up has link but passes nothing); the MAC's port speed and the resolved PAUSE use (802.3 Annex 28B) follow. The interface is told with `setLinkStatus`, with the medium of the speed. Choosing a medium (`ifconfig en0 media 1000baseT`) advertises that speed alone and renegotiates (`nd_tc956x_phy_set_media`).

**The datapath** (`enable` → FreeBSD's `iflib_init_locked`): `nd_tc956x_init` (DMA software reset; bus mode, MTL with a 16 KiB TX and 32 KiB RX FIFO, store and forward, flow-control thresholds; the address, the filter, RX checksum, the port speed; channel 0 with PBL 32×8, the rings' addresses through the TAMAP, the RX ring length with the 3.01a erratum's OWRQ 3, the RX watchdog), then 255 receive buffers posted and the RX tail moved, then interrupts on. `disable` runs `nd_tc956x_stop` (MSI output and channel interrupts off; the TX DMA stopped and the MTL drained before the transmitter is turned off; receive off and drained) and frees every buffer.
- **Rings**: 256 descriptors each way, physically contiguous below 36 bits, never across 4 GiB (the DMA keeps only the low 32 bits of a ring pointer). One descriptor always stays empty, so a full ring isn't mistaken for an empty one.
- **Receive**: each descriptor takes a 2 KiB mbuf cluster (RBSZ 2048); the DMA's completions are handed to the interface in one batch per interrupt and each descriptor is refilled with a fresh mbuf (or its old one, with the frame dropped, if none can be had). A frame longer than one buffer (a jumbo frame: the MAC accepts giants) spans descriptors and is dropped. A cluster above 36 bits is replaced by the slot's bounce buffer.
- **Transmit**: an `IOGatedOutputQueue`; one descriptor per segment (up to 8, from an `IOMbufNaturalMemoryCursor`), the frame length in each, FD and the checksum insertion on the first, LD on the last; the TX tail moved. A packet the DMA can't reach is copied into its descriptor's 2 KiB bounce buffer. Completions free whole packets only, from the interrupt, from `outputPacket` when the ring is short, and from the link timer.
- **DMA coherence**: the descriptors share cache lines with ones the device writes, so a clean on a non-coherent device could overwrite a completion. The driver therefore drives only a `dma-coherent` function (`DMA is not coherent; not supported`); the Q8B's host bridges are `_CCA` 1. Making it work non-coherently needs the XGMAC's descriptor skip length, which nothing has tested on this chip.

**Interrupts.** The function's MSI (IOPCIFamily lists it after INTx; one vector, as no `SUPPORT_MULTIPLE_MSI`) through the ITS, an `IOInterruptEventSource` on the work loop. FreeBSD's handler: the channel status, and if it is 0 a stray (the generator re-armed with MASK_CLR); otherwise the generator's output off, the status acknowledged, both rings serviced, then output on and MASK_CLR. A fatal bus error stops the channel; the datapath is restarted. **Moderation** is FreeBSD's: receive descriptors don't ask for an interrupt, the RX watchdog raises one 64 units (about 130 µs) after the first frame; a transmit descriptor asks for one only every 64 descriptors (the lower of 128 and a quarter ring), and the link timer reclaims what was sent without one. Without an MSI (`nd_pci_msi=0`, no ITS) or with `nd_tc956x_poll=1`, a 1 ms timer services the rings instead.

**Filters and offloads.** Promiscuous and all-multicast modes; the multicast list goes into the 64-bin hash (`~ether_crc32_be(address) >> 26`, FreeBSD `a098b845da`). **Checksum offload** (FreeBSD `fee2e88a86`) both ways: `getChecksumSupport` offers the IPv4 header and TCP/UDP over IPv4 and IPv6; on transmit `getChecksumDemand` picks CIC 3 (or 1 for the IPv4 header alone); on receive the descriptor's packet type becomes `setChecksumResult`, and a frame with the error summary set (a bad checksum among them) goes up unchecked for the stack to judge. `nd_tc956x_csum=0` turns it off.

**Counters**: the `Statistics` registry property (packets, bytes, drops, bounces, stalls, interrupts by cause, RX buffer unavailable, bus errors, link changes, the speeds).

| Boot-arg | Effect |
|---|---|
| `nd_tc956x=0` | the driver doesn't attach |
| `nd_tc956x_cold=0` | keep a MAC, SerDes and PCS firmware left running (FreeBSD's `hw.tcx.cold_init=0`) |
| `nd_tc956x_csum=0` | no checksum offload |
| `nd_tc956x_poll=1` | poll the rings every millisecond instead of taking the MSI |

The log expected for function 1 (not yet seen on the board; the LPI depends on what else has MSIs):

```
NeoDarwinTC956x: 03:00.1: TC956x 1179:0220 function 1: revision 0x01, XGMAC 0x30; MAC 88:12:4e:00:02:01 (firmware's); cold init, SerDes 2500 Mb/s; PHY 0x004dd101; link up; rx 255 x 2048 bytes, tx 256 descriptors; MSI (LPI 8193); checksum offload on; DMA coherent, 36 address bits
NeoDarwinInterfaceNamer: en1: IOEthernetInterface of NeoDarwinTC956x
NeoDarwinTC956x: 03:00.1: en1: link up at 2500 Mb/s, full duplex, flow control rx/tx
NeoDarwinTC956x: 03:00.1: rx: first completion by its interrupt (MSI, LPI 8193)
NeoDarwinTC956x: 03:00.1: tx: first completion by its interrupt (MSI, LPI 8193)
```

### Ported and deferred

| FreeBSD tcx | Here |
|---|---|
| chip set-up, MDIO, the QCA8081, PMA/XPCS per speed, the link state machine, the SerDes FIFO release, PAUSE | ported |
| cold init by default, the address read before reset | ported (`nd_tc956x_cold=0`) |
| one TX and one RX queue, store and forward, flow control, MTL FIFO sizes | ported |
| interrupt moderation (RX watchdog, TX coalescing), MSI generator handling | ported, the defaults fixed (no sysctls) |
| checksum offload | ported |
| multicast hash filter | ported |
| function 1 before function 0 | improved: waits instead of ENXIO |
| **TSO** | **deferred**: IONetworkingFamily gives a driver only the MSS (`mbuf_get_tso_requested`), so the Ethernet, IP and TCP header lengths FreeBSD's first descriptor needs would have to be parsed from the mbuf here; the Skywalk compat path would need checking too. A throughput item, not a bring-up one |
| **jumbo frames** | **deferred**: 2 KiB buffers, MTU 1500 (`getMaxPacketSize` is IOEthernetController's) |
| the MMC PAUSE watch (iflib's TX watchdog), the sysctls, resume | not needed (no TX watchdog here) or deferred |

### Safety

- **Only its own function.** The driver maps BAR0 and BAR4 of the function it matched and nothing else; every offset the core uses is a constant below the lengths it checks. It never touches the TC9563 switch, the GPIOs (the PHY's reset lines), BAR2, or another device. Function 1 writes nothing of function 0's: the model fails a test on any write to the other function's clocks, resets, MAC, MSI generator or BAR0.
- **`nd_tc956x=0`** keeps it off entirely.
- **SErrors.** FreeBSD's order exactly: no block read or written before its clock is on and its reset released (the MSI generators before chip set-up, the MAC before `mac_start`'s release, the XPCS before its own release); the PMA only written, and only while held; the address read only from a running MAC. The model raises an SError for each of these, and the mutation tests show it notices when the order is broken.
- **DMA** goes only to memory the driver allocated or mbufs it was given, below 36 bits, through the TAMAP window the driver programs.
- **What remains a risk on the board**: the switch's other downstream ports (Linux disables ports 1 and 2 because reading them SErrors; how IOPCIFamily's scan of segment 4 behaves is P1-09's business, `pci.md` "Risk"); the SMMUv3 must pass the endpoint's stream through (as for NVMe); the ITS DeviceID of a device behind a switch (`gic-its.md`).

### Tests

`//kernel/neodarwin/network:tc956x_sim_test` builds the header on the host against a model of a TC956x with its two functions. The model flags an SError on any read or write of a block whose clock is off or reset asserted, any access outside the registers the driver uses or to the other function's, a PMA read or a PMA write outside its reset, a reset released before its clock, the PMA released with an invalid speed selector or without its reference clock set, MDIO used while busy or a device addressed in the wrong clause, DMA without the TAMAP or its offset, a TX descriptor reached before the tail that the driver doesn't own, a frame without FD or whose length disagrees, the RX ring length without OWRQ 3, and the transmitter turned off before the DMA stopped and the MTL drained. Frames pass only when the PHY's link, the SerDes FIFO, the PMA rate, the PCS mode and the MAC's port speed all agree. The tests:

| Test | Checks |
|---|---|
| cold attach, function 0 | TAMAP and MSI generators; UEFI's address kept; the clock/reset/EMACCTL sequence (`clk0+msigen, rst0-msigen, rst0+mac+pma+xpcs, rst0-mac, emac0 sp5, rst0-pma, rst0-xpcs`); SGMII at 1G with no link; the QCA8081's ID; the advertisement fixed once |
| function 1 before function 0 | it doesn't attach and writes nothing; after function 0 it does, keeping UEFI's address, at 2500BASE-X; with firmware's MSI set-up alone it attaches without writing BAR0 |
| link speeds | status 0x2600/0x2500/0x2480/0x2400 for 2.5G/1G/100M/10M: SP_SEL 4–7, the PMA restarted on each change, the PCS reset only between 2500BASE-X and SGMII, the MAC's SS 2/3/4/7, the FIFO released on up and held on down, frames passing; one event per change |
| PAUSE | four partner advertisements against Annex 28B and the MAC's flow-control registers |
| datapath | every register init writes; 300 frames received and 400 sent (1 to 4 segments) over many laps, byte for byte and in order; a burst of 32 under the RX watchdog in at most 2 MSIs; a 3000-byte frame dropped with the ring intact |
| TX ring | 63 packets fill a 64-descriptor ring; packets of three descriptors reclaimed whole while the DMA stops mid-packet |
| moderation | IOC on every refill without the watchdog, none with it, every 8th with `rx_coal_frames` 8; TX IOCs every quarter ring, every 4, every packet |
| checksums | CIC 0/1/3 on the first descriptor only; packet types 1, 2, 9, 10 reported, others and errored frames not, the error counted; IPC off without offload |
| multicast | the hash bin against bitrev(~CRC-32) for six groups; joined groups, broadcast and the own address pass, others don't; all-multicast and promiscuous |
| stop, warm attach, port 0, MSI, media, PMA timeout | stop waits for the TX DMA and the MTL, then everything is off, and init works again; `cold_init` false changes no clock or reset; a port UEFI left stopped comes up from scratch (clocks before reset release) and passes traffic once the FIFO is released; a stray MSI re-arms the generator; forced and automatic media; a SerDes that never comes up is logged and retried at the next link check |

**Mutation tests** (`//kernel/neodarwin/network:tc956x_mutation_tests`, 29 targets): each builds the simulation against a copy of the header with one planted bug from `test/mutations.txt` and passes only if a check fails. The bugs: the TAMAP offset dropped; the MSI generator's or the MAC's reset released before its clock; the speed selector fixed at 1G or never written; the PMA written outside its reset or without its reference clock; the SerDes FIFO never released; the PCS type select skipped or its mode never switched; the XPCS used before its reset is released; the address read from a stopped MAC; the PHY's speed field misread; the MAC's port speed fixed; PAUSE ignored; the DMA reset not awaited; OWRQ missing; the TX tail one short; the TX ring filled completely; partial packets reclaimed; the checksum codes swapped; the RX tail at the head; frames split at each descriptor; the error summary trusted; no RX interrupt without the watchdog; the hash not complemented or not enabled; a stray MSI not re-armed; stop not waiting for the DMA. `mutate.sh` fails the build if a mutation no longer applies, so a change to the header can't silently retire one.

### On the board

What the exit needs: the Q8B with the NeoDarwin USB stick (the recipe in `arm64-sbsa-bringup.md`, "Status"), HDMI and a USB keyboard (`usb-console.md`), and an Ethernet cable from a TC956x port to the LAN switch that serves DHCP (192.168.0.0/24; the Mac is 192.168.0.18). Under FreeBSD the cable was in the port that is **function 1** (`tcx1`, Ubuntu's eth1), which linked at 2.5G and leased 192.168.0.11 for UEFI's address 88:12:4e:00:02:01; FreeBSD's notes also have port 0 cabled since 2026-09-28. Either works; note which.

1. **Boot** from the stick with `boot.cfg` as it is. On HDMI, during boot, look for (and photograph) `NeoDarwinPCIHostBridge` lines for segment 4 naming 1179:0220 at 03:00.0 and 03:00.1 with `msi 32`, the `NeoDarwinPCIMSI` lines for them, the two `NeoDarwinTC956x:` lines, `NeoDarwinInterfaceNamer: en0` and `en1`, and `link up at 2500 Mb/s` for the cabled port.
2. **At `login:`** log in as root. `ifconfig -a`: en0 and en1 with `ether` addresses (UEFI's, 88:12:4e:00:02:0x, unless the log says `random`), the cabled one `status: active` and `media: autoselect (2500baseT <full-duplex>)`, and `inet` 192.168.0.x once leased.
3. **The lease**: `cat /var/log/netconfigd.log` shows `DHCPACK from` the LAN's server and `bound to 192.168.0.x`; `netstat -rn` the default route; `cat /etc/resolv.conf` the LAN's name server; `ping -c 3` the gateway and `ping -c 3 example.com`.
4. **ssh from the Mac**: on the board, as root, `launchctl load -w /System/Library/LaunchDaemons/ssh.plist`, and put the Mac's public key in `/Users/test/.ssh/authorized_keys` (or set a password with `passwd test`). From the Mac: `ssh test@192.168.0.x uname -a` says `Darwin`; `scp` a file of a few MB back and compare sizes. That is the exit.
5. **Both ports**, if both are cabled: the other one leases too (netconfigd runs a dhclient per en* interface; only the first lease sets the default route).

If something fails, photograph the screen and note the `NeoDarwinTC956x:` lines, then try in this order (boot-args go in `\NeoDarwin\boot.cfg` on the stick):
- **Nothing from NeoDarwinTC956x**: no `1179:0220` in the host bridge's lines means segment 4 didn't enumerate (P1-09); with the device listed, the driver declined: its line says why (`not coherent`, `cannot map`, `function 0 has not set up the chip`).
- **A hang or SError while the driver starts**: boot with `nd_tc956x=0` to get the board up, and report the last lines; then try `nd_tc956x_cold=0` (no resets: UEFI's SerDes kept).
- **`PHY at 28 does not answer`** or a PHY ID other than 0x004dd101: MDIO isn't reaching the QCA8081.
- **Link never comes up**, or `SerDes did not come up`: `nd_tc956x_cold=0`; note the speed the partner offers.
- **Link up but no lease**: if no `first completion` lines appear, the MSI isn't arriving (ITS DeviceID or the SMMU): `nd_tc956x_poll=1`. If they appear but DHCP never binds, note `netstat -I en1` (its output counts are right, its input counts aren't: "Findings" above) and try `nd_tc956x_csum=0`.
- **Ping works, TCP doesn't**: `nd_tc956x_csum=0`.

### Findings

| Finding | Fix |
|---|---|
| FreeBSD's function 1 fails for good (ENXIO) if it attaches before function 0 has set the chip up | function 1 waits for function 0's driver (10 s), then accepts firmware's set-up if the MSI generators run, as FreeBSD's check does |
| tcx's descriptor rings rely on coherent DMA: 16-byte descriptors share cache lines with ones the device writes back, so cleaning a line for a non-coherent device could overwrite a completion | the driver drives only a `dma-coherent` function (the Q8B's PCIe is `_CCA` 1) |
| IONetworkingFamily's TSO hands the driver only the MSS; FreeBSD's TSO needs the header lengths in its first descriptor | TSO deferred ("Ported and deferred") |
| iflib tells tcx when a TX completion interrupt is wanted (`IPI_TX_INTR`); IONetworkingFamily doesn't | an IOC every quarter ring at most (FreeBSD's cap), completions also reclaimed in `outputPacket` and by the 500 ms link timer |
| tcx's OWN check in its RX ring walk is redundant with LD on this hardware's write-back (a descriptor the driver posted never has LD): its mutant survives every test | kept, as FreeBSD has it; not in the mutation list |

## P4-24: the rest of network_cmds and IPv6

Part of **P4-24** (its status is set in `roadmap/backlog.yaml`, not here). The rest of network_cmds-726 is built and installed with Apple's layout, and en* interfaces **autoconfigure IPv6** at boot (stateless address autoconfiguration from router advertisements), as macOS's IPConfiguration does. The three net tests check both on slirp: `arp`, `traceroute` (also run by an unprivileged user), a SLAAC address, the router in the neighbour cache, the IPv6 default route and `ping6` to the gateway. pf, `pfctl` and NTP are P4-24's other half.

### The tools

`base/network_cmds/build.sh` replays each target of `network_cmds.xcodeproj` with its Release settings, as for ifconfig, ping, netstat and route. Paths and modes are the targets' `INSTALL_PATH` and `INSTALL_MODE_FLAG`, matching what macOS 26 installs; `images/BUILD.bazel` (`_SYSTEM_MODES`) makes traceroute and traceroute6 setuid. Apple signs the networking tools with entitlements (network management, or network client and server); NeoDarwin has no sandbox or policy that reads them, so they're signed ad hoc without, as ping is.

| Program | Installed | Mode | Notes |
|---|---|---|---|
| `arp` | `/usr/sbin/arp` | 0555 | |
| `ndp` | `/usr/sbin/ndp` | 0555 | |
| `ping6` | `/sbin/ping6` | 0555 | still its own program in 726 (ping has no `-6`), as `/sbin/ping6` on macOS 26. The target links libipsec but doesn't define `IPSEC`, so nothing calls it; linked as on macOS ("libpcap and tcpdump") |
| `traceroute` | `/usr/sbin/traceroute` | 4555 | links libpcap (`/usr/lib/libpcap.A.dylib`), used only for TCP probes (`-P tcp`): the target's SYN-ACK or RST reaches no socket, so traceroute captures it on the outgoing interface ("libpcap and tcpdump") |
| `traceroute6` | `/usr/sbin/traceroute6` | 4555 | the same for TCP probes; `IPSEC` defined and libipsec linked, as Apple builds it, for an IPsec bypass policy on its sockets |
| `rtsol`, `rtsold` | — | — | **no longer built from 726** ("IPv6 DNS: RDNSS and DHCPv6"): `/sbin/rtsol` and `/usr/sbin/rtsold` are FreeBSD's newer program (`base/rtsold`), with RDNSS, DNSSL and the M/O scripts. macOS installs neither (no aggregate target includes 726's; IPConfiguration solicits routers itself). 726's patch 0002 went with it, its fix carried over (rtsold patch 0001) |
| `rarpd` | `/usr/sbin/rarpd` | 0555 | built as on macOS, `TFTP_DIR` `/tftpboot`; nothing runs it |
| `spray` | `/usr/sbin/spray` | 0555 | Sun RPC (Libinfo's) |
| `kdumpd` | `/usr/libexec/kdumpd`, `com.apple.kdumpd.plist` | 0555, 0644 | the kernel-core receiver; its job (UDP 1069, through launchproxy) is `Disabled`, as on macOS |

Not built from 726: **rtadvd**, because network_cmds-726 has no sources for it (`rtadvd.tproj` holds only `run-rtadvd`, a test script; macOS's `/usr/sbin/rtadvd` comes from elsewhere): FreeBSD's is built instead ("rtadvd", below); **dnctl**, dummynet's control, which belongs with pf; and `network_cmds_lib_test`, a test tool.

### IPv6 autoconfiguration

On macOS, IPConfiguration's automatic-v6 service starts IPv6 on an interface with private interface requests, and the kernel does the rest: it takes the prefixes, addresses and default routers from router advertisements once the interface accepts them (`IFEF_ACCEPT_RTADV`). netconfigd makes the same requests, in the same order:

| Step | macOS (IPConfiguration) | NeoDarwin (netconfigd) |
|---|---|---|
| attach IPv6 to the interface | `SIOCPROTOATTACH_IN6` | the same ioctl |
| enable it | `ND6_IFF_IFDISABLED` cleared | `ifconfig en0 inet6 -ifdisabled` |
| link-local address | `SIOCLL_START` (with CGA parameters on current macOS) | `SIOCLL_START`: EUI-64, optimistic DAD |
| accept router advertisements | `SIOCAUTOCONF_START` | the same ioctl |
| solicit a router | IPConfiguration's own solicitations, on link up | `rtsol en0`, each time the link comes up (`SIOCGIFMEDIA`); since "IPv6 DNS: RDNSS and DHCPv6", FreeBSD's `rtsold -f` for the interface, which solicits when the link comes up and reads every advertisement |
| prefixes, addresses (with RFC 4941 temporary ones), default route | the kernel (`nd6_rtr.c`) | the kernel, unchanged |
| DNS servers from RDNSS | IPConfiguration, to configd's DNS configuration | rtsold, to `resolvconf(8)` ("IPv6 DNS: RDNSS and DHCPv6") |
| DHCPv6 (the M and O flags) | IPConfiguration's DHCPv6 client | rtsold's M and O scripts start `dhcp6c` ("IPv6 DNS: RDNSS and DHCPv6") |

The three requests are `PRIVATE` in xnu's `<netinet6/in6_var.h>` (the SDK's copy leaves them out), so `netconfig_shim.h` defines their codes, with an `nd_ioctl` wrapper as launchctl's shim has. `/etc/netconfigd.conf` takes FreeBSD's `rc.conf` names: `ifconfig_<if>_ipv6` (then `ifconfig_DEFAULT_ipv6`) is `AUTOCONF` (the default; FreeBSD's `inet6 accept_rtadv` means the same), `NONE`, or a static `ifconfig` argument list such as `inet6 2001:db8::10 prefixlen 64`, with `ipv6_defaultrouter`. An interface whose IPv4 line is `NONE` gets no IPv6 unless it has its own line.

On QEMU 11.1.1's user network (libslirp 4.9.5: prefix fec0::/64, gateway fec0::2, DNS fec0::3; the router answers a solicitation from fe80::2, MAC 52:56:00:00:00:02):

```
netconfigd: en0: DHCP (dhclient -d en0, pid 12)
netconfigd: en0: IPv6 autoconfiguration (link-local address started, router advertisements accepted)
netconfigd: en0: IPv6 autoconfiguration (rtsold -f -R /sbin/resolvconf -M /usr/libexec/dhcp6c-managed -O /usr/libexec/dhcp6c-other en0, pid 14)
$ ifconfig en0 inet6
        inet6 fe80::5054:ff:fe12:3456%en0 prefixlen 64 scopeid 0x4
        inet6 fec0::5054:ff:fe12:3456 prefixlen 64 autoconf
        inet6 fec0::a02c:71bb:323b:ecd4 prefixlen 64 autoconf temporary
$ netstat -rn -f inet6
default                                 fe80::2%en0                             UGcIg                 en0
```

**DNS.** xnu doesn't read RDNSS options (on macOS IPConfiguration does, and gives the servers to configd), and rtsol-726 doesn't either. `/etc/resolv.conf` keeps the DHCP lease's servers (10.0.2.3 on slirp), so mDNSResponder asks over IPv4, AAAA queries included. On an IPv6-only network there was no unicast DNS until RDNSS and DHCPv6 were handled: "IPv6 DNS: RDNSS and DHCPv6" (rtsold, dhcp6c, resolvconf, and mDNSResponder's IPv6 servers).

### Tests

`NET_TOOLS` in `kernel/BUILD.bazel`, part of `NET_SESSION`, so `sbsa_net_test`, `sbsa_net_intx_test` and `sbsa_ref_net_test` all run it. Root waits (up to 30 s) for rtsol to finish and the IPv6 default route (since "IPv6 DNS: RDNSS and DHCPv6", for dhcp6c's reply), then checks:
- netconfigd's log lines above;
- `arp -an`: `? (10.0.2.2) at 52:55:a:0:2:2 on en0 ifscope [ethernet]` (after a ping);
- `traceroute -n -I -m 3 10.0.2.2`: hop 1 is 10.0.2.2. ICMP probes, because slirp answers an echo to the gateway whatever its hop limit, while UDP probes to 10.0.2.2 are forwarded to the host's loopback;
- the same traceroute run by `test` through `su`: `-I` needs a raw socket, so it works only setuid root (`traceroute-as-test-42`);
- `ifconfig en0 inet6`: the link-local and the EUI-64 SLAAC address (`fec0::5054:ff:fe12:3456 ... autoconf`);
- `ndp -an`: the router, `fe80::2%en0` at 52:56:0:0:0:2;
- `netstat -rn -f inet6`: the default route via `fe80::2%en0` and `fec0::/64` on en0;
- `ping6 -c 2 fec0::2`: both answered.

### Findings

| Finding | Fix |
|---|---|
| rtsol run at boot exited at once with status 0 and sent nothing: xnu gives a new link-local address optimistic DAD, rtsol's `interface_up()` reports `IFS_OPTIMISTIC`, which neither caller handles, so the interface counts as down, and the one-shot rtsol exits when every interface is down. A manual `rtsol en0` a few seconds later worked | network_cmds patch 0002: optimistic is treated as tentative (wait for DAD, then solicit; RFC 4429 asks that of a solicitation carrying a link-layer address option). macOS never runs rtsol, which is why the bug went unnoticed (FreeBSD's rtsold, which replaced it, has the same fix: rtsold patch 0001) |
| The one-shot rtsol also gives up on an interface whose link is down | netconfigd starts it when the link comes up (and again after every link-down), not when it configures the interface (since "IPv6 DNS", rtsold runs as a daemon and watches the link itself) |
| traceroute and traceroute6 link libpcap; traceroute6 and ping6 link libipsec; NeoDarwin built neither | first a failing `pcap.h` and traceroute6 without `IPSEC`; since "libpcap and tcpdump", both libraries are built and the three link them as on macOS |
| traceroute6 includes traceroute's `as.h`, which Xcode finds through its header map | `-Itraceroute.tproj` |
| slirp forwards UDP to 10.0.2.2 to the host's 127.0.0.1, so a UDP traceroute to the gateway probes the host | the tests use ICMP probes (`-I`) |
| `ld` warns that traceroute's 64 KiB `pcap_buffer` has its `__common` alignment reduced | harmless: the buffer needs no more than 16 KiB alignment |

### Deferred

- DHCPv6 (stateful addresses, or other configuration on the O flag) and RDNSS/DNSSL: done since "IPv6 DNS: RDNSS and DHCPv6".
- rtadvd and router mode (no sources in 726): done since "rtadvd", with FreeBSD's; dnctl (with pf).
- CGA or stable-privacy link-local addresses as current macOS uses; NeoDarwin's are EUI-64.
- dhclient-script skipped its ARP flush (dhclient patch 0002) while there was no `arp`; with `arp` built the flush runs (the guard stays, for a system without it).

## libpcap and tcpdump

Part of **P4-24**. Apple's packet-capture library and `tcpdump`, from the macOS 26.0 release set, and Apple's libipsec, which ping6 and traceroute6 link. traceroute's TCP probes (`-P tcp`) now work, and root can capture on en0, as on macOS and FreeBSD (both ship `tcpdump` in the base).

| Image | Installed | Source | Notes |
|---|---|---|---|
| `libpcap.A.dylib` (+ `libpcap.dylib` link) | `/usr/lib`, install name `/usr/lib/libpcap.A.dylib`, versions 1/1 | libpcap-144 (libpcap 1.10.1 with Apple's pktap, pcapng writer and process/interface tables) | `base/libpcap/build.sh` replays the Xcode target: the drop's `config.h`, `grammar.y`/`scanner.l` through the toolchain's bison 2.3 and flex 2.6.4, `PRIVATE` and `HAVE_PKTAP_API`, hidden by default. Exports: all **200** of the SDK's `libpcap.A.tbd` (`//base:libpcap_exports_test`). Headers (private variants) build-only in `usr/local/include` |
| `libipsec.A.dylib` (+ `libipsec.dylib`) | `/usr/lib`, install name `/usr/lib/libipsec.A.dylib`, current 300, compatibility 1 | ipsec-1125 (KAME's libipsec as ipsec-tools 0.7 ships it); only the library | five sources and a yacc/lex parser; exports all **45** of the SDK's `libipsec.A.tbd` (`//base:libipsec_exports_test`) |
| `tcpdump` | `/usr/sbin/tcpdump`, 0755 root:wheel as on macOS | tcpdump-153 (tcpdump 4.99.1 with Apple's pktap, pcapng and metadata-filter printing) | links `libpcap.A.dylib` and OpenSSL 3.5's `libcrypto.3.dylib` (ESP decryption, `-E`; macOS links LibreSSL's `libcrypto.46` and `libssl.48`); signed ad hoc without Apple's `com.apple.private.skywalk.observe-all`. Patch 0001 |

traceroute (`-lpcap`), traceroute6 (`-lpcap -lipsec`, `IPSEC` defined) and ping6 (`-lipsec`) now link what their targets link, so their load commands match macOS 26's; network_cmds' patch series is unchanged (no new patch was needed), and its failing `compat/pcap/pcap.h` is gone.

**BPF devices.** xnu's `bpf_init()` makes `/dev/bpf0`… at boot and one more each time the last is opened, `root:wheel` mode **0600** (`bpf_make_dev_t`), as on macOS: no kernel change. So `tcpdump` is root's. traceroute is setuid root but drops privileges at start-up, before it opens its capture, so `-P tcp` works for root only, as on macOS.

**Closed pieces.** None needed: libpcap's `pcap-darwin.c` uses xnu's private `net/pktap.h` and `net/iptap.h` and `libproc`, all in the sysroot. Two differences from Apple's build environment:

- Apple's internal SDK defines `SPI_AVAILABLE` (and `SPI_DEPRECATED_WITH_REPLACEMENT`) as availability attributes, and clang gives a declaration with one default visibility; the public SDK's are empty. Under `GCC_SYMBOLS_PRIVATE_EXTERN` that hid 95 of the 200 exports (the pcapng writer, pktap, the process and interface tables). `base/libpcap/compat/nd_spi_available.h`, force-included into libpcap and tcpdump, defines them as default visibility.
- tcpdump copies its command line, everything it writes to standard error and its `-v` statistics to the unified log (`os_log`). NeoDarwin's libsystem_trace stand-in writes `os_log` to standard error, so each line would appear twice, and it has no `_os_log_impl`. tcpdump patch 0001 leaves the copies out under `NEODARWIN_NO_LOG_STORE`.

### Tests

`//kernel:sbsa_net_capture_test` (`NET_CAPTURE` in `kernel/BUILD.bazel`): its own boot on virt with virtio-net on slirp, so the three net tests keep their time budgets; root waits for en0's DHCP address, then (about 16 s in all):

```
# tcpdump -n -c 2 -i en0 icmp > /tmp/td.txt 2> /tmp/td.err & sleep 3; ping -c 2 10.0.2.2 > /dev/null; ...
tcpdump: verbose output suppressed, use -v[v]... for full protocol decode
listening on en0, link-type EN10MB (Ethernet), snapshot length 524288 bytes
2 packets captured
4 packets received by filter
0 packets dropped by kernel
02:11:35.112771 IP 10.0.2.15 > 10.0.2.2: ICMP echo request, id 50432, seq 0, length 64
02:11:35.113036 IP 10.0.2.2 > 10.0.2.15: ICMP echo reply, id 50432, seq 0, length 64
# tcpdump -D
1.en0 [Up, Running, Connected]
2.lo0 [Up, Running, Loopback]
3.gif0 [none]
4.stf0 [none]
# ls -l /dev/bpf0
crw-------  1 root  wheel  0x17000000 Oct  2 02:10 /dev/bpf0
# traceroute -n -e -P tcp -p $ND_HOSTFWD_PORT -m 3 -q 1 10.0.2.2
traceroute to 10.0.2.2 (10.0.2.2), 3 hops max, 40 byte packets
 1  10.0.2.2  3.164 ms
```

- `test`'s `tcpdump -i en0` fails with `/dev/bpf0: Permission denied` (`bpf-denied-42`).
- The TCP traceroute goes to the gateway at the run's `--hostfwd` port (a `--host-setup` writes it for `{hostfile:hostfwd_port}`), with a fixed port (`-e`). slirp hands a TCP connection to 10.0.2.2 to the host's loopback and answers the SYN once that connects: here QEMU's own forward listener, a port no other program holds, so the check is hermetic and never touches a host service. The SYN-ACK reaches no socket of traceroute's: only the capture sees it, so hop 1 proves libpcap works. (A closed port would get slirp's RST, also a TCP answer, but would probe whatever the host runs there.)
- On the host, the built tcpdump reads tcpdump's own pcap and pcapng test captures and decrypts its ESP sample (`-E`, 3DES) through OpenSSL 3.5.

### Findings

| Finding | Fix |
|---|---|
| libpcap built with the drop's settings exported 105 of 200 symbols | `nd_spi_available.h` (above) |
| tcpdump's unified-log copies would double its standard error with the trace stand-in, which also lacks `_os_log_impl` | tcpdump patch 0001 |
| tcpdump-153's target compiles every `print-*.c` but tcpdump.org's `print-pktap.c` (Apple's `print_pktap.c` replaces it) | the build leaves it out |
| libipsec's `policy_token.l` includes the parser's header as `y.tab.h`, and `policy_parse.y` includes racoon's `var.h`, both through Xcode's header map | the build copies the header to `y.tab.h` and searches `racoon/` |
| traceroute's `-P tcp` reports a failed `pcap_activate()` with its own (empty) error buffer, not `pcap_geterr()` (from the source) | upstream behaviour, unchanged; it shows only when the capture can't be opened, e.g. for a non-root user |

### Deferred

- Capture on pktap (macOS's default when no `-i` is given, and `-i pktap,…`), process metadata (`-k`) and pcapng output (`-P`) are built but untested: they need xnu's pktap interface, which nothing here exercises.
- tcpdump's `-E` uses OpenSSL 3.5's default provider; ciphers moved to the legacy provider (e.g. Blowfish, CAST) need it loaded.
- Man pages (`tcpdump.1`, `pcap*.3`) wait for the man-page work, as for the other programs.

## rtadvd

Part of **P4-24** (its status is set in `roadmap/backlog.yaml`, not here). `/usr/sbin/rtadvd`, the IPv6 router advertisement daemon, and `/etc/rtadvd.conf` (FreeBSD's, every entry commented out: with no entry rtadvd advertises the prefixes of the interfaces it is given). It is router-side: nothing runs it at boot, and a NeoDarwin machine is a host unless root makes it a router.

**Source.** network_cmds-726 has no rtadvd sources: `rtadvd.tproj` holds only `run-rtadvd`, a test script for a newer Apple rtadvd (its configuration uses keywords FreeBSD's lacks, `rdnssaddrs#`, `pvd`), and macOS's `/usr/sbin/rtadvd` is built from sources Apple doesn't publish. By the reuse order (`docs/repository.md` §3.1) FreeBSD's `usr.sbin/rtadvd` (KAME's, rewritten by Hiroki Sato in 2011) comes next, pinned per file at freebsd-src 050683bb8e13 by `base/rtadvd/freebsd.lock` (`@freebsd_rtadvd`, as `base/dhclient`). `base/rtadvd/build.sh` builds its Makefile's sources with `LIBADD util` (Apple's libutil has `pidfile(3)`), `__APPLE_USE_RFC_3542` (RFC 3542's `IPV6_RECVPKTINFO` and `IPV6_RECVHOPLIMIT`) and System.framework's PrivateHeaders (xnu's private `nd6.h`, `in6_var.h`, `if_private.h`, `sockio_private.h`), as network_cmds' rtsol and ndp are built. RDNSS, DNSSL, route information and PREF64 options are all kept. `run-rtadvd` isn't installed (its configuration is for the other rtadvd), but its steps are the ones to follow, and the test's (on its one interface, en0):

```
sysctl -w net.inet6.ip6.forwarding=1          # rtadvd sends no RA with a router lifetime without it
ifconfig en0 inet6 routermode enabled         # IPV6_ROUTER_MODE_EXCLUSIVE: other routers' RAs ignored
ifconfig en0 inet6 2001:db8:1::1 prefixlen 64
rtadvd -c /etc/rtadvd.conf en0                # e.g. en0:rdnss="2001:db8:1::1":dnssl="example.test":
```

**What differs on xnu** (`compat/nd_rtadvd_compat.h`: `nitems`, `CLOCK_MONOTONIC_FAST` as `CLOCK_MONOTONIC`, `INFTIM`; patch 0001 for the rest, each change under `__APPLE__`):

| FreeBSD | xnu | Patch 0001 |
|---|---|---|
| the routing socket pads addresses to a `long` | to 32 bits (`rtsock.c`, `ROUNDUP32`) | `NEXT_SA` pads to `uint32_t`, as rtsol's `if.c` does |
| `RTM_IFANNOUNCE` for an interface's arrival and departure | none | an `RTM_IFINFO` from an unknown interface is its arrival; a departing one goes down first |
| `SIOCGIFINFO_IN6` takes `struct in6_ndireq` | encoded with the shorter `struct in6_ondireq` | `in6_ondireq` (the fields read are at the same offsets) |
| `ND6_IFF_ACCEPT_RTADV` says the interface takes RAs | never set ("APPLE: not used"); the flag is `IFEF_ACCEPT_RTADV` (`SIOCGIFEFLAGS`) | `IFEF_ACCEPT_RTADV` is reported as the ND flag, so rtadvd still refuses a non-zero router lifetime on an interface that accepts RAs |
| `struct nd_opt_rdnss`, `nd_opt_dnssl`: the 8-octet fixed part | also hold the first address and 8 octets of domains (24 and 16 bytes) | the fixed part by `offsetof`; with `sizeof` each option would carry 16 stray zero octets (an extra `::` server, a mangled domain list) |
| `nd_opt_pref64_sl_plc`, `nd_opt_prefix` | `nd_opt_pref64_scaled_lifetime_plc`, `nd_opt_pref64_prefix` | the names |
| `SIOCSIFINFO_IN6` (rtadvd sets the kernel's hop limit and timers) | none | none: upstream already compiles it only where it exists |

Router renumbering (`rrenum.c`) uses the prefix ioctls xnu has withdrawn (`EOPNOTSUPP`, "prefix ioctls are now invalidated"), as FreeBSD has. `rtadvctl`, the control client, isn't built (the control socket, `/var/run/rtadvd.sock`, is).

### Two guests

The harness boots one QEMU, and slirp's router can't be the device under test. `qemu_efi_test.sh` now has an opt-in second guest (`arm64-sbsa-bringup.md` §2.1.9, "Two guests"): `--peer-net DEV` starts it with the same kernel and image, `--link-net DEV` puts this guest on the same segment, a QEMU `dgram` netdev over two Unix datagram sockets in the run's own directory (no host port, no multicast). Two guests rather than one with two NICs: one machine can't be both, since `ip6.forwarding` is global and xnu accepts a locally generated RA on the advertising interface (`nd6_ra_input`, "for convenience"), and a ping between two of its own addresses never leaves it.

`//kernel:sbsa_rtadvd_test` (virt, `neoverse-n2`, two guests of 2 GiB, `pam_session_root`):

| Router (the peer) | Host (this guest) |
|---|---|
| one NIC, en0 (52:54:00:00:01:01), on the segment | one NIC, en0 (52:54:00:00:01:02), on the segment |
| waits for en0's link-local address; `ip6.forwarding` on, en0 in router mode, 2001:db8:1::1/64 on it | netconfigd at boot: link-local address, `SIOCAUTOCONF_START`, the solicitor (no answer yet); dhclient finds no server |
| `rtadvd -f -D` on en0 with `rdnss="2001:db8:1::1":dnssl="example.test":rltime#1800:maxinterval#4:mininterval#3:` (the prefix learned from en0) | the kernel takes the unsolicited RAs: `2001:db8:1:0:5054:ff:fe00:102 autoconf` (and a temporary address), the router in `ndp -rn` (`flags=IT, pref=medium`), the default route `UGcIg` via `fe80::5054:ff:fe00:101%en0`, `2001:db8:1::/64` on en0 |
| pings the host's SLAAC address: answered (`hlim=64`) | pings 2001:db8:1::1: both answered |
| waits for the host's solicitation in rtadvd's log (`<rs_input> RS received from fe80::5054:ff:fe00:102 on en0`); prints the RDNSS and DNSSL lengths and `ndp -an` | `rtsol en0` (exit 0; a solicited RA); prints `/var/run/resolv.conf` |

Neither guest has a user-mode NIC: with two NICs the en* names follow the order in which the driver instances attach, not the PCI slots (a run gave the segment's NIC en0 and slirp's en1), so the router's interface wouldn't be known.


xnu only checks the RDNSS and DNSSL options' lengths; the router's log checks they are encoded as RFC 8106 has them: `nd_opt_dnss_len = 3` (8 octets and one address, in units of 8) and `nd_opt_dnssl_len = 3` (8 octets and `example.test`, 14 octets, padded to 16); with `sizeof` they would be 5 and 4. The host's resolv.conf (rtsold and resolvconf, "IPv6 DNS: RDNSS and DHCPv6") is printed but not required: at the time of writing it read `search test` and no nameserver, which is what a reader that skips xnu's 24- and 16-byte structures instead of 8 octets makes of these options (the first address, and `\x07example`, skipped): the same structure difference on the receiving side.

### Findings

| Finding | Fix |
|---|---|
| xnu's routing socket pads addresses to 32 bits; FreeBSD's rtadvd steps by a `long` | patch 0001 (`NEXT_SA`) |
| xnu's RDNSS and DNSSL structures are 16 bytes longer than the options' fixed part | patch 0001 (`offsetof`); the same applies to any FreeBSD RA reader built on xnu (rtsold) |
| rtadvd's "accepts RAs" check reads `ND6_IFF_ACCEPT_RTADV`, which xnu never sets | patch 0001 reads `IFEF_ACCEPT_RTADV` |
| No `RTM_IFANNOUNCE` on xnu | patch 0001: an unknown interface's `RTM_IFINFO` is its arrival |
| The router autoconfigures from its own RAs: en0 got `2001:db8:1:0:5054:ff:fe00:101` and a temporary address beside the static one, though router mode (`EXCLUSIVE`) clears `IFEF_ACCEPT_RTADV` | none: xnu accepts a locally generated RA on the advertising interface by design (`nd6_ra_input`; `in6_autoconf`'s comment, "autoconfigured via a locally-generated RA"), as macOS does |
| `ifconfig en0 inet6 routermode enabled` logs `en0: enable allmulti failed (102)`: NeoDarwinVirtioNet doesn't implement all-multicast | none needed here (rtadvd joins ff02::2 and receives solicitations); a router with multicast routing would need it |
| ndp, ifconfig and ping6 print `2001:db8:1:0:5054:…` (KAME's form, no `::` for a single zero group) | the test matches that form |
| The harness ran one guest | `--link-net`, `--peer-net` (above) |

## IPv6 DNS: RDNSS and DHCPv6

Part of **P4-24** (its status is set in `roadmap/backlog.yaml`, not here). IPv6 name servers now reach the resolver: from router advertisements (RFC 8106 RDNSS and DNSSL) and from DHCPv6, stateless (Information-Request, when an advertisement's O flag is set) or stateful (an address too, when M is set). Every source's servers merge in `/var/run/resolv.conf` with the DHCPv4 lease's, and mDNSResponder asks IPv6 servers too. The kernel still does the address autoconfiguration (SLAAC); nothing else touches addresses or routes from advertisements.

### Decisions

| Question | Decision | Why |
|---|---|---|
| Apple's client | **not usable**: IPConfiguration (bootp-534) has an RDNSS reader (`RTADVSocket.c`) and a DHCPv6 client (`DHCPv6Client.c`), but they are parts of its service state machine (`ipconfigd_threads.h`, `interfaces.h`), on CoreFoundation and SystemConfiguration's private SPI (`SCPrivate.h`), and give their results to configd | the reuse order (`docs/repository.md` §3.1): Apple first where it can do the job standalone; it can't |
| Router advertisements | **FreeBSD's `rtsold`** (`usr.sbin/rtsold`, pinned per file, `base/rtsold/freebsd.lock`), replacing network_cmds-726's `rtsol`. Installed as FreeBSD installs it: `/usr/sbin/rtsold`, and `/sbin/rtsol` (the same program; the one-shot name) | 726's rtsol is KAME's program from before RDNSS: no RDNSS or DNSSL, no `-M`/`-O` scripts. FreeBSD's is the same program fifteen years on: RDNSS and DNSSL to `resolvconf -a`, with their lifetimes, and scripts on the M and O flags. macOS installs neither rtsol, so nothing of macOS's layout changes. It only reads advertisements and solicits; the kernel goes on doing SLAAC |
| DHCPv6 client | **WIDE-DHCPv6's `dhcp6c`**, as FreeBSD's ports build it (`net/dhcp6`: hrs@'s fork, tag v20080615.2; `base/dhcp6c/wide.lock`), the client only | FreeBSD's base has no DHCPv6 client. Of the ports, `dhcp6c` is KAME's (rtsold's lineage) and speaks only DHCPv6; `dhcpcd` (BSD-2, `docs/architecture/freebsd-parity.md`'s fallback for DHCP) would also take router advertisements and SLAAC from the kernel (two daemons on the same job) and has no Darwin port. Darwin is a KAME stack (`__KAME__`), and `dhcp6c` built with its configure's answers and two patches |
| Merging the servers | **FreeBSD's `resolvconf(8)`**, openresolv 3.17.4 (`contrib/openresolv`, `base/resolvconf`), fed by dhclient-script (key `en0`), rtsold (`en0:slaac`) and dhcp6c's script (`en0:dhcp6`) | the way FreeBSD does it, and the three feeders already speak it (dhclient-script and rtsold unchanged). `/etc/resolvconf.conf` is NeoDarwin's: the merged file is `/var/run/resolv.conf` (`/etc/resolv.conf` links to it, as on macOS), written atomically, and `libc_restart` sends `SIGHUP` to mDNSResponder. openresolv is plain `sh` with `cat`, `mkdir`, `rm`, `mv` and `sed`, all in the base |
| What starts them | **netconfigd**: `rtsold -f` per autoconfiguring interface (restarted 10 s after it exits, like dhclient), with the M/O scripts by `ifconfig_<if>_dhcp6` | one supervisor for every interface's clients, as before; the scripts start `dhcp6c` when the flags ask |

### Pieces

| Where | What |
|---|---|
| `//base:router_solicitation` (`base/rtsold`) | `/usr/sbin/rtsold`, `/sbin/rtsol`. `compat/`: Capsicum and Casper as no-ops (built without `WITH_CASPER`, the services' functions are called directly), `nd_rtsold_compat.h` (`nitems`, `__DECONST`, `CLOCK_MONOTONIC_FAST`, `closefrom`, xnu's extended interface flags). Patches 0001 and 0002 (resolvconf runs only when an advertisement changes the name servers or search domains, not on every one) |
| `//base:resolver_config` (`base/resolvconf`) | `/sbin/resolvconf`, `/usr/libexec/resolvconf/libc` (the only subscriber: `resolv.conf`), `/etc/resolvconf.conf` |
| `//base:dhcp6_client` (`base/dhcp6c`) | `/usr/sbin/dhcp6c` (parser and scanner through the toolchain's yacc and lex); `/usr/libexec/dhcp6c-managed` and `dhcp6c-other` (one script, `dhcp6c-start`), rtsold's M and O scripts; `/usr/libexec/dhcp6c-script`, dhcp6c's. Patches 0001 and 0002 |
| `base/dhclient` patch 0002 | the script hands its servers to `resolvconf` as FreeBSD's does (its own `/var/run/resolv.conf` writer is gone) |
| `base/mdnsresponder` `daemon-patches/0002` | IPv6 name servers; servers gone from `resolv.conf` dropped |
| `base/netconfigd` | rtsold instead of the one-shot rtsol; `ifconfig_<if>_dhcp6`, `rtsold_flags` |
| `tests/qemu/net` | the net tests' `/etc/netconfigd.conf`: the system's, with `ifconfig_DEFAULT_dhcp6="INFORM"` |

### Configuration

`/etc/netconfigd.conf`, with `rc.conf`'s names where FreeBSD has one:

| Name | Values | Default |
|---|---|---|
| `ifconfig_<if>_ipv6`, `ifconfig_DEFAULT_ipv6` | `AUTOCONF` (or FreeBSD's `inet6 accept_rtadv`): SLAAC and rtsold; `NONE`; a static `ifconfig` argument list | `AUTOCONF` (`NONE` for an interface whose IPv4 line is `NONE`) |
| `ifconfig_<if>_dhcp6`, `ifconfig_DEFAULT_dhcp6` (NeoDarwin's; FreeBSD's base has no DHCPv6) | `AUTO`: as advertisements say (rtsold `-M dhcp6c-managed -O dhcp6c-other`); `INFORM`: stateless whatever the flags (`-A dhcp6c-other`, after the first advertisement); `ADDRESS`: stateful whatever the flags (`-A dhcp6c-managed`); `NONE` | `AUTO` |
| `rtsold_flags` (FreeBSD's) | more `rtsold` options, e.g. `-d` (informational messages), `-u` (one resolvconf key per router) | none |

A static interface adds no servers: `resolvconf -a <name> < file`, or replace the `/etc/resolv.conf` link with a file. `resolvconf -l` lists each source's lines.

### How it runs

```
netconfigd
  en0: SIOCPROTOATTACH_IN6, -ifdisabled, SIOCLL_START, SIOCAUTOCONF_START
  rtsold -f -R /sbin/resolvconf -M /usr/libexec/dhcp6c-managed -O /usr/libexec/dhcp6c-other en0
    solicits; each advertisement:      the kernel: prefix, addresses, default route
      RDNSS, DNSSL                     resolvconf -a en0:slaac       (removed when their lifetime ends)
      M on                             dhcp6c-managed en0 <router>   dhcp6c: Solicit/Request (IA_NA)
      O on (M off)                     dhcp6c-other en0 <router>     dhcp6c: Information-Request
        dhcp6c-script (REASON, interface, new_domain_name_servers, new_domain_name)
                                       resolvconf -a en0:dhcp6       (-d on RELEASE, EXIT)
  dhclient-script BOUND                resolvconf -a en0             (-d on EXPIRE)
resolvconf: /var/run/resolv.conf (en0, then en0:dhcp6, en0:slaac; duplicates once), kill -HUP mDNSResponder
```

`dhcp6c` runs in the foreground, one per interface (`/var/run/dhcp6c.<if>.{conf,pid,mode}`), its messages in netconfigd's log; a stateless client is replaced by a stateful one when M turns on. Its DUID is `/var/db/dhcp6c_duid` (DUID-LLT from the first interface's MAC). There is no control key (`/etc/dhcp6cctlkey`), so no control port.

On QEMU's user network (libslirp 4.9.5): its advertisements set neither M nor O, and carry RDNSS (fec0::3) **only when the host itself has an IPv6 name server** (`get_dns6_addr`); its DHCPv6 server answers Information-Request with fec0::3 always; fec0::3 forwards only to the host's IPv6 name server. The net tests' image therefore asks for `INFORM`. A run's log (`/var/log/netconfigd.log`; this host has IPv4 name servers only, so no RDNSS):

```
netconfigd: en0: IPv6 autoconfiguration (rtsold -f -R /sbin/resolvconf -A /usr/libexec/dhcp6c-other en0, pid 15)
dhcp6c-other: en0: DHCPv6 (other) for router fe80::2: dhcp6c -f -d -c /var/run/dhcp6c.en0.conf en0
client6_send: Sending Information Request
client6_recvreply: dhcp6c Received INFOREQ
client6_recvreply: no server ID option in a reply to Information-Request; taken
dhcp6c-script: en0: INFOREQ: name servers fec0::3
$ resolvconf -l
# resolv.conf from en0
nameserver 10.0.2.3
# resolv.conf from en0:dhcp6
nameserver fec0::3
$ cat /etc/resolv.conf
# Generated by resolvconf
nameserver 10.0.2.3
nameserver fec0::3
```

### Tests

- The three net tests (`NET_TOOLS`, `NET_DNS6`): the log lines above, `resolvconf -l` and the merged `resolv.conf` (both servers). Then, if an RDNSS record came (`en0:slaac`), the DHCP lease's servers are withdrawn (`resolvconf -d en0`), `example.com` is looked up through fec0::3 alone, and the lease's are put back; the host checks the answer (`host: dns6: ...`) only when it has an IPv6 name server of its own and resolves the name, as the IPv4 check does, and otherwise says why it skipped (`host: dns6: skipped: the host has no IPv6 name server, ...` on this machine).
- RDNSS and DNSSL hermetically: `//kernel:sbsa_rtadvd_test` ("rtadvd"), whose router advertises RDNSS 2001:db8:1::1 and DNSSL `example.test`: the host's `resolv.conf` has `nameserver 2001:db8:1::1` and `search example.test`.

### Findings

| Finding | Fix |
|---|---|
| Apple's rtsol-726 has no RDNSS, DNSSL or M/O scripts | FreeBSD's rtsold replaces it (`base/rtsold`); network_cmds no longer builds 726's, and its patch 0002 (optimistic DAD) moved to rtsold patch 0001 |
| FreeBSD's rtsold reads "IPv6 disabled" and "accepts RAs" as ND flags (`SIOCGIFINFO_IN6`, `ND6_IFF_*`), which xnu keeps as extended interface flags (`IFEF_IPV6_DISABLED`, `IFEF_ACCEPT_RTADV`); xnu has no `RTM_IFANNOUNCE`; the SDK no `<netinet6/ip6_var.h>` | rtsold patch 0001: `SIOCGIFEFLAGS` (`nd_ifeflags`); `-F` only warns (netconfigd's `SIOCAUTOCONF_START` sets the flag) |
| xnu's `struct nd_opt_rdnss` and `nd_opt_dnssl` include the first address and 8 octets of domains (24 and 16 bytes; FreeBSD's are the 8-octet header), so rtsold took the addresses at `sizeof()` and skipped the first: an RA with one RDNSS address gave a `search` line and no server (seen in `sbsa_rtadvd_test`, as rtadvd's agent found on the sending side) | rtsold patch 0001: `offsetof()` of the address and domain fields, 8 on both |
| rtsold ran `resolvconf -a <if>:slaac` on every router advertisement, with the same lines: about six processes (sh, mkdir, two subshells, two rm) per advertisement in each guest. With `sbsa_rtadvd_test`'s 4-second advertisements and, until kernel patch 0045, two kernel console lines per exec (`shared_region: ... check_np ... vm_shared_region_start_address() returned 0x1`, no shared cache), the traces split the test's expected lines under host load and it timed out | rtsold patch 0002: resolvconf runs when the text it would get changes (per key; a failure, a delete or a restart forgets it); kernel patch 0045 traces the empty shared region at INFO |
| libslirp answers Information-Request without a Server Identifier, which RFC 8415 has a client discard: dhcp6c logged "no server ID option" and retried forever | dhcp6c patch 0002: a reply to Information-Request is taken without one, with a notice; every other reply still needs it |
| dhcp6c runs its script with `REASON` and the options, not the interface, which a `resolvconf` key needs | dhcp6c patch 0001: `interface=<name>`, as dhclient-script has |
| mDNSResponder's POSIX daemon read only IPv4 name servers (`inet_aton`, a 16-byte buffer): IPv6 servers were ignored | `daemon-patches/0002`: IPv6 addresses too (not link-local: the platform's unicast sends set no zone) |
| The daemon added servers on every `SIGHUP` and never removed one (a withdrawn lease's server stayed); and at start, `uDNS_SetupDNSConfig()` deleted the servers `mDNSPlatformInit()` had just read, so a daemon started with `resolv.conf` in place had none until a `SIGHUP` | `daemon-patches/0002`: servers are read in `mDNSPlatformSetDNSConfig()`, as configd's on macOS, so the core keeps those listed and drops the rest; `Reconfigure()` calls `uDNS_SetupDNSConfig()` |
| libslirp's advertisements carry RDNSS and its fec0::3 proxy forwards only when the host has an IPv6 name server (this one has none) | the guest's IPv6-only lookup and the host's check are conditional (above); the hermetic RDNSS check is the two-guest rtadvd test |
| dhcp6c built with `-ll`'s `yywrap()` | `lex --noyywrap`, which is what `-ll` gives |

### Limits

- One `dhcp6c` per interface, each bound to port 546 with `SO_REUSEPORT`: with DHCPv6 on two interfaces at once a reply may reach the other interface's client, which drops it (wrong transaction) and retries.
- Link-local IPv6 name servers (`fe80::1%en0`) are kept in `resolv.conf` but skipped by mDNSResponder.
- mDNSPosix keeps one socket per query, of the family of the first server it asked; a query that fails over between an IPv4 and an IPv6 server can't send on it.
- No DHCPv6 prefix delegation (IA_PD) or `rtsold` on a router; no RFC 8925 IPv6-only preference; Information-Request's refresh time is dhcp6c's (a day unless the server says).
- A stateful address from `dhcp6c` (IA_NA) is added with `SIOCAIFADDR_IN6`, /128, as on FreeBSD; no QEMU test has a stateful server.

## What P4-24 still needs

**P4-24** (the networking userland) builds on this. Done: the rest of network_cmds and SLAAC ("P4-24: the rest of network_cmds and IPv6"); libpcap, libipsec and tcpdump ("libpcap and tcpdump"); pf with OpenBSD 4.3's `pfctl`, macOS's `/etc/pf.conf` and `com.apple.pfctl`, and an NTP client, Apple's `sntp` under `com.neodarwin.sntp` (`docs/base/pf-ntp.md`); IPv6 DNS, RDNSS and DHCPv6, merged with DHCP's by `resolvconf(8)` ("IPv6 DNS: RDNSS and DHCPv6"); a router's side, FreeBSD's `rtadvd` ("rtadvd"). Still to do: a persistent interface namer (with P3-08's `netd`); and whether NeoDarwin keeps FreeBSD's dhclient or moves to bootp's IPConfiguration once configd is built. Checkpoint 3 needed nothing more from the userland: netconfigd starts dhclient on whatever en* interface the TC956x driver attaches.
