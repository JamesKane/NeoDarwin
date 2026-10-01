// SPDX-License-Identifier: BSD-2-Clause
// NeoDarwin-Language: expressibility: a compat header for Apple's C traceroute and traceroute6, which include it.
//
// <pcap/pcap.h>, for traceroute and traceroute6 (docs/kernel/network.md,
// "P4-24"): they open a capture only for TCP probes (-P tcp), to see the
// target's SYN-ACK or RST, which no ICMP socket receives. NeoDarwin doesn't
// build libpcap yet, so pcap_create fails and a TCP traceroute exits with
// "pcap_open_live(en0) failed: libpcap is not built"; UDP and ICMP probes
// (the default and -I) never reach libpcap. Every other call is unreachable
// once pcap_create has failed, and fails if it is reached anyway.
#ifndef ND_NETWORK_CMDS_PCAP_H
#define ND_NETWORK_CMDS_PCAP_H
#include <stdio.h>
#include <string.h>
#include <sys/time.h>
#include <net/bpf.h>

#define PCAP_ERRBUF_SIZE 256
#define PCAP_NETMASK_UNKNOWN 0xffffffff

typedef struct nd_pcap pcap_t;

struct pcap_pkthdr {
	struct timeval ts;
	unsigned int caplen;
	unsigned int len;
};

static inline pcap_t *pcap_create(const char *source, char *errbuf) {
	(void)source;
	snprintf(errbuf, PCAP_ERRBUF_SIZE, "libpcap is not built");
	return NULL;
}
static inline pcap_t *pcap_open_live(const char *source, int snaplen, int promisc, int ms, char *errbuf) {
	(void)snaplen; (void)promisc; (void)ms;
	return pcap_create(source, errbuf);
}
static inline char *pcap_geterr(pcap_t *p) { (void)p; return (char *)"libpcap is not built"; }
static inline int pcap_set_snaplen(pcap_t *p, int n) { (void)p; (void)n; return -1; }
static inline int pcap_set_immediate_mode(pcap_t *p, int n) { (void)p; (void)n; return -1; }
static inline int pcap_set_buffer_size(pcap_t *p, int n) { (void)p; (void)n; return -1; }
static inline int pcap_setnonblock(pcap_t *p, int n, char *errbuf) {
	(void)p; (void)n;
	snprintf(errbuf, PCAP_ERRBUF_SIZE, "libpcap is not built");
	return -1;
}
static inline int pcap_activate(pcap_t *p) { (void)p; return -1; }
static inline int pcap_compile(pcap_t *p, struct bpf_program *fp, const char *str, int optimize, unsigned int mask) {
	(void)p; (void)fp; (void)str; (void)optimize; (void)mask;
	return -1;
}
static inline int pcap_setfilter(pcap_t *p, struct bpf_program *fp) { (void)p; (void)fp; return -1; }
static inline int pcap_get_selectable_fd(pcap_t *p) { (void)p; return -1; }
static inline int pcap_next_ex(pcap_t *p, struct pcap_pkthdr **h, const unsigned char **data) {
	(void)p; (void)h; (void)data;
	return -1;
}
static inline int pcap_datalink(pcap_t *p) { (void)p; return -1; }
static inline void pcap_close(pcap_t *p) { (void)p; }
#endif
