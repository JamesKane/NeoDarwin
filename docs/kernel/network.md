<!-- SPDX-License-Identifier: BSD-2-Clause -->
# Bring-up Ethernet: IONetworkingFamily and virtio-net (P1-19)

**P1-19, checkpoint 1.** The SBSA kernel has an Ethernet stack: Apple's open **IONetworkingFamily**, the family every macOS Ethernet driver publishes through, with a NeoDarwin **virtio-net** driver under it. Its interface attaches to the BSD stack as **en0**, root configures it by hand (`ifconfig en0 inet 10.0.2.15/24 up`, `route add default 10.0.2.2`), and on QEMU's user-mode network the gateway answers `ping`, and **the host logs in over ssh** with a key and copies a file back, on `virt` (MSI-X and INTx) and on `sbsa-ref`.

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

Static, by hand, for now. QEMU's user-mode network (slirp) is 10.0.2.0/24: the guest is 10.0.2.15, the gateway (and the host, as seen from the guest) 10.0.2.2, the DNS proxy 10.0.2.3.

```
ifconfig en0 inet 10.0.2.15/24 up
route -n add default 10.0.2.2
```

`ifconfig en0 up` enables the interface (`IONetworkController::enable`), which starts the output queue and reports the link (`en0: link up`). Nothing configures en0 at boot yet: launchctl's bootstrap configures only lo0 (`docs/base/session.md`, "Loopback and sshd"). DHCP is checkpoint 2.

## Tests

| Target | Machine | Asserts |
|---|---|---|
| `//kernel:sbsa_net_test` | `virt`, `virtio-net-pci,disable-legacy=on` (1af4:1041) on the user network, MSI-X | the driver's line (MAC 52:54:00:12:34:56, three MSI-X vectors), en0 named, `ifconfig en0` (ether, inet 10.0.2.15/24, `status: active`), the default route, `ping -c 2 10.0.2.2` (2 of 2), the first receive and transmit completions on vectors 1 and 2; **ssh from the host**: `uname -a` says `Darwin`, a 1.5 MB file copies back whole; `netstat -I en0`; sshd logs the publickey login from 10.0.2.2 |
| `//kernel:sbsa_net_intx_test` | `virt`, `nd_pci_msi=0`, transitional `virtio-net-pci` (1af4:1000) | the same on INTx |
| `//kernel:sbsa_ref_net_test` | `sbsa-ref` (TF-A, SbsaQemu, four CPUs), `virtio-net-pci,disable-legacy=on` on its PCIe root bus | the same, with MSI-X through the ITS via the SMMUv3 in bypass |

**ssh from the host.** The harness runs the host's own `ssh`, and keeps the run sealed:

- `--host-setup` makes an ed25519 key with `ssh-keygen` in a directory of the run's own (also `HOME` for host commands), deleted with the run.
- In the guest, root runs `launchctl load -w /System/Library/LaunchDaemons/ssh.plist` (Remote Login, socket-activated) and writes the public key to `/Users/test/.ssh/authorized_keys`: the harness types it, from `{hostfile:id_ed25519.pub}`.
- `--user-net ...` with `--hostfwd 22` adds `-netdev user,hostfwd=tcp:127.0.0.1:PORT-:22`. PORT is a free port the host's kernel picks for this run; the forward listens on the loopback only and closes with QEMU. No other host port is opened.
- `--host-cmd-after 'key-42' 'ssh ... uname -a'` runs `ssh -F /dev/null -i id_ed25519 -o IdentitiesOnly=yes -o IdentityAgent=none -o BatchMode=yes -o StrictHostKeyChecking=no -o UserKnownHostsFile=/dev/null -p $ND_HOSTFWD_PORT test@127.0.0.1`: no user configuration, agent or known hosts, never a prompt. The first connection waits while `sshd-keygen-wrapper` makes the guest's host keys.
- Its output joins the log as `host: ...` lines (`host: Darwin localhost 25.0.0 ...`, `host: exit 0`), which the test expects like serial lines. Commands are the test's own text; nothing the guest prints goes into them. Each has a time limit (180 s), and is killed with QEMU.
- The throughput check copies `/usr/lib/libcrypto.46.dylib` (1.5 MB) over ssh and compares its size with the guest's `wc -c`. It takes about a second on every machine (`host: copied 1595120 of 1595120 bytes in 1 s`), TCG included; a sanity check, not a benchmark.

**ICMP to the gateway.** QEMU 11.1's slirp answers echo requests to 10.0.2.2 (TTL 255, a millisecond or so), without privileges on the host, so `ping` is the first check.

