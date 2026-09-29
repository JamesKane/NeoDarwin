#!/usr/bin/env bash
# SPDX-License-Identifier: BSD-2-Clause
# mDNSResponder-2881.0.25's installed client headers, for the sysroot
# (docs/base/libsystem.md §2): replays libsystem_dnssd's headers phase (as in
# the last published project, 1310.140.1: dns_sd.h Public to usr/include,
# dns_sd_private.h Private to usr/local/include), with patches/ applied, and
# the version stamp Apple's build_scripts/update_dns_sd_h_version writes into
# the installed dns_sd.h (its sed, with the number
# build_scripts/project_version_string_to_integer makes of 2881.0.25).
#   install_headers.sh MDNSRESPONDER_SRC DSTROOT
set -euo pipefail
S="$1"; DST="$2"
PATCHES="$(cd "$(dirname "$0")" && pwd)/patches"
w="$(mktemp -d)"; trap 'rm -rf "$w"' EXIT
mkdir -p "$w/mDNSShared"; cp -L "$S"/mDNSShared/*.h "$S"/mDNSShared/*.c "$w/mDNSShared/"; chmod -R u+w "$w"
for p in "$PATCHES"/*.patch; do patch -d "$w" -p1 --quiet < "$p"; done
mkdir -p "$DST/usr/include" "$DST/usr/local/include"
sed "s/#define _DNS_SD_H .*/#define _DNS_SD_H 2881000025/" "$w/mDNSShared/dns_sd.h" > "$DST/usr/include/dns_sd.h"
cp "$w/mDNSShared/dns_sd_private.h" "$DST/usr/local/include/"
