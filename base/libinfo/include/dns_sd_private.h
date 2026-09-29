/*
 * SPDX-License-Identifier: BSD-2-Clause
 * NeoDarwin-Language: expressibility: a C header standing in for one from Apple's internal SDK.
 *
 * Stands in for mDNSResponder's dns_sd_private.h for Libinfo's build;
 * mDNSResponder is not pinned in NeoDarwin yet. mdns_module.c uses one thing
 * from it beyond the public <dns_sd.h>: the query attribute
 * kDNSServiceAttrAllowFailover, which libsystem_dnssd exports (the SDK's
 * libsystem_dnssd.tbd lists it) and passes to
 * DNSServiceQueryRecordWithAttribute().
 */
#ifndef _DNS_SD_PRIVATE_H
#define _DNS_SD_PRIVATE_H

#include <dns_sd.h>

__BEGIN_DECLS

/* Lets a query fail over to another DNS service. */
extern const DNSServiceAttribute kDNSServiceAttrAllowFailover;

__END_DECLS

#endif /* _DNS_SD_PRIVATE_H */