**The machines' own NICs.** QEMU adds a default NIC only when no `-netdev` or `-nic` is given. `--user-net` therefore replaces `virt`'s default virtio-net (the test's device takes 00:01.0) and `sbsa-ref`'s built-in e1000e (absent; bochs-display stays at 00:01.0, the test's device is 00:02.0). Every other `virt` test still has the default NIC, a transitional virtio-net on QEMU's user network, which the driver now drives (named en0, never brought up); `sbsa-ref`'s e1000e has no driver.

## Findings

| Finding | Fix |
|---|---|
| IONetworkingFamily doesn't compile in xnu's iokit as-is: under `XNU_KERNEL_PRIVATE`, BSD's internal headers don't build as C++ there | the kext's view of the kernel (patch 0035, "What of IONetworkingFamily builds") |
| Interfaces wait for a user-space namer that doesn't exist | `NeoDarwinInterfaceNamer` ("Naming") |
| `netstat -I en0` reports 0 input packets while traffic flows. The SBSA kernel's Skywalk default (`IF_ATTACH_NX_DEFAULT` for a non-macOS target: `SKYWALK_NETWORKING_ENABLED`) attaches a netif *compat* nexus to every Ethernet interface, which sets `IFCAP_SKYWALK`; the DLIL input thread then skips its stats sync ("the stats are already incremented there", `dlil_input.c`), but IONetworkingFamily's input goes through `ifnet_input_extended` and that thread, so the counts are dropped. Output counts are unaffected. Confirmed: with the boot-arg `if_attach_nx=2` (flowswitch only, no compat netif) `netstat -I en0` counts input (4 packets after a 3-echo ping) | open, not NeoDarwin's code: the driver's `Statistics` has the real receive counts. Whether NeoDarwin's default should leave compat netif off (it buys nothing for legacy-TX interfaces, which get no flowswitch: "skip attaching fsw to en0 using legacy TX model") is for checkpoint 2 or P3-08 |
| The harness's first host-command hook only looked at its child when the next step was due, so a step waiting for `host: exit` never fired | `host_poll` on every pass of the watchdog loop; a timed-out command reports `host: exit 124` |
| Both `sbsa-ref`'s e1000e and `virt`'s default NIC vanish with `-netdev` | expected (QEMU's default NIC rule); the tests' PCI addresses are not asserted |

## Limits

- One queue pair, no multiqueue, no offloads (checksums, TSO), no `MRG_RXBUF`, no jumbo frames (MTU 1500), no control queue (no MAC filtering, VLAN filtering or MAC address change: `setHardwareAddress` is IONetworkingFamily's default, unsupported).
- No hot unplug handling beyond `stop` resetting the device; no `NEEDS_RESET` recovery (logged).
- No power management, no Wake-on-LAN.
- No KDP over Ethernet.
- Interface units are not persistent (the namer).

## What checkpoints 2 and 3 need

**Checkpoint 2, DHCP and a resolver.**
- A DHCP client. macOS's is `IPConfiguration` in bootp-527 (macOS 26.0's `bootp`), a configd plugin, which needs configd; NeoDarwin has neither. Options: a small first-party client run by launchd (Embedded Swift, like launchctl), or FreeBSD's `dhclient` (BSD; needs BPF, which xnu has). It sets the address and route with the same ioctls as `ifconfig` and `route`, and writes `/etc/resolv.conf` (or `/var/run/resolv.conf`, as macOS links it).
- What brings en0 up at boot: the DHCP client's launchd job, or launchctl's bootstrap next to the loopback set-up.
- libresolv-93 (macOS 26.0's) for `res_query` and `getrrsetbyname`; Libinfo's `getaddrinfo` asks mDNSResponder on macOS (`DNSServiceGetAddrInfo` through `libsystem_dnssd`), so either mDNSResponder-2881 comes too or Libinfo's resolver falls back to libresolv and `/etc/hosts` (a Libinfo module). OpenSSH's patch 0003 (no `getrrsetbyname`) can then go.
- QEMU's user network serves DHCP (10.0.2.15 from 10.0.2.2) and DNS (10.0.2.3, the host's resolver). The harness's user network lets the guest out through the host's NAT, as QEMU's default NIC always has (`restrict=on` would cut DNS too); the DNS proxy answers with the host's resolver, so a test can resolve `localhost` or a name in the host's own configuration without leaving the machine.
- The exit's "accept ssh from the host" is already proven here.
- Decide the `if_attach_nx` default ("Findings": input counters), and whether the namer moves to user space with the DHCP client.

**Checkpoint 3, the TC956x** (FreeBSD's `sys/dev/tcx`, BSD-2-Clause, James Kane, on the `radxa-dragon-q8b` branch of `../freebsd-src`).
- The board's TC956x is on PCIe segment 4 (`arm64-sbsa-bringup.md`, the board's table), behind the same SMMUv3 as the NVMe disk.
- An `IOEthernetController` shell like this one, with the register logic ported into a C header (as `nd_geni_uart.h` and `nd_dwc3.h` were), `PROVENANCE.md` naming the upstream commit.
- The chip is a PCIe switch with an internal endpoint whose two functions each hold an XGMAC 3.01, an XPCS and a SerDes; function 0 owns the chip's clocks, resets and the MAC's 64 GiB DMA window translation table. The PHY (a QCA8081) is reached over MDIO; the SerDes switches between 2500BASE-X and SGMII as the link changes. One TX and one RX DMA channel, a single MSI (`kIOInterruptTypePCIMessaged`, which IOPCIFamily and the ITS already support).
- DMA through a 36-bit window: `dma-address-bits` 36 (the IORT also says 36 for the Q8B's root complexes), so the bounce paths above run for real there, and descriptor addresses are translated (`pa + TC956X_DMA_OFFSET`).
- The descriptor rings are XGMAC's, not virtio's: `NDVirtqueue` doesn't apply, `NDStorageDMA` and the receive/transmit/batching structure of this driver do.
- Risks the FreeBSD driver records: a block whose clock is off or reset asserted doesn't answer, and on Qualcomm PCIe a failed read is an SError, so the bring-up order matters; the SMMUv3 must be in bypass (as for NVMe, `storage.md`); the switch's downstream link must be up before its functions enumerate.
- Exit: the TC956x takes a DHCP lease (checkpoint 2's client) and accepts ssh.
