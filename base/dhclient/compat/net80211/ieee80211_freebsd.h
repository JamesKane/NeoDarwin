// SPDX-License-Identifier: BSD-2-Clause
// NeoDarwin-Language: expressibility: a compat header for FreeBSD's C dhclient, which includes it.
//
// FreeBSD's <net80211/ieee80211_freebsd.h>, for dhclient: its routing-socket
// handler follows 802.11 (re)association events (RTM_IEEE80211), which xnu's
// routing socket doesn't send; patch 0001 leaves that case out, so only the
// structure's name is needed.
#ifndef ND_DHCLIENT_IEEE80211_FREEBSD_H
#define ND_DHCLIENT_IEEE80211_FREEBSD_H
struct ieee80211_join_event;
#endif
