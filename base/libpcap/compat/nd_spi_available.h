// SPDX-License-Identifier: BSD-2-Clause
// NeoDarwin-Language: expressibility: a compat header force-included into Apple's C libpcap and tcpdump builds.
//
// SPI_AVAILABLE and SPI_DEPRECATED_WITH_REPLACEMENT as Apple's internal SDK
// has them, for libpcap-144 and tcpdump-153 (docs/kernel/network.md,
// "libpcap and tcpdump"). There they are availability attributes, and clang
// gives a declaration carrying one default visibility, so libpcap's SPI
// (pcap-ng.h, pcap-util.h: the pcapng writer, pktap, the process and
// interface tables) is exported although the target builds with
// GCC_SYMBOLS_PRIVATE_EXTERN. The public SDK's <os/availability.h> defines
// them empty (#ifndef), which would leave 95 of macOS 26's 200 exports
// hidden. Only the visibility matters here: NeoDarwin has no older releases
// for the availability to describe.
#ifndef ND_SPI_AVAILABLE_H
#define ND_SPI_AVAILABLE_H
#define SPI_AVAILABLE(...) __attribute__((visibility("default")))
#define SPI_DEPRECATED_WITH_REPLACEMENT(...) __attribute__((visibility("default")))
#endif
